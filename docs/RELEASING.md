# Preparing a public release

Freeze the scientific source, required build inputs and selected observations
before packaging. The current v1.3.0 seven-dataset comparison uses two TurboHap
observations per mode/dataset from 1 November 2026 and reuses the seven unchanged
GNU Fortran observations from 30 October 2026. The manuscript and supplementary
material remain a v1.0.0 record.

Use Python 3.11 or newer. Install the versions in
`tools/requirements-reporting.txt` in an isolated environment. These are reporting
dependencies; they are not needed to build or run TurboHap.

```sh
python3 tools/render_benchmarks.py
```

This regenerates the README headline and performance summary, both benchmark
tables, CSV export, benchmark report, six PNG/SVG figures, release claims JSON
and release notes from `docs/turbohap_benchmarks_011126.json` and
`docs/turbohap_validation_011126.json`. Every current numerical claim must use
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
build/runtime inputs are identical; document metadata-only packaging changes.
Changed scientific code requires the relevant regression and benchmark checks.
The package checker rejects incomplete build/test metadata. Regenerate the
presentation after updating this metadata.

Stage the intended public files explicitly, then run:

```sh
python3 tools/check_release.py --write-manifest
python3 tools/check_release.py
```

The checker verifies every scientific source and CMake-module hash, complete
benchmark-to-validation coverage, actual packaged build/test qualification,
version metadata, relative document/image links, complete generated presentation
and SHA256SUMS. A stale headline causes a failure even if the benchmark tables
are correct. A negative test should deliberately alter the headline in an
isolated copy and confirm rejection. The manifest covers the Git-tracked
package, excluding itself, so stage every intended new file first. It contains
no commit hash, avoiding a self-referential hash dependency.

All 28 selected C++ observations must pass their ten raw output checks: 280
checks in total. Keep the Fortran exception explicit: 63 of 70 raw file hashes
match, and only `cross.overs` record order differs; its header and complete
records match after C-locale sorting with duplicates retained. No other output
difference is accepted.

Create the ZIP from the final committed tree, extract it to an empty directory
and run `python3 tools/check_release.py` there. Also build and run the small
example from the unpacked source archive. Keep fresh test output outside the
unpacked source tree so it cannot contaminate the manifest file selection.
The package checker verifies recorded evidence; it does not itself rerun C++
tests or benchmarks.

Use `docs/turbohap_release_notes_011126.md` as the GitHub release body. Verify the
public default branch and tag commit, release notes, assets and anonymous
source/archive downloads after publication. Compare the complete downloaded
file set and checksums. Keep the publication audit in the development repository.
An authorised replacement of an existing version deletes and recreates that
release and tag only after the complete candidate passes; other releases remain
intact.
