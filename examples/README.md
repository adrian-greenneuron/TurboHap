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

To run TurboHap LM, add `--low-memory`. With the default processing scope, each mode writes the ordinary FindHap output filenames. Use separate run directories when keeping both sets. To try the new per-chromosome analysis, repeat the example in a fresh directory with `../build/findhap 4 --processing-scope=chromosome`. Inspect the separate sets of results under `chromosome-results/chrN/`. The example’s saved FindHap checksums apply to the default genome-wide analysis.

Return to the source directory after the manual run:

```sh
cd ..
```

The automated check runs both modes independently, with one, two and four threads,
and verifies every output against the saved original-Fortran hashes in
`small/expected.json`:

```sh
python3 examples/check_installation.py --binary build/findhap
```

Expect six PASS lines and a summary confirming that all 60 outputs matched. To check an installed Linux copy, use `python3 "$HOME/.local/share/doc/turbohap/examples/check_installation.py" --binary "$HOME/.local/bin/findhap"` for an installation under `$HOME/.local`.

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
