# Output compatibility

TurboHap’s default genome-wide analysis uses FindHap v3’s scientific settings, including its X-dependent progeny scoring. Use this default to reproduce an existing FindHap analysis. Standard and LM accept the same inputs and produce the same enabled result files for a given analysis.

| Analysis requested | Effective X-progeny scoring | Results to compare |
| --- | --- | --- |
| No scientific option, or `--processing-scope=legacy` | Legacy | One genome-wide result set, comparable with FindHap v3. |
| Genome-wide with `--x-progeny=corrected` | Corrected | One genome-wide result set. Calls for an inferred male and derived outputs can change when the marker map includes X-specific segments. |
| `--processing-scope=chromosome` | Corrected, including when legacy scoring was requested explicitly | One result set per chromosome, with its own reference eligibility and quality cutoffs. Compare a chromosome with a standalone run on that same map using corrected scoring. |

The [illustrated guide to X-progeny scoring](X_PROGENY.md) follows one cattle family and shows why corrected genome-wide scoring can change an autosomal call.

For an example of which animals qualify for matching and which appear in the genotype files, follow the same cow under both processing scopes. [Illustrated guide to chromosome scope](PROCESSING_SCOPE.md)

## What the release benchmarks checked

On all nine release datasets, v1.7.0 produced **360/360 exact file matches** against the same references used to check v1.6.0. Standard and LM each ran twice. Every completed file was checked in raw bytes by size and SHA-256. The previously published GNU FindHap controls provide 14 runs on eight of those datasets. Of their 140 file comparisons, **126 matched byte for byte**. In 14 `cross.overs` files the header and complete records matched, including duplicate records, after allowing for record order. Multithreaded GNU FindHap can write its crossover records in a different order. The million-animal dataset has a TurboHap reference only. [Measurements and checks](turbohap_benchmarks_221126.md)

All nine benchmark maps contain autosomes only. The chromosome and X choices have separate evidence. On a cohort with an X-specific map, the ten files for chromosome 1 and the ten files for X each matched the corresponding standalone corrected run in Standard and LM. These checks included the inferred calls, per-animal quality scores, and which animals passed or failed the output filters. Together these comparisons gave **40/40 exact file matches**.

For v1.6.0 we also checked the haplotype lookup for reconstructed ancestors against the preceding implementation. The checks covered an ordinary pedigree and a case where the algorithm accepted a match with a nonzero conflict. All ten files matched in each case in both Standard and LM (**40/40**).

## Comparing your results

Use the same input files, options, processing scope and X-progeny setting in fresh run directories. A result file that receives no records under the original genome-wide options can retain its previous contents. Check completion before comparing files and include only the files your options enable. With `hapout=2`, the original empty-write behaviour is retained.

To verify exact matches, compare the raw byte length and SHA-256 of each enabled output against its trusted reference. The record-order allowance above applies specifically to GNU FindHap’s `cross.overs` control. Console timestamps, CPU placement messages and elapsed times are diagnostics. Choosing `--stdout=modern` changes their presentation and leaves the scientific choices intact. [Options and output files](USAGE.md)

## The executable used for the benchmarks

A clean build from the release source on the DGX Spark with GCC 13.3.0 reproduced the executable used for all 36 TurboHap measurements. It was 2,196,328 bytes with SHA-256 `938fb1cb5c76c8d8213f6f44ac9f90f3b24d490d6abb912bf6f9bf600bfaf8da`. This identifies the benchmark executable. For a build on your own computer, use the example below to check the results.

## Installation checks

From the extracted source folder, run:

```sh
python3 examples/check_installation.py --binary build/findhap
```

Use `build-macos/findhap` for the manual Mac build. The checker runs the small USDA example in Standard and LM at one, two and four threads. It compares each output’s size and SHA-256 with the original Fortran reference. All six runs can be checked using just Python, the compiled program and the supplied example. [Worked example](../examples/README.md)
