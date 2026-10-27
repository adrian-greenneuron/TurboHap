# TurboHap

<p align="center">
  <img src="docs/assets/turbohap-logo.png" alt="TurboHap" width="820">
</p>

**The FindHap algorithm, reimplemented in C++. Byte-for-byte compatible output.
Up to 93× faster.**

TurboHap is a drop-in replacement for FindHap v3 for haplotype phasing and
genotype imputation. Keep your existing input files and options, and replace
the executable.

Choose standard TurboHap for speed, or TurboHap LM when RAM is limited.
Both modes produce identical result files.

On the tested datasets, result files match the original Fortran byte for byte,
except that multithreaded Fortran may write records in `cross.overs` in a
different order. The crossover records themselves match exactly. See the
[compatibility details](docs/COMPATIBILITY.md).

**Version 1.0.0.** One executable provides two modes:

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

The manuscript and figures are also available under CC BY 4.0. See the
[paper download page](docs/paper/README.md) for file details and checksums.

## Performance

On a simulated dataset of **1,000,000 animals × 10,000 loci**, the original
Fortran baseline took **6 hours 53 minutes 40 seconds**.
TurboHap finished in **4 minutes 26 seconds**: **93.4× faster**, with
**2.2% more peak memory**. TurboHap LM finished in **7 minutes 22 seconds**:
**56.2× faster**, with **25.1% less peak memory**.

All benchmarks used **ten CPU threads** on the same NVIDIA DGX Spark.
The GPU was not used. Values are medians of two complete runs per program,
including reading inputs, computation, writing results and temporary-file I/O.
The server also ran other work during measurement. Performance depends on the
dataset, options, storage, hardware and competing load.

![Runtime and peak memory on seven synthetic datasets](docs/assets/turbohap_benchmarks_221026.png)

See [all results and methodology](docs/turbohap_benchmarks_221026.md), including
individual measurements, runtime, peak resident memory and output checks.

<!-- BENCHMARK_TABLES_START -->

### Runtime

Speedup is relative to the original Fortran baseline.

| Animals × loci | Fortran baseline | TurboHap | Speedup | TurboHap LM | Speedup |
| --- | ---: | ---: | ---: | ---: | ---: |
| 10,000 × 1,000 | 10.52s | **1.06s** | **10.0×** | 1.49s | 7.1× |
| 10,000 × 10,000 | 1m 16s | **2.64s** | **28.6×** | 2.99s | 25.2× |
| 10,000 × 100,000 | 7m 28s | **15.99s** | **28.0×** | 24.32s | 18.4× |
| 100,000 × 1,000 | 14m 34s | **15.04s** | **58.1×** | 21.33s | 41.0× |
| 100,000 × 10,000 | 19m 42s | **23.74s** | **49.8×** | 32.15s | 36.8× |
| 100,000 × 100,000 | 1h 17m 00s | **2m 34s** | **30.0×** | 3m 57s | 19.5× |
| 1,000,000 × 10,000 | 6h 53m 40s | **4m 26s** | **93.4×** | 7m 22s | 56.2× |

### Peak memory

Negative percentages mean less memory than Fortran.

| Animals × loci | Fortran baseline | TurboHap | Memory change | TurboHap LM | Memory change |
| --- | ---: | ---: | ---: | ---: | ---: |
| 10,000 × 1,000 | 0.302 GiB | 0.182 GiB | -39.6% | **0.180 GiB** | **-40.5%** |
| 10,000 × 10,000 | 1.197 GiB | 0.765 GiB | -36.1% | **0.746 GiB** | **-37.7%** |
| 10,000 × 100,000 | 10.645 GiB | 6.312 GiB | -40.7% | **5.910 GiB** | **-44.5%** |
| 100,000 × 1,000 | 0.377 GiB | 0.331 GiB | -12.2% | **0.316 GiB** | **-16.3%** |
| 100,000 × 10,000 | 1.615 GiB | 1.499 GiB | -7.2% | **1.101 GiB** | **-31.8%** |
| 100,000 × 100,000 | 14.985 GiB | 13.025 GiB | -13.1% | **8.581 GiB** | **-42.7%** |
| 1,000,000 × 10,000 | 16.117 GiB | 16.472 GiB | +2.2% | **12.077 GiB** | **-25.1%** |

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
Linux ARM64 with GCC 13.3 is validated; other Linux systems, macOS and Windows
through WSL2 have documented build routes but remain untested. Native Windows
is not supported. A GPU, CUDA and a Fortran compiler are unnecessary.

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
