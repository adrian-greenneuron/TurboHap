# TurboHap

<p align="center">
  <img src="docs/assets/turbohap-logo.png" alt="TurboHap" width="820">
</p>

**The FindHap algorithm, reimplemented in C++. Byte-for-byte compatible output.
Up to 128× faster in DGX Spark benchmarks.**

TurboHap is a drop-in replacement for FindHap v3 for haplotype phasing and
genotype imputation. Keep your existing input files and options, and replace
the executable.

Choose standard TurboHap for speed, or TurboHap LM when RAM is limited.
Both modes produce identical result files.

On the tested datasets, result files match the original Fortran byte for byte,
except that multithreaded Fortran may write records in `cross.overs` in a
different order. The crossover records themselves match exactly. See the
[compatibility details](docs/COMPATIBILITY.md).

**Version 1.1.0.** One executable provides two modes:

| Mode | Command | Purpose |
| --- | --- | --- |
| **TurboHap** | `findhap 10` | The default, recommended for the fastest runs. |
| **TurboHap LM** | `findhap 10 --low-memory` | Uses less RAM; requires temporary disk space and usually takes longer. |

The number selects the CPU thread count: `10` means ten threads. Omitting it
uses the OpenMP default. The executable remains named `findhap` for compatibility.

## Manuscript and supporting data

Read the **[manuscript (PDF)](docs/paper/turbohap_manuscript_221026.pdf)**,
dated 22 October 2026. This author manuscript has not been peer reviewed.

- [Supplementary File 1 (PDF)](docs/paper/turbohap_supplementary_material_221026.pdf):
  methods, individual benchmark observations and simulation recipe.
- [Supplementary File 2 (ZIP)](docs/paper/turbohap_supplementary_archive_221026.zip?raw=true):
  measurements, source/build manifests, output checks, reference Fortran code,
  reproduction tools and figures.
- [Seven synthetic datasets and simulation truth (Zenodo)](https://doi.org/10.5281/zenodo.23566926),
  available under CC BY 4.0.

The manuscript describes v1.0.0; the v1.1.0 software benchmarks below are newer.
The manuscript and figures are also available under CC BY 4.0. See the
[paper download page](docs/paper/README.md) for file details and checksums.

## Performance

On a simulated dataset of **1,000,000 animals × 10,000 loci**, the original
Fortran baseline took **6 hours 53 minutes 40 seconds**.
TurboHap v1.1.0 finished in **3 minutes 40 seconds**: **112.6× faster**, with
**2.4% more peak memory**. TurboHap LM finished in **6 minutes 33 seconds**:
**63.1× faster**, with **25.1% less peak memory**.

All benchmarks used **ten CPU threads** on the same NVIDIA DGX Spark.
The GPU was not used. The Fortran measurements are retained from 21–22 October;
TurboHap and LM were measured on 28 October. Speedups therefore compare with a
historical baseline. Values are medians of two complete runs per program,
including input, computation, buffered output and temporary-file I/O.
The older baseline was measured on a shared server. Performance depends on the
dataset, options, storage, hardware and competing load.

Version 1.1.0 also improves performance on **AMD and Intel x86 computers**.
Detailed benchmarks are ongoing.

![Runtime and peak memory on seven synthetic datasets](docs/assets/turbohap_benchmarks_281026.png)

See [all results and methodology](docs/turbohap_benchmarks_281026.md), including
individual measurements, runtime, peak resident memory and output checks.

<!-- BENCHMARK_TABLES_START -->

### Runtime

Speedup is relative to the retained Fortran baseline measured on 21–22 October 2026.

| Animals × loci | Fortran baseline | TurboHap | Speedup | TurboHap LM | Speedup |
| --- | ---: | ---: | ---: | ---: | ---: |
| 10,000 × 1,000 | 10.52s | **0.66s** | **15.9×** | 1.10s | 9.6× |
| 10,000 × 10,000 | 1m 16s | **2.17s** | **34.9×** | 2.46s | 30.8× |
| 10,000 × 100,000 | 7m 28s | **14.89s** | **30.1×** | 23.71s | 18.9× |
| 100,000 × 1,000 | 14m 34s | **6.80s** | **128.4×** | 17.09s | 51.1× |
| 100,000 × 10,000 | 19m 42s | **19.96s** | **59.2×** | 27.55s | 42.9× |
| 100,000 × 100,000 | 1h 17m 00s | **2m 21s** | **32.8×** | 3m 45s | 20.6× |
| 1,000,000 × 10,000 | 6h 53m 40s | **3m 40s** | **112.6×** | 6m 33s | 63.1× |

### Peak memory

Negative percentages mean less memory than Fortran.

| Animals × loci | Fortran baseline | TurboHap | Memory change | TurboHap LM | Memory change |
| --- | ---: | ---: | ---: | ---: | ---: |
| 10,000 × 1,000 | 0.302 GiB | 0.182 GiB | -39.6% | **0.180 GiB** | **-40.3%** |
| 10,000 × 10,000 | 1.197 GiB | 0.766 GiB | -36.0% | **0.746 GiB** | **-37.7%** |
| 10,000 × 100,000 | 10.645 GiB | 6.312 GiB | -40.7% | **5.911 GiB** | **-44.5%** |
| 100,000 × 1,000 | 0.377 GiB | 0.332 GiB | -12.0% | **0.316 GiB** | **-16.2%** |
| 100,000 × 10,000 | 1.615 GiB | 1.504 GiB | -6.8% | **1.102 GiB** | **-31.8%** |
| 100,000 × 100,000 | 14.985 GiB | 13.026 GiB | -13.1% | **8.582 GiB** | **-42.7%** |
| 1,000,000 × 10,000 | 16.117 GiB | 16.496 GiB | +2.4% | **12.078 GiB** | **-25.1%** |

<!-- BENCHMARK_TABLES_END -->

## Build and run

A C++17 compiler with OpenMP and CMake 3.16 or newer are required.
From the source directory on Linux:

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

The build is tuned to the local CPU. Build on the machine that will run it.
Linux ARM64 and Intel/AMD x86-64 builds with GCC 13.3 have passed output checks.
Other Linux systems, macOS and Windows through WSL2 require local validation.
Native Windows is not supported. A GPU, CUDA and a Fortran compiler are unnecessary.

- [Installation and platform instructions](docs/INSTALL.md)
- [Inputs, outputs and every original option](docs/USAGE.md)
- [Output compatibility and validation](docs/COMPATIBILITY.md)
- [Small example](examples/README.md)

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
