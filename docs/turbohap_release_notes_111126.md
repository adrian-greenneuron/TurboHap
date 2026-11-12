# TurboHap v1.5.0 release notes

12 November 2026

## Changes from v1.4.0

<!-- VERSION_COMPARISON_START -->
- **Faster, with lower peak memory than v1.4.0.** Across 8 matched datasets, standard TurboHap delivers a **1.22× speedup** with a **15% reduction in peak memory**. TurboHap LM delivers a **1.27× speedup** with a **20% reduction in peak memory**. Tests used 20 CPU threads on an NVIDIA DGX Spark, one run per version and mode on each dataset. Summaries use geometric means of the paired ratios. [Compare all six releases](turbohap_release_comparison_111126.md). <!-- VERSION_COMPARISON_END -->
- **Optional modern console output.** `--stdout=modern` adds UTC date/time stamps,
  elapsed phase/iteration progress, peak live haplotypes before pruning and
  capacity summaries. Legacy STDOUT remains the release default. Neither mode
  changes the legacy result files; shell redirection can save a run log.
- **Lower library memory on qualified native profiles.** Frequency/order metadata
  uses demand-zero allocation so unused capacity need not become resident RAM.
  Active library bounds avoid work on unpublished rows. Both standard and LM
  benefit, independently of whether maxhap exceeds 65,535. This is not automatic
  capacity sizing; maxhap remains a hard per-segment limit.
- **Adaptive LM lookup positions.** Qualified Spark, Granite Rapids and Turin
  paths retain 16-bit candidate positions through 65,535 indexed entries and
  can widen to 32 bits beyond that, within the existing lookup memory budget.
  Width follows the live index size. Standard mode is unaffected by this
  LM-specific lookup; other builds retain their compatible fallback paths.
- **Faster exact population matching.** Reused sampled signatures, fused validity
  checks, vectorized normalization/backsolving and ordered bitmap candidate
  filtering reduce repeated work while retaining candidate and tie order.
- **Faster pedigree work.** Integer base-frequency vectorization, bounded
  single-conflict tests, early rejection and segment-owned progeny work reduce
  processing and synchronization without changing inference decisions.
- **Native x86 implementations.** Qualified Intel Granite Rapids and AMD Turin
  profiles select AVX2 population/byte/integer loops, AVX-512BW pedigree conflict
  checks and BMI2 signature extraction under CPU-feature guards. Other x86
  processors retain their established paths; ARM NEON is not executed on x86.
- **Shared ARM64 final scan.** Exact NEON validation-and-fill in the final
  population scan is enabled on supported ARM64 builds, including Apple Silicon
  and Google Axion. The scalar fallback is retained. This kernel is not M4-only.
- **Metadata and prior-output ordering.** Library export preserves the published
  permutation and interprets unpublished slots consistently, maintaining output
  identity when metadata-based allocation and imported priors are used.
- **Optional M4 Pro profile.** A bounded native Apple Clang/libomp configuration
  is available with `FINDHAP_MAC_COHORT_PROFILE=ON`. It is off by default, uses
  fixed M4-specific compiler settings and no profile-guided optimization.
  Select `--row-schedule=dynamic` explicitly for that configuration. General
  Apple Silicon builds retain their ordinary platform policy.
- **New benchmark suite and documentation.** Nine realistic synthetic cohorts,
  20-thread measurements, updated graphs/tables, a dataset-methods document,
  revised platform installation instructions and reproducible release checks.

The standalone exhaustive input auditor, strict in-run validation, file formats
and automatic CPU-placement interfaces are retained from v1.4.0. The new
benchmarks use different datasets and thread counts from v1.4.0; they are not
a direct version-to-version timing comparison. The historical manuscript and
supplementary downloads remain unchanged.


## New benchmark suite

The current comparison uses new structured synthetic cattle cohorts at 10k,
25k, 100k and 250k animals with 20k and 80k loci, plus a 1M × 80k demonstration.
All use 20 CPU threads on DGX Spark. Two repetitions per TurboHap mode are
included; FindHap has two per dataset through 100k animals and one per 250k
dataset. These different workloads replace the earlier five-dataset table;
the figures do not establish a direct performance change from v1.4.0.

<!-- BENCHMARK_SUMMARY_START -->
This comparison includes **36 TurboHap measurements**, **14 GNU FindHap measurements** and **360 passing raw TurboHap output-file checks**. The largest completed matched-control speedup is **116.4×**; the largest measured memory reduction is **92.2%**. All eight planned FindHap controls are complete. No FindHap control is planned for 1,000,000 × 80,000 because of time and resource limits.
<!-- BENCHMARK_SUMMARY_END -->

Read the [method and complete measurements](turbohap_benchmarks_111126.md) and
[dataset-generation recipe](SYNTHETIC_DATASETS.md). Historical manuscript files remain frozen.

The exact inputs for all nine benchmarks are available on Zenodo. [Dataset DOI 10.5281/zenodo.24144449](https://doi.org/10.5281/zenodo.24144449)

## Installation and compatibility

Source builds support Linux ARM64, Intel/AMD x86-64 and Apple Silicon macOS.
See [installation](INSTALL.md) and the [Mac guide](../MACOS.md). Build natively
on the intended host. No GPU, CUDA or Fortran compiler is required.

Both modes preserve the ten legacy result formats. Modern STDOUT is optional:
`findhap 20 --stdout=modern > run.log 2> diagnostics.log`.
Release builds keep legacy STDOUT as the default. Fortran crossover record-order
differences remain explicitly documented in [compatibility](COMPATIBILITY.md).
