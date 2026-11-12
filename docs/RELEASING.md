# Preparing and checking a TurboHap release

Prepare the complete distribution from a reviewed source tree. Publish to the
public TurboHap repository only after authorization.

## Evidence and presentation

1. Freeze the selected source, compiler/runtime, CPU policies and benchmark
   observations. Reuse only qualified measurements; preserve their original
   repeat counts, units and verification statuses.
2. Update `docs/turbohap_benchmarks_111126.json` with approved aggregates only.
   Do not copy input genotypes, biological hashes, application logs or private
   development paths into the distribution. The individual observation CSV is
   generated from that JSON.
3. Run `python3 tools/render_benchmarks.py`. It regenerates the headline,
   tables, figures, benchmark report, release-note summary and numerical claims
   from the same observations. Review the changes visually as well as numerically.
4. The v1.5.0 release includes 50 qualified observations. Preserve their
   individual measurements and verification statuses. GNU FindHap controls are
   available for eight datasets. The 1M × 80k dataset measures TurboHap alone.

## Source package and installation

- Every file must have a reviewed distribution role in
  `PUBLIC_PACKAGE_FILES.json`. Experimental trees, rejected patches, disabled
  experimental build hooks, benchmark payloads and private configuration do not
  belong in the distribution. A matching checksum alone is not approval to ship.
- Keep legacy STDOUT as the normal source-build default. Check both ordinary
  `findhap` and `--stdout=modern`; diagnostic changes must not change result bytes.
- Rebuild the exported source under the qualified environment. A modern-mode
  build is compared with the measured executable; the ordinary legacy-mode
  build is tested separately. Record the exact relationship rather than calling
  changed executable bytes identical.
- Exercise the extracted archive's build, example, test suite and installed
  layout. Record platform-specific skips explicitly. Linux checks do not prove a
  fresh native macOS build; retain applicable Mac evidence and state its scope.
- Preserve historical manuscript downloads and verify their existing hashes.
  Historical supplementary code is labelled as such; it is not the new dataset
  generator.

## Release checks

With Python 3.11+ and `tools/requirements-reporting.txt` installed:

```sh
python3 tools/render_benchmarks.py
python3 tests/test_release_validation.py
python3 tools/check_release.py --root . --release --write-manifest
python3 -O tools/check_release.py --root . --release
```

The checker enforces the file allowlist, version policy, source/build identities,
raw-file status rules, replicate plan, generated presentation, relative links
and complete checksums. The tests deliberately try bad evidence, a stale
headline, an allowlisted experiment and a symlink. A test pass is not a fresh
benchmark measurement.

Create the archive from the reviewed tracked file set. Extract it into an empty
folder and run the checker again. Compare its complete file set and bytes to
the proposed tree. Hash the final archive separately; `SHA256SUMS` describes
the files inside it, not the ZIP itself.

## Final publication

Confirm the release version and publication date in the changelog and citation
metadata. Run the release checks on the complete package with the same
documentation, release notes and archive that will be published.

Verify the remote commit and tag, release
notes and exact asset list. Download both the tag archive and the attached source
archive without authentication after public publication, then check their
contents and checksums. Retain a dated audit. Preserve other releases and the
frozen manuscript. Do not silently retag an existing public release.
