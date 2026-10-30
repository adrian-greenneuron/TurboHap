# TurboHap v1.2.0 benchmarks

30 October 2026. Seven synthetic datasets; one Fortran measurement and two measurements per TurboHap mode and dataset.

All 35 observations were collected on 30 October in separate studies. The refreshed GNU Fortran baseline uses expanded compiler flags with no taskset or added binding. The second Fortran replicate is deferred; all 28 previously collected TurboHap observations are unchanged. Historical baselines remain available separately.

Fortran values are single observations; TurboHap values are medians of two technical runs per mode. Runtime and RSS medians are calculated independently. Bold marks the lowest recorded runtime or RSS and its corresponding ratio, not statistical significance. No Fortran variation estimate is available yet.

### Runtime

Speedup uses the single expanded-flag Fortran measurement and the median of two TurboHap measurements per mode, all collected on 30 October 2026.

| Animals × loci | Fortran baseline | TurboHap | Speedup | TurboHap LM | Speedup |
| --- | ---: | ---: | ---: | ---: | ---: |
| 10,000 × 1,000 | 10.23s | **0.52s** | **19.7×** | 0.55s | 18.8× |
| 10,000 × 10,000 | 1m 12s | **1.71s** | **42.2×** | 1.98s | 36.5× |
| 10,000 × 100,000 | 7m 22s | **11.91s** | **37.1×** | 20.46s | 21.6× |
| 100,000 × 1,000 | 14m 03s | **5.59s** | **150.8×** | 5.96s | 141.3× |
| 100,000 × 10,000 | 19m 05s | **16.41s** | **69.8×** | 19.09s | 60.0× |
| 100,000 × 100,000 | 1h 17m 08s | **1m 53s** | **41.0×** | 3m 14s | 23.9× |
| 1,000,000 × 10,000 | 6h 44m 06s | **3m 06s** | **130.0×** | 3m 44s | 108.2× |

### Peak memory

Negative percentages mean less memory than Fortran.

| Animals × loci | Fortran baseline | TurboHap | Memory change | TurboHap LM | Memory change |
| --- | ---: | ---: | ---: | ---: | ---: |
| 10,000 × 1,000 | 0.301 GiB | 0.183 GiB | -39.4% | **0.181 GiB** | **-40.0%** |
| 10,000 × 10,000 | 1.197 GiB | 0.766 GiB | -36.0% | **0.747 GiB** | **-37.6%** |
| 10,000 × 100,000 | 10.645 GiB | 6.312 GiB | -40.7% | **5.910 GiB** | **-44.5%** |
| 100,000 × 1,000 | 0.377 GiB | 0.333 GiB | -11.8% | **0.320 GiB** | **-15.2%** |
| 100,000 × 10,000 | 1.614 GiB | 1.505 GiB | -6.8% | **1.111 GiB** | **-31.2%** |
| 100,000 × 100,000 | 14.985 GiB | 13.026 GiB | -13.1% | **8.582 GiB** | **-42.7%** |
| 1,000,000 × 10,000 | 16.116 GiB | 16.496 GiB | +2.4% | **12.079 GiB** | **-25.1%** |

![Runtime and peak memory](assets/turbohap_benchmarks_301026.png)

[Download runtime and memory figure (SVG)](assets/turbohap_benchmarks_301026.svg)

![Speedup over Fortran](assets/turbohap_benchmarks_301026_speedup.png)

[Download speedup figure (SVG)](assets/turbohap_benchmarks_301026_speedup.svg)

![Memory change relative to Fortran](assets/turbohap_benchmarks_301026_memory.png)

[Download memory change figure (SVG)](assets/turbohap_benchmarks_301026_memory.svg)

## Methodology

- Hardware: NVIDIA DGX Spark; 20 logical CPU cores; Ubuntu 24.04 ARM64.
- Ten OpenMP threads for every measured program. GCC/gfortran 13.3.0.
- Fortran flags: `-O3 -march=native -funroll-loops -flto -flto-partition=1to1 -fopenmp -ffp-contract=off -fno-underscoring`. No fast-math or profile-guided optimisation.
- C++ release flags: `-O3 -march=native -funroll-loops -flto -ffp-contract=off -std=gnu++17 -fopenmp`; GNU linking uses `-flto-partition=1to1`. No fast-math or profile-guided optimisation.
- The seven datasets were generated with genosimV4; dimensions are genotyped animals × map loci. Each pedigree also contains four ungenotyped founders. Chip membership and pedigree structure affect work; see the [generation recipe and input checksums](turbohap_benchmark_datasets_221026.md).
- Wall time covers the complete executable, including reading inputs, computation, writing results and temporary-file I/O. Output validation is outside the timer.
- Peak memory is process peak resident set size from GNU `time`, converted from KiB to GiB. It is not total system memory, filesystem cache or disk usage.
- Each dataset used standard-1, LM-1, LM-2, standard-2 order. All 28 C++ observations are included unchanged. Fortran used one run per dataset in ascending animal count, then locus count; all seven are included.
- Both C++ modes use the default automatic CPU policy: the full 20-core container allocation is available, and the launcher selects the ten Cortex-X925 cores (logical IDs 5–9 and 15–19). No taskset or additional worker binding is used. The new Fortran runs had access to all 20 cores with ten threads, no taskset and no added binding. Both C++ modes use `LC_ALL=C`, `OMP_NUM_THREADS=10` and `OMP_DYNAMIC=FALSE`; explicit placement variables are unset.
- All primary elapsed times use GNU time (%e), excluding measurement-wrapper overhead. Fortran wrapper wall time is retained separately in the raw observations. Studies used different sessions and the placement policies above.
- A clean release layout, with CMake project version 1.2.0, was frozen and built before measurement. All 19 packaged tests passed. Source and measured executable hashes are included in the JSON metadata. Every benchmark input matches its published SHA-256 and byte count, including the million-animal benchmark options.
- Inputs were explicitly warmed and residency checked before each timed C++ run. There were no inference warmup runs or forced cache eviction. Outputs were fresh; post-exit checked file/directory fsync and hashing occurred outside the timer. Fortran inputs were verified before its study, with no forced cache eviction or inference warmups. No competing benchmark was intentionally run; unrelated background activity was not comprehensively traced. One Fortran observation and two C++ technical repeats do not establish confidence intervals, statistical equivalence or universal speedup.
- All 280 C++ raw-file checks pass. Fortran passes 63/70 raw-file checks: only cross.overs record order differs. Its header and every complete record match after C-locale sorting with duplicates retained. These seven raw mismatches remain explicit; no other difference is accepted.

[All observations (CSV)](turbohap_benchmarks_301026.csv) · [Metadata and observations (JSON)](turbohap_benchmarks_301026.json) · [Raw output checks](turbohap_validation_301026.json) · [Compatibility](COMPATIBILITY.md)

[Historical v1.1.0 benchmarks](turbohap_benchmarks_281026.md) · [Historical v1.0.0 benchmarks](turbohap_benchmarks_221026.md). The manuscript and supplementary files continue to describe v1.0.0; they have not been revised for this software release.

## Regenerate the tables and figures

With Python 3 and the versions in `tools/requirements-reporting.txt` installed, run `python3 tools/render_benchmarks.py` from the source directory. This reads the included aggregate JSON and does not access input genotypes or run either program.

The same renderer updates the README headline, performance summary and release notes. See [release preparation](RELEASING.md) for complete-package checks and checksums.
