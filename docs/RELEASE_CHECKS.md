# TurboHap v1.5.0 release qualification

**Green Neuron AI · 12 November 2026**

## Completed checks

- The selected benchmark evidence contains 36 TurboHap measurements and
  360 passing raw output-file checks, plus 14 GNU FindHap measurements with
  140 passing file checks (126 raw matches and 14 documented
  crossover-order exceptions). Individual observations and
  statuses are included in the [aggregate evidence](turbohap_benchmarks_111126.json).
- An independent review matched the nine dataset identities to completed
  generation records and checked all table entries and numerical claims.
  Inputs, simulation truth, biological checksum manifests, application logs and
  generation code are not included in the distribution.
- The source projection excludes unpromoted experimental trees, arbitrary
  compiler-feature injection, rejected patches and private profiling tools.
  `PUBLIC_PACKAGE_FILES.json` records each reviewed distribution role.
- Rebuilding the projected source on Linux ARM64 using the pinned GCC 13.3
  environment, with modern STDOUT selected, produced an inference executable
  byte-for-byte identical to the one used for the 36 timed observations.
- The ordinary source build defaults to legacy STDOUT. That separate build
  passed all **57 native regression tests, with no skips**. These include
  standard/LM exact-output fixtures, validation, CPU policies, lookup/ISA paths,
  prior ordering and output-error handling.
- Both console-default builds passed checks of implicit default, explicit legacy
  and explicit modern modes: **60 raw fixture-file checks per build**, plus
  capacity-failure diagnostics. Result compatibility is not inferred merely from
  an unchanged source-code description.
- Reporting checks run separately under normal Python and `python -O`.
  Deliberately stale headlines, failed/missing/duplicate measurements,
  undocumented output differences, symlinks and an allowlisted experiment are
  rejected. The final release checks also require the published version label
  and all eight GNU FindHap controls.

The measured modern executable SHA-256 is
`16f8f2702f3c45ec91d5c180ba067b07e44ecb6e0e2f59bd06733d7d4bd9a7a9`.
The legacy-default executable is a different build; it is not described as
binary-identical to the modern build. Build/source identities are recorded in
[the qualification metadata](build_qualification_111126.json).

## Scope

The benchmark matrix is complete, including the verified and retained single
GNU FindHap observation for **250k × 80k**. No GNU run is planned for **1M × 80k**.

The optional M4 Pro profile uses the previously qualified Apple Clang/libomp
recipe. The final source package was rebuilt and tested on Linux ARM64. The
macOS evidence comes from the earlier native qualification of the same build
recipe. See the [Apple Silicon installation guide](../MACOS.md).

The isolated extracted source archive also built successfully. Its legacy
executable matched the qualified legacy build byte for byte, with 40 example
raw-file checks, 60 console-mode checks and 20 full-input audit fixtures passing.
The installed package passed CLI smoke checks and checks of all 68 internal
documentation links and 27 shell snippets. Timed
benchmarks are not rerun merely to regenerate documents. Repetition ranges
remain technical-run ranges, not confidence intervals or proof of universal
performance.
