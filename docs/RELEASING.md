# Preparing a public release

The scientific source and selected observations are frozen before packaging.
The current seven-dataset comparison uses one GNU Fortran observation and two
observations per TurboHap mode/dataset. The manuscript remains a v1.0.0 record.

Install the figure-generation dependencies from `tools/requirements-reporting.txt`
in an isolated environment. These are reporting dependencies; they are not needed
to build or run TurboHap.

```sh
python3 tools/render_benchmarks.py
```

This regenerates the README headline and performance summary, both benchmark
tables, CSV export, benchmark report, six PNG/SVG figures, release claims JSON
and release notes from `docs/turbohap_benchmarks_301026.json`. Inspect the plots
and compare the claims with the selected observations. Version metadata in
CMake, CITATION.cff, README and the changelog must agree.

Stage the intended public files explicitly, then run:

```sh
python3 tools/check_release.py --write-manifest
python3 tools/check_release.py
```

The check verifies frozen scientific source hashes, validation counts, version
metadata, relative document/image links, complete generated presentation and
SHA256SUMS. A stale headline causes a failure even if the benchmark tables are
correct. The manifest covers the Git-tracked package, excluding itself. It
contains no commit hash, avoiding a self-referential hash dependency.

Create the ZIP from the final committed tree, extract it to an empty directory
and run `python3 tools/check_release.py` there. Reuse the applicable recorded
19-test and 28-run evidence only while the scientific source and its required
build/runtime inputs remain unchanged; this package check is not a fresh C++
build or benchmark. Changed code needs the relevant regression checks.

Use `docs/turbohap_release_notes_301026.md` as the GitHub release body. Verify the
public default branch and tag commit, release notes, assets and anonymous
source/archive downloads after publication. Compare the complete downloaded
file set and checksums. Keep the publication audit in the development repository.
An authorised replacement of an existing version deletes and recreates that
release and tag after the full candidate passes; other releases remain intact.
