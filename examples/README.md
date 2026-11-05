# Small example

`small/` contains the four input files supplied with the original USDA FindHap
v3 distribution: 325 pedigree animals and 5,000 loci. It is an installation
and compatibility example; it is not included in the performance tables.

From the source directory, copy the inputs into a separate run directory:

```sh
mkdir run-example
cp examples/small/findhap.options examples/small/chromosome.data \
   examples/small/pedigree.file examples/small/genotypes.txt run-example/
cd run-example
../build/findhap 4
```

To run TurboHap LM, add `--low-memory`. Each mode writes the ordinary FindHap
output filenames. Use another run directory when keeping both sets of results.

Return to the source directory after the manual run:

```sh
cd ..
```

The automated check runs both modes independently, with one and four threads,
and verifies every output against the saved original-Fortran hashes in
`small/expected.json`:

```sh
python3 tests/check_example.py --binary build/findhap
```

The original options-file comment describes `hapout=2` as writing all animals.
TurboHap preserves the original program's actual empty-write behaviour for
that setting; see the [options reference](../docs/USAGE.md#options). The bundled
input file is kept unchanged so its saved hash remains valid.

The four inputs are copied without modification from the public FindHap v3
distribution. Their SHA-256 hashes are recorded in `expected.json`. See the
[USDA FindHap catalogue](https://catalog.data.gov/dataset/findhap) and
[original software documentation](https://www.ars.usda.gov/northeast-area/beltsville-md-barc/beltsville-agricultural-research-center/agil/aip/software/findhap/).
The original distribution and example are credited to Paul M. VanRaden, USDA;
the catalogue identifies the FindHap resource as public under CC0.
