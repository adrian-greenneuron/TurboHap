# TurboHap v1.6.0 release notes

## Changes since v1.5.0

<!-- VERSION_COMPARISON_START -->
- Across [nine matched datasets](turbohap_release_comparison_201126.md), v1.6.0 delivers a **1.36× speedup with 13% lower peak memory** in Standard and a **1.48× speedup with 15% lower peak memory** in LM compared with v1.5.0. <!-- VERSION_COMPARISON_END -->
- **Choose how to use parental haplotypes when X is on the map.** FindHap v3 can use only the dam’s haplotypes to score an ungenotyped male reconstructed from his offspring, even on autosomes. With `--x-progeny=corrected`, both parents can contribute evidence on autosomes and diploid pseudoautosomal segments. The maternal rule still applies on X-specific segments. The default genome-wide run keeps FindHap v3 scoring. [Illustrated guide to the correction](X_PROGENY.md).
- **Get a separate answer for each chromosome.** Add `--processing-scope=chromosome` to assess reference eligibility and output quality within each chromosome. An animal can appear in the results for one chromosome and fall below the quality cutoff on another. Both Standard and LM write a directory per chromosome and use corrected X-progeny scoring automatically. [Illustrated example](PROCESSING_SCOPE.md) · [Using priors](USAGE.md#processing-each-chromosome-independently)
- **Get an optimised build for your processor.** On Linux servers and Apple M-series Macs, a native build selects settings for the processor automatically. The [illustrated processor guide](PROCESSORS.md) lists the optimised generations and shows how to check your installation.
- **Catch ambiguous or incomplete inputs earlier.** The engine reads complete pedigree records, including long trailing metadata. It reports both line numbers for duplicate genotype records assigned to a pedigree animal. The engine and the input auditor require `errate` to be a finite fraction from 0 to 1. An unrecognised long command-line option now stops the run with a diagnostic.
- **Run larger analyses more reliably.** The program checks workspace sizes before opening result files and uses wider index and worker offsets. It sizes the haplotype library for the largest actual segment stage. This can reduce reserved address space when the longest segments and the largest number of segments occur at different stages. Peak resident memory describes a different quantity. Worker failures now reach the main process with their cause. An output failure can still leave partial files.
- **Trace a run.** The optional modern log shows the actual maximum segment length at each stage, alongside the existing UTC timestamps and peak live-haplotype summary.

## Changes to matching, storage and writing

When pedigree matching revisits a reconstructed ancestor, qualifying builds can create a candidate index once and reuse it for that segment. TurboHap checks candidates in the original order and applies the original acceptance and tie rules. The ordered search also uses corrected array-access assumptions across compiler optimisations.

Other changes reduce the time spent outside matching. Depending on processor and mode, TurboHap restores observed calls directly through the chip map, uses compact inheritance and assignment storage, avoids scratch writes after LM’s last population pass and batches LM disk writes. Some native builds use bounded buffers to overlap genotype, crossover or assignment output with computation. On the DGX Spark, workers can receive different amounts of pedigree work to suit the two core types. These changes preserve the expected result format and order for the same scientific settings.

## Benchmark evidence

The same nine simulated cattle datasets were used for the v1.5.0 and v1.6.0 comparisons. Both TurboHap modes ran twice per dataset with 20 threads on the NVIDIA DGX Spark. The FindHap comparisons reuse the GNU Fortran 13.3 controls published with v1.5.0.

<!-- BENCHMARK_SUMMARY_START -->
The two TurboHap modes completed **36 timed runs** on nine datasets and passed **360 byte-for-byte output checks**. Using the 14 GNU FindHap controls published with v1.5.0, the largest measured speedup was **204.7×** and the largest peak memory reduction was **93.3%**. The million-animal dataset has TurboHap results only.
<!-- BENCHMARK_SUMMARY_END -->

[Measurements and figures](turbohap_benchmarks_201126.md) · [Comparison with v1.5.0](turbohap_release_comparison_201126.md) · [Performance across seven releases](turbohap_release_history_201126.md) · [Dataset methods and download](SYNTHETIC_DATASETS.md)

The [v1.6.0 manuscript and supplementary material](paper/README.md) explain the implementation and present the nine-dataset comparison.

## Upgrading

Existing input files, `findhap.options` values and run commands select the genome-wide FindHap v3 behaviour in both modes. Rebuild in a fresh directory for the CPU that will run the analysis. Use a fresh working directory when comparing results across versions or scoring choices. [Install](INSTALL.md) · [Input checks](INPUT_VALIDATION.md) · [Output compatibility](COMPATIBILITY.md)
