# Output compatibility

TurboHap implements the FindHap v3 input formats, inference method, options and
ten legacy output filenames. Standard and LM modes must match their qualified
reference outputs byte for byte, checked using raw file sizes and SHA-256.
No sorting, rounding or whitespace normalization is used for TurboHap checks.

The nine v1.5.0 benchmark datasets supplied 36 successful TurboHap measurements
and 360 raw output-file checks. Every check passed. The completed GNU FindHap
controls use identical data, options and thread counts. There is one established
Fortran exception: multithreaded `cross.overs` output can contain the same
records in a different order. Its header and all complete records are checked,
with duplicates retained. That comparison does not claim the unsorted file is
byte-identical. No other difference is accepted.

All eight planned FindHap control datasets are complete. The 1M × 80k case
has no FindHap control. Its TurboHap runs were checked against retained
qualified TurboHap references; a direct GNU comparison is not implied.
See the [benchmark report](turbohap_benchmarks_111126.md) for individual
verification statuses, counts and methods.

## What is preserved

- Existing filenames, positional options, genotype encodings and enabled outputs.
- Standard/LM inference decisions and result-file ordering for qualified inputs.
- Exact numerical decisions in native vector paths and their fallbacks.
- The original capacity-failure behaviour when a segment exceeds maxhap.

Strict validation rejects malformed or ambiguous layouts instead of silently
accepting shifted calls. Diagnostic `--input-validation=warn` retains documented
safe legacy interpretations; reproducing unsafe reads or crashes is not a
compatibility requirement. The [standalone validator](INPUT_VALIDATION.md)
does not alter inputs or inference outputs.

Console progress, timestamps and elapsed-time diagnostics are separate from
result-file compatibility. Release builds default to `--stdout=legacy` for
FindHap-style console output. `--stdout=modern` adds clearer progress and
capacity summaries. No exact identity is claimed for elapsed timings or console
formatting.

The positional thread argument retains the original two-character
interpretation: `100` selects 10 and `010` selects 1. Use a positive one- or
two-digit count. Recognized launcher flags may precede or follow it.
Explicit OpenMP/CPU restrictions take precedence over automatic placement.

Use a fresh output directory for comparisons. Legacy file-opening semantics can
leave an existing output untouched when the selected options write no records.
Only compare outputs enabled by the options. `hapout=2` preserves the actual
legacy behaviour; it does not produce the all-animal output advertised by one
original options comment.

## Installation checks

```sh
(cd build && ctest --output-on-failure --parallel 1)
python3 tests/check_example.py --binary build/findhap
```

Use `build-macos` for the Mac recipe. The example checker runs both modes at
one and four threads and checks the saved original Fortran reference identities;
it needs neither Fortran nor the benchmark datasets. The broader suite covers
parsing, priors, output order, CPU policies, lookup widths, allocation and vector
paths. Platform-dependent skips are reported, not counted as successful tests.
Passing qualification is evidence for the tested configurations, not a proof for
every possible dataset or compiler. See [release qualification](RELEASE_CHECKS.md).
