TurboHap v1.3.0 reduces working memory and improves genotype processing, matching and output generation while preserving the existing commands, options and result-file formats.

- Native installation support for Apple Silicon Macs, Linux ARM64, and Intel and AMD x86-64 systems. Detailed benchmarking on these systems is ongoing.
- Automatic CPU placement respects user affinity and OpenMP settings; adaptive work scheduling is used when eligible heterogeneous cores require it.
- Native NEON or AVX-512 paths are selected where supported, with portable fallbacks.
- Checked output finalisation reports write, flush and close failures correctly.
- 5 synthetic DGX Spark datasets, each with at least 10,000 loci: 25 observations, with 1 Fortran and 2 per TurboHap mode/dataset. All 200 C++ raw-file checks pass. The packaged release built successfully and passed all 33 packaged regression tests.
- The unchanged GNU Fortran 13.3 baseline uses expanded flags, ten threads, no taskset or added binding. It has one observation per dataset. Fortran passes exact-record checks; only crossover record ordering differs in raw checksums.

On **1,000,000 × 10,000**, Fortran took **24,245.51 s**. Median complete runtime is **169.80 s standard** and **215.66 s LM**, giving **142.8×** and **112.4×** speedup. The maximum is **142.8×**, on 1,000,000 × 10,000. Fortran values are single observations; TurboHap values are medians of two technical runs. GNU Fortran measurements: 30 October 2026; TurboHap measurements: 1 November 2026. These are separate measurement sessions.

Standard is faster and LM uses less peak resident memory on every measured dataset.

[All observations and methodology](https://github.com/adrian-greenneuron/TurboHap/blob/v1.3.0/docs/turbohap_benchmarks_011126.md) · [Output validation](https://github.com/adrian-greenneuron/TurboHap/blob/v1.3.0/docs/turbohap_validation_011126.json).

Build natively on the intended CPU with a C++17/OpenMP compiler and CMake. One executable provides `findhap 10` and `findhap 10 --low-memory`; no GPU or Fortran compiler is needed. See the README and MACOS.md for installation.

The manuscript and supplementary material continue to describe v1.0.0 and are unchanged.
