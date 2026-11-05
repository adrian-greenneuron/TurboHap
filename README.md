# TurboHap

<p align="center">
  <img src="docs/assets/turbohap-logo.png" alt="TurboHap" width="820">
</p>

**The FindHap algorithm, reimplemented in C++. Byte-for-byte compatible output.
Up to 149.4× faster in DGX Spark benchmarks.**

TurboHap is a drop-in replacement for FindHap v3 for haplotype phasing and
genotype imputation. Keep your existing input files and options, and replace
the executable.

Choose standard TurboHap for speed, or TurboHap LM when RAM is limited.
Both modes produce identical result files.

On the tested datasets, result files match the original Fortran byte for byte,
except that multithreaded Fortran may write records in `cross.overs` in a
different order. The crossover records themselves match exactly. See the
[compatibility details](docs/COMPATIBILITY.md).

**Version 1.4.0.** One executable provides two modes:

| Mode | Command | Purpose |
| --- | --- | --- |
| **TurboHap** | `findhap 10` | The default, recommended for the fastest runs. |
| **TurboHap LM** | `findhap 10 --low-memory` | Uses less RAM; requires temporary disk space and usually takes longer. |

The number selects the CPU thread count: `10` means ten threads. You can omit
it to use the platform default, with explicit OpenMP settings respected. On
Apple Silicon, automatic selection prefers performance cores when no count is
specified. The executable remains named `findhap` for compatibility.

## Manuscript and supporting data

Read the **[manuscript (PDF)](docs/paper/turbohap_manuscript_221026.pdf)**,
dated 22 October 2026. This author manuscript has not been peer reviewed.

- [Supplementary File 1 (PDF)](docs/paper/turbohap_supplementary_material_221026.pdf):
  methods, individual benchmark observations and simulation recipe.
- [Supplementary File 2 (ZIP)](docs/paper/turbohap_supplementary_archive_221026.zip?raw=true):
  measurements, source/build manifests, output checks, reference Fortran code,
  reproduction tools and figures.
