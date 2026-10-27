# TurboHap manuscript and supplementary material

**Adrian R. Carr (2026). _TurboHap: accelerating FindHap haplotype phasing and
genotype imputation while preserving output compatibility._**

Author manuscript, 22 October 2026; not peer reviewed. These are the exact
manuscript and supplementary files submitted on that date. They describe
TurboHap v1.0.0 and the seven-dataset DGX Spark benchmark study.

| File | Contents |
| --- | --- |
| [Manuscript (PDF)](turbohap_manuscript_221026.pdf) | Main article, four figures and two tables |
| [Supplementary File 1 (PDF)](turbohap_supplementary_material_221026.pdf) | Methods, all 42 benchmark observations, haplotype-library summaries and simulation recipe |
| [Supplementary File 2 (ZIP)](turbohap_supplementary_archive_221026.zip?raw=true) | Supplementary PDF and Markdown, machine-readable measurements, input/truth and source/build manifests, output-validation records, exact Fortran reference sources, reproduction tools and figures |
| [SHA256SUMS](SHA256SUMS) | SHA-256 checksums for the three downloads above |

After downloading the three files and `SHA256SUMS` to the same directory,
verify their identities with:

```sh
sha256sum -c SHA256SUMS
```

Extract Supplementary File 2 and run `python3 tools/recalculate_benchmarks.py`
from its top-level directory to verify its internal checksums and recalculate
the published summaries. This checks the supplied observations; instructions
for new inference runs are in Section S2 of Supplementary File 1.

## Synthetic datasets and simulation truth

All **seven synthetic input datasets and their simulation truth** are publicly
archived on Zenodo:

**[TurboHap synthetic benchmark datasets and simulation truth —
doi:10.5281/zenodo.23566926](https://doi.org/10.5281/zenodo.23566926).**

The record contains one compressed archive per dataset, a README, reproduction
recipe, dataset manifest, archive inventory, SHA-256 checksums and a verification
script. These large input/truth archives are downloaded separately from Zenodo;
the supplementary ZIP above contains the compact study records and tools.

## Licences

The manuscript and figures are distributed under
[Creative Commons Attribution 4.0 International (CC BY 4.0)](https://creativecommons.org/licenses/by/4.0/).
The Zenodo datasets and simulation truth use CC BY 4.0 as well.
TurboHap software retains its [software licence](../../LICENSE), and the original
USDA Fortran reference retains its public-domain attribution.
