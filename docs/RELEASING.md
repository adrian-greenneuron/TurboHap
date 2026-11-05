# Preparing a public release

Freeze the scientific source, required build inputs and selected observations
before packaging. The current v1.4.0 five-dataset comparison uses two TurboHap
observations per mode/dataset from 5 November 2026 and reuses the five unchanged
GNU Fortran observations from 30 October 2026. The manuscript and supplementary
material remain a v1.0.0 record. Current performance comparisons include
only datasets with at least 10,000 loci; both 1,000-locus datasets are excluded
from current JSON/CSV observations, validation summaries, tables, figures and
release claims. Historical release records and the manuscript remain intact.

Use Python 3.11 or newer. Install the versions in
`tools/requirements-reporting.txt` in an isolated environment. These are reporting
dependencies; they are not needed to build or run TurboHap.

```sh
python3 tools/render_benchmarks.py
```

This regenerates the README headline and performance summary, both benchmark
tables, CSV export, benchmark report, six PNG/SVG figures, release claims JSON
and release notes from `docs/turbohap_benchmarks_051126.json` and
`docs/turbohap_validation_051126.json`. Every current numerical claim must use
this same selected evidence. Inspect the plots, including the full memory-change
colour range, and compare the claims with the selected observations. Keep
historical release reports and the frozen manuscript intact.

Version metadata in CMake, CITATION.cff, README and the changelog must agree.
Review all installation and usage documents, including MACOS.md. The public
platform statement is that TurboHap works on Apple Silicon Macs, Linux ARM64,
and Intel and AMD x86-64 systems; detailed benchmarking is ongoing. Do not add
unpublished platform benchmark details, private data, development paths or
private audit links to the public package.

Build the packaged source in a clean directory, then run its regression suite
and small example. Record the actual completed build and test count in
`release_build_validation` in the benchmark JSON. Retained long-run benchmark
evidence is applicable only when the scientific implementation and required
build/runtime inputs are identical. If removing inactive development code,
rebuild with the pinned environment and require the inference and auditor
executables to match the qualified binaries byte for byte. Record that proof
separately from the original source identity; do not relabel the measured source.
Changed scientific code requires the relevant regression and benchmark checks.
The package checker rejects incomplete build/test metadata. Regenerate the
presentation after updating this metadata.

Review every distributed file and its role in `PUBLIC_PACKAGE_FILES.json`.
Only production source, build support, regression tests, synthetic examples,
user documentation, benchmark evidence, frozen manuscript material and release
tools belong in this package. Disabled or rejected experiments, patches,
experimental build hooks and development-only benchmarks must remain outside
the public tree. A file being committed or disabled does not qualify it for release.

Stage the intended public files explicitly, then run:

```sh
python3 tools/check_release.py --root . --base turbohap_benchmarks_051126 --write-manifest
python3 tools/check_release.py --root . --base turbohap_benchmarks_051126
```

The checker enforces the reviewed per-file distribution allowlist, rejects
untracked additions and unpromoted source/build hooks, and verifies every
scientific source and CMake-module hash, complete
benchmark-to-validation coverage, actual packaged build/test qualification,
version metadata, relative document/image links, complete generated presentation
and SHA256SUMS. A stale headline causes a failure even if the benchmark tables
are correct. A negative test should deliberately alter the headline in an
isolated copy and confirm rejection. Also confirm that an added experimental
folder, patch or disabled build hook is rejected, even if allowlisted. The manifest covers the Git-tracked
package, excluding itself, so stage every intended new file first. It contains
no commit hash, avoiding a self-referential hash dependency.

All 20 selected C++ observations must pass their ten raw output checks: 200
checks in total. Keep the Fortran exception explicit: 45 of 50 raw file hashes
match, and only `cross.overs` record order differs; its header and complete
records match after C-locale sorting with duplicates retained. No other output
difference is accepted.

Create the ZIP from the final committed tree, extract it to an empty directory
and run `python3 tools/check_release.py --root . --base turbohap_benchmarks_051126` there. Also build and run the small
example from the unpacked source archive. Keep fresh test output outside the
unpacked source tree so it cannot contaminate the manifest file selection.
The package checker verifies recorded evidence; it does not itself rerun C++
tests or benchmarks.

Use `docs/turbohap_release_notes_051126.md` as the GitHub release body. Verify the
public default branch and tag commit, release notes, assets and anonymous
source/archive downloads after publication. Compare the complete downloaded
file set and checksums. Keep the publication audit in the development repository.
An authorised replacement of an existing version deletes and recreates that
release and tag only after the complete candidate passes; other releases remain
intact.

The standalone input auditor is part of the package. Exercise report destinations, all-file coverage, bounded counts and no-write behaviour with the packaged `standalone_validation` and `full_input_audit` tests. Record executed and skipped regression tests separately. The separate report, private cohorts and development-report links are outside the public release allowlist.
