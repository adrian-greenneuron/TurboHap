# Credits and references

TurboHap is maintained by Adrian Carr at Green Neuron AI.

The original FindHap algorithm and Fortran program were written by
Paul M. VanRaden at the USDA Agricultural Research Service, Animal Improvement
Programs Laboratory. TurboHap reimplements the FindHap v3 algorithm in C++17.
Original attribution and the public-domain notice are retained in
[LICENSE](../LICENSE), alongside the separate terms for TurboHap's additions.

For the scientific method, please cite:

- VanRaden PM, O'Connell JR, Wiggans GR, Weigel KA (2011).
  **Genomic evaluations with many more genotypes.** Genetics Selection Evolution
  43:10. [doi:10.1186/1297-9686-43-10](https://doi.org/10.1186/1297-9686-43-10).
- VanRaden PM et al. (2013). **Genomic imputation and evaluation using
  high-density Holstein genotypes.** Journal of Dairy Science 96:668–678.
  [doi:10.3168/jds.2012-5702](https://doi.org/10.3168/jds.2012-5702).

The nine synthetic cattle datasets used for v1.5.0 and v1.6.0 are archived separately. The simulation used public cattle chip maps, historical ancestry generated with msprime, tskit for access to ancestral haplotypes and the AlphaSimR meiosis model. The [dataset methods](SYNTHETIC_DATASETS.md) cite the source maps and software and explain the role of each component. [Benchmark dataset DOI](https://doi.org/10.5281/zenodo.24144449)

The [original manuscript and supplement](https://github.com/adrian-greenneuron/TurboHap/tree/v1.5.0/docs/paper) use an earlier simulation and benchmark collection. The much smaller installation example comes from the original FindHap v3 distribution and is credited in [its README](../examples/README.md).

For software citation metadata, see [CITATION.cff](../CITATION.cff).
