TurboHap v1.2.0 improves matching, low-memory lookup and output processing while retaining the existing commands, options and result-file formats.

- Automatic performance-core selection on DGX Spark, respecting explicit affinity and OpenMP settings.
- Improved performance on AMD and Intel x86 computers; detailed benchmarks are ongoing.
- Checked output finalisation reports write, flush and close failures correctly.
- Seven synthetic datasets: 35 observations, with 1 Fortran and 2 per TurboHap mode/dataset. All 280 C++ raw-file checks pass. The unchanged release code passed all 19 packaged tests.
- The refreshed GNU Fortran 13.3 baseline uses expanded flags, ten threads, no taskset or added binding. Its second replicate is deferred. Fortran passes exact-record checks; only crossover record ordering differs in raw checksums.

On **1,000,000 × 10,000**, Fortran took **24,245.51 s**. Median complete runtime is **186.44 s standard** and **224.08 s LM**, giving **130.0×** and **108.2×** speedup. The maximum is **150.8×**, on 100,000 × 1,000. Fortran values are single observations; TurboHap values are medians of two technical runs. All measurements were collected on 30 October 2026 in separate studies.

Standard is faster and LM uses less peak resident memory on every measured dataset.

[All observations and methodology](https://github.com/adrian-greenneuron/TurboHap/blob/v1.2.0/docs/turbohap_benchmarks_301026.md) · [Output validation](https://github.com/adrian-greenneuron/TurboHap/blob/v1.2.0/docs/turbohap_validation_301026.json).

Build natively on the intended CPU with a C++17/OpenMP compiler and CMake. One executable provides `findhap 10` and `findhap 10 --low-memory`; no GPU or Fortran compiler is needed.

The manuscript and supplementary material continue to describe v1.0.0 and are unchanged.
