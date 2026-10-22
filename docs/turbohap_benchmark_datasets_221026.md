# Reproducing the synthetic benchmark datasets

22 October 2026. This recipe describes the seven corrected synthetic datasets
used in the [benchmark measurements](turbohap_benchmarks_221026.md).
The [input and truth manifest](turbohap_benchmark_datasets_221026.json) records
source hashes, dimensions, chip counts, chromosome lengths, the exact four
benchmark inputs and retained genotype/haplotype truth identities.

## Dataset sizes

The animal count in the tables is the number of **genotyped animals**. Each
pedigree contains four additional ungenotyped founder females. The marker
count is the number exported in `chromosome.data`; the simulator also creates
100 quantitative trait loci (QTLs), which are excluded from that marker map.

| Genotyped animals | Pedigree animals | Exported markers | Simulated loci including QTLs | FindHap `maxhap` |
| ---: | ---: | ---: | ---: | ---: |
| 10,000 | 10,004 | 1,000 | 1,100 | 50,000 |
| 10,000 | 10,004 | 10,000 | 10,100 | 50,000 |
| 10,000 | 10,004 | 100,000 | 100,100 | 50,000 |
| 100,000 | 100,004 | 1,000 | 1,100 | 50,000 |
| 100,000 | 100,004 | 10,000 | 10,100 | 50,000 |
| 100,000 | 100,004 | 100,000 | 100,100 | 50,000 |
| 1,000,000 | 1,000,004 | 10,000 | 10,100 | 500,000 |

Each dataset is simulated independently. Reusing the same seed does not make
the smaller datasets subsets of the larger ones. The simulation uses eight
initial ancestors and a simple pedigree with large families; the million-animal
case has a parent with 499,998 progeny. This structure is useful for measuring
scaling, but performance on other pedigrees can differ.

There are 30 chromosomes with simulated lengths; chromosome 30 has X-specific
handling. About 83.33% of animals have the full marker panel and 16.67% have a
panel containing every third marker. Marker seed 96, initial linkage parameter
0.20, missing-call probability 0.01, erroneous-call probability 0.0002 and
mutation probability 0.00000003 are used throughout.

## Build the simulator

