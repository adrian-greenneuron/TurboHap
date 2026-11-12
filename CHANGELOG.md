# Changelog

## 1.5.0 — 12 November 2026

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

See the [release notes](docs/turbohap_release_notes_111126.md) and [benchmark report](docs/turbohap_benchmarks_111126.md).

## 1.4.0 — 5 November 2026

- A separate exhaustive `turbohap-validate-inputs` auditor checks all ordinary inputs and active prior files, including cross-file references.
- Informative reports provide exact issue counts, bounded location examples, suggested fixes, Markdown/JSON output and an optional complete issue TSV.
- Strict exhaustive genotype/map validation is enabled by default in both inference modes. Explicit warning mode supports diagnosis of safe legacy layout ambiguities.
- `turbohap-build-info` records source, compiler, OpenMP runtime and inference executable identity.
- Fresh ten-thread, five-dataset Spark benchmarks: two runs per mode and all 200 raw output checks passed. The five Fortran observations remain unchanged. See [results and methodology](https://github.com/adrian-greenneuron/TurboHap/blob/v1.4.0/docs/turbohap_benchmarks_051126.md).
- Existing native CPU policies and result-file formats are retained. The manuscript and supplementary material are unchanged.

## 1.3.0 — 1 November 2026

- A unified implementation for Apple Silicon macOS, Linux ARM64 and Intel/AMD
  x86-64. Detailed benchmarking across these systems is ongoing.
- Updated native installation instructions, automatic platform defaults and
  support for an external OpenMP runtime on macOS.
- More compact working storage, improved ordered matching/library processing
  and native instruction selection, with unchanged input and result formats.
- Adaptive row scheduling for requests spanning clearly separated core classes;
  the ordinary static path remains available. User thread counts, affinity and
  explicit OpenMP settings take precedence.
- Updated five-dataset DGX Spark benchmark tables, figures and raw checks;
  each current benchmark has at least 10,000 loci.
- Refreshed TurboHap logo.
  The existing GNU Fortran baseline is reused unchanged. See the
  [results and methodology](https://github.com/adrian-greenneuron/TurboHap/blob/v1.3.0/docs/turbohap_benchmarks_011126.md).
- The bundled v1.0.0 manuscript, supplementary material and historical benchmark
  records remain unchanged.

## 1.2.0 — 30 October 2026

- Faster matching, bounded LM lookup and output formatting, with native CPU
  selection for the qualified paths. Input formats, options and commands remain
  compatible with existing workflows.
- Automatic performance-core selection on DGX Spark when the requested thread
  count fits, with no added worker binding. Explicit affinity and OpenMP settings
  take precedence; `--cpu-policy=inherit` retains the caller's placement.
- Automatic core binding on native Milan, Cascade Lake and qualified Turin
  builds, with explicit OpenMP settings preserved. Detailed x86 benchmarks are
  ongoing.
- More efficient pedigree rematching on Spark and checked output finalisation:
  write, flush and close failures now cause an error rather than a success message.
- Fresh seven-dataset Spark benchmarks: two repeats per mode, all 28 runs and
  280 strict raw output checks pass. Standard is faster and LM uses less peak
  resident memory in every measured dataset. See the
  [results and methodology](https://github.com/adrian-greenneuron/TurboHap/blob/v1.2.0/docs/turbohap_benchmarks_301026.md).
- Refreshed GNU Fortran 13.3 baseline: one expanded-flag run per dataset, ten
  threads, no taskset or added binding. The second replicate is deferred; all
  28 TurboHap observations are unchanged. Sample sizes are explicit in the
  tables and figures.
- Historical Fortran records and the bundled v1.0.0 manuscript and supplementary
  material remain unchanged.

## 1.1.0 — 28 October 2026

- Faster population matching and pedigree processing in standard and LM modes.
- Improved performance on AMD and Intel x86 computers; detailed benchmarks are
  ongoing. Build on the target machine to select its native CPU paths.
- Existing input files, options and executable commands continue to work.
  All seven synthetic datasets passed exact result-file checks in both modes,
  including crossover record order.
- Updated DGX Spark runtime and memory tables, figures and individual
  observations. The original Fortran measurements are retained as a historical
  baseline; see the [benchmark methodology](https://github.com/adrian-greenneuron/TurboHap/blob/v1.1.0/docs/turbohap_benchmarks_281026.md).
- The bundled manuscript and supplementary material continue to describe v1.0.0.

## 1.0.0 — 22 October 2026

Initial public release, providing standard TurboHap and TurboHap LM in one
executable. See the [original benchmarks](https://github.com/adrian-greenneuron/TurboHap/blob/v1.0.0/docs/turbohap_benchmarks_221026.md).
