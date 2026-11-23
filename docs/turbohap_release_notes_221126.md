# TurboHap v1.7.0 release notes

## Faster population matching in Standard and LM

When phasing or imputing an animal, TurboHap compares its observed calls with haplotypes in the segment library. v1.7.0 can rule out candidates in groups of up to 512, using the instructions available on the processor. The surviving candidates go through the same acceptance and tie-breaking rules in the same order. The 512 is a count of candidate haplotypes, independent of the number of loci in a segment.

Both modes use the wider search. LM keeps its candidate index within a fixed memory allowance. On Apple silicon the release also streams result rows more efficiently. AMD EPYC Milan gains a faster candidate scan in both modes. Milan LM also counts expanded genotypes more efficiently and streams result output. [Processor support and build information](PROCESSORS.md)

## Benchmark results

Across the nine Spark benchmarks, v1.7.0 used **13.1% less time in Standard** and **11.5% less in LM** than v1.6.0, giving each dataset equal weight. The largest change in mean peak memory was 0.13%. On the 250,000-animal, 20,000-locus dataset, TurboHap ran **295.8× faster** than GNU FindHap.

All ten outputs matched their references in both modes on every dataset and repeat (**360/360**). [Measurements and figures](turbohap_benchmarks_221126.md) · [Comparison with v1.6.0](turbohap_release_comparison_221126.md) · [Performance across eight releases](turbohap_release_history_221126.md)

## Upgrading from v1.6.0

Build in a fresh directory and run the bundled installation check. Both versions accept the same commands, inputs and scientific options. The default genome-wide analysis retains FindHap v3 scoring. [Install](INSTALL.md) · [Check your results](COMPATIBILITY.md)

v1.6.0 introduced optional corrected X-progeny scoring and independent chromosome processing. Its manuscript and supplement report measurements made with **v1.6.0**. [v1.6.0 release notes](https://github.com/adrian-greenneuron/TurboHap/blob/v1.6.0/docs/turbohap_release_notes_201126.md) · [Manuscript and supplement](paper/README.md)
