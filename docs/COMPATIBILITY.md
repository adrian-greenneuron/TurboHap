# Output compatibility

TurboHap implements the FindHap v3 algorithm with the original input formats,
options and output filenames. TurboHap and TurboHap LM use the same inference
decisions and produce identical result-file bytes for the qualified inputs.

## What the comparisons establish

The reference is the original FindHap v3 Fortran source adapted to build with
gfortran on ARM64. Original qualification used GCC/gfortran 13.3 with
`-O3 -fopenmp -fno-underscoring`. The current performance tables retain the
expanded-flag GNU Fortran 13.3 observations published with v1.2.0; their complete
flags and method are recorded in the [current benchmark report](turbohap_benchmarks_011126.md).
Reference result-file identities are retained across releases. Qualification
uses the C locale and matching inputs, options and thread counts.

For the synthetic benchmark datasets, every result file must match byte for
byte except `cross.overs`. Multithreaded Fortran can emit the same crossover
records in a different order. That file is checked by sorting complete records
and comparing every byte, retaining duplicate records. No values are rounded,
records dropped or genetic calls normalised. This is an ordering exception,
not a strict byte-for-byte comparison of the unsorted crossover file.

The two C++ modes are compared strictly, including crossover order. Original
v1.0.0 qualification also checked 17 example configurations per mode against the
Fortran reference, covering thread counts, priors, incremental animals, segment
lengths, output selectors, fixed-width blank fields and capacities on both sides
of 65,535 haplotypes. Those checks are broader than the bundled installation
check: the included checker runs one original example in both modes at one and
four threads (four runs), comparing every output against saved Fortran hashes.
The complete qualification matrix is not included in this source release.

For v1.1.0, all 28 timed runs across the seven synthetic datasets and both modes
matched the retained C++ reference files exactly: 280 SHA-256 and byte-count
checks, including raw crossover order. The [validation record](turbohap_validation_281026.json)
contains the observed and expected hashes. Additional option/mode comparisons
at one, four and ten threads passed against the qualified serial control.
These checks supplement the original qualification; they do not claim that
every possible input has been tested.

For v1.2.0, all 28 fresh Spark benchmark runs also passed **280 strict raw
SHA-256 and byte-count checks**, including crossover order. See the
[v1.2.0 validation record](turbohap_validation_301026.json). The release layout
passed all 19 packaged tests before timing, including output-error handling
and CPU-policy checks. The historical Fortran observations remain unchanged.

For v1.3.0, the seven synthetic datasets were checked in standard and LM modes
with two observations per mode and dataset: **28 runs and 280 strict raw-file
checks**, including crossover order. See the
[v1.3.0 validation record](turbohap_validation_011126.json). These observations
use the normal Linux platform defaults; the separate optional allocation-policy
experiments are not included in the public benchmark selection. The packaged
regression suite covers the integrated storage, scheduling, launcher and
output-error paths. Native Mac installation checks also validate the integrated
source with its selected compiler and OpenMP runtime.

Console progress, diagnostics and elapsed-time text are not result-file
compatibility targets. Use an explicit thread count when comparing programs;
Apple Silicon's automatic default can differ from the OpenMP runtime maximum.
The included example is a reproducible installation check, not a universal
proof for every possible dataset, compiler or option combination.

## Existing workflows

Keep `findhap.options`, data files and optional priors unchanged. The executable
name is still `findhap`. The positional thread argument keeps the original
two-character interpretation; `100` selects 10 and `010` selects 1.
`--low-memory`, CPU-policy and row-schedule options are consumed by the launcher
and may appear before or after that argument. Other arguments after the thread
count retain their original ignored behaviour. The same result-file contract
applies to the available row schedules and allocation defaults.

The original file-opening behaviour is retained: an existing output file can
remain untouched when the selected options cause no records to be written.
Use clean run directories for comparisons, and compare only outputs enabled
by the supplied options. `hapout=2` retains the original implementation's
behaviour; it does not write the all-animal haplotypes advertised by the original
options-file comment.

## Validation commands

```sh
cd build
ctest --output-on-failure --parallel 1
cd ..
```

Use `build-macos` in place of `build` on macOS. The full Linux suite requires
Python 3.11 or newer; Python 3 is sufficient for the example check. Tests that
require another operating system are reported separately as skipped.

To run only the example check, from the source directory:

```sh
python3 tests/check_example.py --binary build/findhap
```

The example checker uses saved Fortran result hashes, checks the exact set of
output files, and runs both modes with one and four threads. It never runs
Fortran and does not require a Fortran compiler.
