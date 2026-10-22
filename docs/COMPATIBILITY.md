# Output compatibility

TurboHap implements the FindHap v3 algorithm with the original input formats,
options and output filenames. TurboHap and TurboHap LM use the same inference
decisions and produce identical result-file bytes for the qualified inputs.

## What the comparisons establish

The reference is the original FindHap v3 Fortran source adapted to build with
gfortran on ARM64, compiled with GCC/gfortran 13.3 and the original release flags
`-O3 -fopenmp -fno-underscoring`. Qualification uses the C locale and matching
inputs, options and thread counts.

For the synthetic benchmark datasets, every result file must match byte for
byte except `cross.overs`. Multithreaded Fortran can emit the same crossover
records in a different order. That file is checked by sorting complete records
and comparing every byte, retaining duplicate records. No values are rounded,
records dropped or genetic calls normalised. This is an ordering exception,
not a strict byte-for-byte comparison of the unsorted crossover file.

The two C++ modes are compared strictly, including crossover order. Release
qualification also checked 17 example configurations per mode against the
Fortran reference, covering thread counts, priors, incremental animals, segment
lengths, output selectors, fixed-width blank fields and capacities on both sides
of 65,535 haplotypes. Those checks are broader than the bundled installation
check: the included checker runs one original example in both modes at one and
four threads (four runs), comparing every output against saved Fortran hashes.
The complete qualification matrix is not included in this source release.

Console progress and elapsed-time text are not result-file compatibility targets.
The optimised default uses the OpenMP thread limit; use an explicit thread count
when comparing programs. Results on other compilers, operating systems or inputs
need their own validation. The included example is a reproducible smoke check,
not a universal proof for every possible dataset or option combination.

## Existing workflows

Keep `findhap.options`, data files and optional priors unchanged. The executable
name is still `findhap`. The positional thread argument keeps the original
two-character interpretation; `100` selects 10 and `010` selects 1.
`--low-memory` is consumed by the launcher and may appear before or after that
argument. Other arguments after the thread count retain their original ignored
behaviour.

The original file-opening behaviour is retained: an existing output file can
remain untouched when the selected options cause no records to be written.
Use clean run directories for comparisons, and compare only outputs enabled
by the supplied options. `hapout=2` retains the original implementation's
behaviour; it does not write the all-animal haplotypes advertised by the original
options-file comment.

## Validation commands

```sh
cd build
ctest --output-on-failure
cd ..
```

To run only the example check, from the source directory:

```sh
python3 tests/check_example.py --binary build/findhap
```

The example checker uses saved Fortran result hashes, checks the exact set of
output files, and runs both modes with one and four threads. It never runs
Fortran and does not require a Fortran compiler.
