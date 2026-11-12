# Synthetic datasets for the TurboHap v1.5.0 benchmarks

The benchmarks use nine simulated dairy-cattle genotype datasets. The workloads
combine uneven marker spacing, two overlapping SNP panels, multigeneration
pedigrees, incomplete parent records and genotyping errors. These features
give phasing and imputation a mix of densely and sparsely observed segments,
related animals and uncertain calls. The release documents the methods and
aggregate results; it does not distribute the datasets, simulation truth or
generation code.

The exact inputs for all nine benchmarks are available on Zenodo. [Dataset DOI 10.5281/zenodo.24144449](https://doi.org/10.5281/zenodo.24144449)

Uneven marker loads change how work is divided across chromosomes. Family
structure affects which matches can be resolved from ancestors; unassayed
loci and no-calls reduce the evidence available for each match. Haplotype
diversity affects how many distinct patterns the software must store and search.

## Animals, chromosomes and marker panels

We simulated independent populations with 10,000, 25,000, 100,000,
250,000 and 1,000,000 **genotyped** animals. At the first four sizes, the
20,000- and 80,000-locus assays use the same animals, true genomes and
pedigree. The 20,000 markers are a subset of the 80,000 markers, but no-calls
and wrong calls were drawn separately for each assay. Only the 80,000-locus
assay is included at one million animals. The paired assays and repeated
program runs are not independent population draws.

The simulations use the 29 unequal autosomes of the
[ARS-UCD1.3 cattle assembly](https://www.ncbi.nlm.nih.gov/datasets/genome/GCF_002263795.2/).
The [BovineSNP50 v3](https://support.illumina.com/downloads/bovinesnp50-v3-0-product-files.html)
and [BovineHD](https://support.illumina.com/array/array_kits/bovinehd_dna_analysis_kit/downloads.html)
manifests provide spatial targets for chromosome-specific marker loads, local
density and panel overlap. Simulated variants retain their own positions.
Selection matches chromosome, 1 Mb window and source panel counts and uses
chromosome-wide minor allele frequency quotas informed by 20 Holsteins from
[Decker et al.'s public cattle dataset](https://doi.org/10.5061/dryad.th092).
Frequencies for selecting variants come from a separate set
of 100 simulated discovery animals. The simulated panels are derived from
commercial layouts; they are not copies of the commercial arrays.

| Assay union | Medium panel | High panel | Shared | Medium only | High only |
|---:|---:|---:|---:|---:|---:|
| 20,000 | 12,950 | 18,566 | 11,516 | 1,434 | 7,050 |
| 80,000 | 51,870 | 74,118 | 45,988 | 5,882 | 28,130 |

## Ancestry and pedigree

Historical ancestry was simulated with **msprime 1.4.5**, using the SMC′ approximation and the
[MacLeod et al. Holstein-Friesian population history](https://doi.org/10.1093/molbev/mst125).
The historical rates are 9.4 × 10⁻⁹ mutations and a mean of
10⁻⁸ recombinations per base per generation. We used **tskit 1.0.4** to
read the simulated ancestry and extract donor haplotypes. Recent founders
were built by copying mosaics from 8,000 simulated diploid donors. Empirical
genotypes and pedigrees were not used as donors.

Eight ordered cohorts follow the founders. Parents can be drawn from the
previous three cohorts, and skewed sire contributions produce unequal family
sizes. We used the meiosis model from **AlphaSimR 2.1.0** to transmit
chromosomes through the pedigree using separate
[Swiss Holstein male and female genetic maps](https://doi.org/10.1186/s12711-026-01041-0).
The maps total about 2,755 and 2,458 cM, respectively; the simulation holds
the genetic coordinate constant beyond the outermost mapped points.
Independently omitting 4.5% of known parent links creates incomplete records
without changing true inheritance. There are no deliberately misassigned
parents or additional recent marker mutations.

The complete simulated pedigree contains 1.8 times as many animals as the
genotyped count. Inference uses the recorded pedigree, which includes
genotyped animals and additional recorded ancestors. At the million-animal
scale it contains 1,154,396 animals. Among genotyped animals at every scale,
recorded pedigree depth has a median of two parent-child links, a 95th
percentile of five, and a maximum of eight.

## Missing genotypes and incorrect calls

The older half of genotyped animals receives the medium panel; the newer half
receives the high panel. Loci outside an animal's panel are unassayed. On the
assigned panel, each animal has a no-call propensity drawn from a mixture:
99% from Beta(1, 199), with mean 0.5%, and 1% from Beta(2, 18), with mean
10%. A mean-one lognormal locus multiplier (log-scale standard deviation
0.7) is normalised within the assigned panel, and resulting probabilities
are capped at one. No-calls are then sampled
independently conditional on these animal and locus propensities.

Residual errors among called genotypes use a symmetric adaptation of the
cross-platform discordance model in
[O’Connell et al. (2014), Table S2](https://doi.org/10.1371/journal.pgen.1004234.s036).
The probabilities are conditioned on a call, averaged under exchange of the
arbitrary A and B labels, and scaled to roughly 0.1% wrong calls overall.
This is a workload assumption based on a human array study, not a measured
bovine assay error rate. Rows are true genotypes; columns are reported calls.

| True genotype | Called AA (%) | Called AB (%) | Called BB (%) |
|---|---:|---:|---:|
| AA | 99.942726 | 0.042127 | 0.015147 |
| AB | 0.096982 | 99.806037 | 0.096982 |
| BB | 0.015147 | 0.042127 | 99.942726 |

The displayed values are rounded; the unrounded probabilities sum to 100%
within each row.

Across the nine filesets, 21.674–21.735% of all animal–locus combinations
are missing after expanding to the assay union. About 21.2% are missing
because the assigned panel does not assay the locus. Of the loci actually
assayed, 0.589–0.607% were no-calls. Of the reported genotypes,
0.1003–0.1007% were deliberately incorrect.

## Checks and scope

Inheritance checks compared simulated offspring alleles with the corresponding
parents. All nine exports passed independent reconstruction of genotype
records and observation masks, checks of the realised error transitions, and
the TurboHap input validator. These checks establish simulation and export
consistency. They do not establish that the model reproduces every property
of dairy populations.

The cohort and family model is simplified. For example, the recent pedigree
does not represent deeper recorded lineages, and pilot comparisons found
weaker linkage disequilibrium at distances below 20 kb than in a small public
Holstein sample. The benchmarks measure computation on these particular
workloads; they do not predict performance for every breed, array or pedigree,
or establish biological imputation accuracy.

## Simulation software references

- Baumdicker et al. (2022). Efficient ancestry and mutation simulation with
  msprime 1.0. *Genetics* 220(3), iyab229. [DOI](https://doi.org/10.1093/genetics/iyab229)
- Jeffery et al. (2026). Population-scale Ancestral Recombination Graphs with
  tskit 1.0. *arXiv* preprint 2602.09649. [DOI](https://doi.org/10.48550/arXiv.2602.09649)
- Gaynor, Gorjanc and Hickey (2021). AlphaSimR: an R package for breeding
  program simulations. *G3* 11(2), jkaa017. [DOI](https://doi.org/10.1093/g3journal/jkaa017)
