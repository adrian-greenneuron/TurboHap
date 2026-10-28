# Changelog

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