Download and extract version 4 from the
[USDA GENOSIM page](https://www.ars.usda.gov/northeast-area/beltsville-md-barc/beltsville-agricultural-research-center/agil/aip/software/genosim/).
The benchmark generation used GCC/gfortran 13.3.0 on Linux ARM64. Compare the
source and option-file checksums with `simulator_sources` in the manifest;
the version name alone does not identify an exact source snapshot.

In the directory containing the simulator sources:

```sh
gfortran kind.f90 pedsim.f90 -o pedsim
gfortran kind.f90 markersim.f90 -o markersim -ffp-contract=off
gfortran kind.f90 genosim.f90 -o genosim -O3 -ffp-contract=off
```

The two marker-location calculations must use the same floating-point behaviour.
On the tested ARM64 build, contraction in the optimised genotype simulator
changed collision handling and some chromosome assignments relative to the
unoptimised marker simulator. Disabling contraction aligned every exported
marker location for the 1,000-, 10,000- and 100,000-marker panels. The corrected
pipeline retains the marker map and regenerates genotypes and truth against it.

These tools are needed only to generate benchmark data. They are not needed
to build or run TurboHap. The following example generates 10,000 animals ×
1,000 markers. Replace `/path/to/genosimV4` with your simulator directory.

```sh
mkdir simulated-10000-1000
cd simulated-10000-1000
cp /path/to/genosimV4/pedsim.options /path/to/genosimV4/markersim.options \
   /path/to/genosimV4/genosim.options /path/to/genosimV4/chips.txt .
```

Use a fresh directory for each dataset. The original simulation also writes
large truth and intermediate files, so allow disk space beyond the four final
FindHap inputs.

## Set the dimensions and FindHap options

Run this block in the simulation directory. Change `animals` and `loci` to
select another row above; retain the other simulator settings from the
identified option files.

```sh
python3 - <<'PY'
from pathlib import Path

animals, loci = 10000, 1000
pedigree = animals + 4

ped = Path('pedsim.options').read_text().splitlines(True)
ped[3] = f'{pedigree} {pedigree // 3} {pedigree // 3} 2 2 10\n'
Path('pedsim.options').write_text(''.join(ped))

markers = Path('markersim.options').read_text().splitlines(True)
markers[0] = f'{loci + 100} 100 1.12 0 10\n'
Path('markersim.options').write_text(''.join(markers))

maxhap = 500000 if animals == 1000000 else 50000
Path('findhap.options').write_text(
    f'4 30 100 50 3 {maxhap} 1 1 1 1\n'
    'iters XChrom maxlen minlen steps maxhap hapout genout damout listout\n\n.004\n')
PY
```

All three benchmark programs use this same `findhap.options` file, with all
output selectors set to 1 and no prior-haplotype files. The million-animal
case needs the larger haplotype capacity; it therefore changes both the animal
count and this capacity relative to the smaller cases.

## Generate and identify the inputs

Run the three original programs in order, stopping if any fails:

```sh
set -e
LC_ALL=C /path/to/genosimV4/pedsim > output.pedsim 2>&1
python3 /path/to/TurboHap/tools/prepare_simulated_parent_sexes.py .
LC_ALL=C /path/to/genosimV4/markersim > output.markersim 2>&1
LC_ALL=C /path/to/genosimV4/genosim > output.genosim 2>&1
mv genotypes.raw genotypes.txt
```

The included sex-preparation script runs after `pedsim` and before `genosim`.
For each parent, it sets sex to male for a sire role or female for a dam role,
rejects parents used in both roles, and updates the associated pedsim metadata.
It changes sex labels only; ancestry and animal identifiers are retained. It
refuses to run if genotypes or truth already exist. The original pedsim output
required 1,112, 11,112 and 111,112 changes for the three population sizes. The
corrected study regenerates truth after this preparation; it does not relabel
sex on an already simulated genotype dataset.

Keep `genotypes.true` and `haplotype.data`, optionally gzip-compressed, as the
simulation truth. The audited truth includes all pedigree animals; inferred
benchmark outputs must be kept separate from these simulator truth files.
Before timing, check exported marker locations against the simulator replay,
marker coverage, truth-phase/genotype consistency, parent roles and male
X-chromosome haploidy. The completed checks and truth identities are in the
manifest. Truth validation does not establish imputation accuracy.

Check that each log ends with its normal-completion message. `geno2seq` is
unnecessary because these tests use SNP-chip genotypes, not sequence reads;
no conflict-correction or phenotype-simulation step is applied to the inputs.

On Linux, obtain the four input fingerprints with:

```sh
sha256sum pedigree.file chromosome.data genotypes.txt findhap.options
```

Compare them with `inputs` for the selected dataset in the manifest. The
checksums describe the actual benchmark inputs, including the larger
`maxhap` options file for 1,000,000 × 10,000. Random-number implementations and
floating-point behaviour can vary by compiler and platform: matching settings
alone do not establish byte-identical regenerated inputs.

For timing, copy the four inputs into a separate clean run directory for each
program and repetition. Use ten threads, `LC_ALL=C` and `OMP_DYNAMIC=FALSE`,
and measure the complete executable, including all output writes. Compare
outputs after timing, following the [compatibility policy](https://github.com/adrian-greenneuron/TurboHap/blob/v1.0.0/docs/COMPATIBILITY.md).
For each dataset the corrected study ran Fortran, TurboHap, LM, LM, TurboHap,
Fortran, without warmups or forced cache eviction. All timings were collected
on 21–22 October 2026. The host also ran other tasks; competing load was not
systematically recorded or controlled. The [benchmark methodology](turbohap_benchmarks_221026.md#methodology) gives
the measurement and compiler details. Dataset files are planned for Zenodo under CC BY 4.0; this
recipe and the manifest do not claim that the raw-data deposit is already live.
