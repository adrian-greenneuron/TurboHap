# Changelog

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
  [results and methodology](docs/turbohap_benchmarks_301026.md).
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
  baseline; see the [benchmark methodology](docs/turbohap_benchmarks_281026.md).
- The bundled manuscript and supplementary material continue to describe v1.0.0.

## 1.0.0 — 22 October 2026

Initial public release, providing standard TurboHap and TurboHap LM in one
executable. See the [original benchmarks](docs/turbohap_benchmarks_221026.md).