- [Synthetic datasets and simulation truth (Zenodo)](https://doi.org/10.5281/zenodo.23566926),
  available under CC BY 4.0. The archive retains all seven datasets from the
  manuscript; the current software comparison uses the five with at least
  10,000 loci.

The manuscript describes v1.0.0; the v1.4.0 software benchmarks below are newer.
The manuscript and figures are also available under CC BY 4.0. See the
[paper download page](docs/paper/README.md) for file details and checksums.

## Performance

On a simulated dataset of **1,000,000 animals × 10,000 loci**, GNU Fortran
FindHap took **6 hours 44 minutes 6 seconds**.
TurboHap v1.4.0 finished in **2 minutes 42 seconds**: **149.4× faster**, with
**31.2% less peak memory**. TurboHap LM finished in **3 minutes 28 seconds**:
**116.8× faster**, with **58.6% less peak memory**.

All benchmarks used **ten CPU threads** and **GCC/gfortran 13.3** on the same
NVIDIA DGX Spark. The GPU was not used. TurboHap measurements were collected on
5 November 2026; the existing Fortran measurements from 30 October are reused.
They were collected in separate studies. Fortran uses expanded optimisation flags,
all 20 CPUs eligible, no taskset and no added binding. Its values are
**single observations**; TurboHap and LM use **medians of two runs per mode**.
The second Fortran replicate is deferred. Complete executable times include
strict input validation, input, computation, buffered output and temporary-file I/O. Performance
depends on the dataset, options, storage, hardware and competing load.

TurboHap works on **Apple Silicon Macs, Linux ARM64, and Intel/AMD x86-64
systems**. Detailed benchmarking on these systems is ongoing.

![Runtime and peak memory on five synthetic datasets with at least 10,000 loci](docs/assets/turbohap_benchmarks_051126.png)

See [all results and methodology](docs/turbohap_benchmarks_051126.md), including
individual measurements, runtime, peak resident memory and output checks.

<!-- BENCHMARK_TABLES_START -->

### Runtime

Speedup uses the unchanged single expanded-flag Fortran measurement and the median of two TurboHap measurements per mode. GNU Fortran measurements: 30 October 2026; TurboHap measurements: 5 November 2026.

| Animals × loci | Fortran baseline | TurboHap | Speedup | TurboHap LM | Speedup |
| --- | ---: | ---: | ---: | ---: | ---: |
| 10,000 × 10,000 | 1m 12s | **1.57s** | **46.1×** | 1.83s | 39.4× |
| 10,000 × 100,000 | 7m 22s | **10.46s** | **42.3×** | 19.25s | 23.0× |
| 100,000 × 10,000 | 19m 05s | **14.71s** | **77.8×** | 18.07s | 63.4× |
| 100,000 × 100,000 | 1h 17m 08s | **1m 42s** | **45.5×** | 3m 05s | 25.0× |
| 1,000,000 × 10,000 | 6h 44m 06s | **2m 42s** | **149.4×** | 3m 28s | 116.8× |

### Peak memory

Negative percentages mean less memory than Fortran.

| Animals × loci | Fortran baseline | TurboHap | Memory change | TurboHap LM | Memory change |
| --- | ---: | ---: | ---: | ---: | ---: |
| 10,000 × 10,000 | 1.197 GiB | 0.230 GiB | -80.8% | **0.208 GiB** | **-82.7%** |
| 10,000 × 100,000 | 10.645 GiB | 1.622 GiB | -84.8% | **1.184 GiB** | **-88.9%** |
| 100,000 × 10,000 | 1.614 GiB | 0.984 GiB | -39.0% | **0.562 GiB** | **-65.2%** |
| 100,000 × 100,000 | 14.985 GiB | 8.431 GiB | -43.7% | **3.942 GiB** | **-73.7%** |
| 1,000,000 × 10,000 | 16.116 GiB | 11.084 GiB | -31.2% | **6.676 GiB** | **-58.6%** |

<!-- BENCHMARK_TABLES_END -->

## Build and run

A C++17 compiler with OpenMP and CMake 3.16 or newer are required.
Download the [v1.4.0 source release](https://github.com/adrian-greenneuron/TurboHap/releases/tag/v1.4.0)
and extract it, or check out the `v1.4.0` tag.

On Linux, install your distribution’s C++/OpenMP compiler and CMake, then run
these commands from the extracted source directory:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=g++
cmake --build build --parallel 2
./build/findhap --help
```

Run from a directory containing `findhap.options`, `pedigree.file`,
`chromosome.data` and `genotypes.txt`:

```sh
/path/to/turbohap/build/findhap 10
# Or select TurboHap LM:
/path/to/turbohap/build/findhap 10 --low-memory
```

On **Apple Silicon macOS**, install Apple’s Command Line Tools and Homebrew
GCC, libomp and CMake; follow the [native Mac installation instructions](docs/INSTALL.md#macos-apple-silicon).
The build selects the native Mac defaults automatically.

The build is tuned to the local CPU. Build on the machine that will run it and
keep its C++/OpenMP runtime installed. The default CPU policy respects explicit
CPU and OpenMP settings; see [CPU placement](docs/USAGE.md#cpu-placement).
Windows users can follow the Linux route inside WSL2; native Windows is not
supported. A GPU, CUDA and a Fortran compiler are unnecessary.

With Python 3.11 or newer available, check a fresh build with
`ctest --test-dir build --output-on-failure`. See the installation guide for
platform support and test requirements.

- [Installation and platform instructions](docs/INSTALL.md)
- [Inputs, outputs and every original option](docs/USAGE.md)
- [Output compatibility and validation](docs/COMPATIBILITY.md)
- [Small example](examples/README.md)

## Check your inputs before inference

v1.4.0 includes a separate exhaustive input auditor. It checks all ordinary
input files and active prior files, including cross-file references, without
running inference or modifying inputs and results:

```sh
./build/turbohap-validate-inputs --directory /path/to/inputs
```

The report counts errors and review warnings, explains suggested fixes and
gives example line/byte locations. Save Markdown/JSON reports or an optional
complete issue TSV when needed. Ordinary inference retains strict checks by
default in both modes. See the [input-validation guide](docs/INPUT_VALIDATION.md)
for coverage, exit codes and diagnostic warning mode.

`./build/turbohap-build-info --build-info` reports build details and the SHA-256
of the compiled `findhap` executable without reading a dataset.

## Licence and commercial use

**Free for non-commercial academic research and teaching. Commercial use
requires a paid licence.**

This includes paid imputation, genetic evaluation and consultancy services
delivered by universities, charities or government institutions, even when
charges only recover costs. Ordinary research grants do not, by themselves,
make academic research commercial.

TurboHap is licensed by **Green Neuron Ltd, trading as Green Neuron AI**.
Using TurboHap gives us no ownership of your data or results. Publications
using the software must acknowledge TurboHap and the original FindHap method.

See the [full licence](LICENSE) for terms, or contact
[Adrian Carr](mailto:adrian.carr@greenneuron.ai) to discuss commercial licensing.

The original FindHap algorithm and Fortran program were developed by
Paul M. VanRaden at USDA. Original attribution and licence notices are retained.
See [credits and references](docs/ACKNOWLEDGEMENTS.md) and [citation metadata](CITATION.cff).
