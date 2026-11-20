# Which processors is TurboHap optimised for?

Build TurboHap on the computer that will run your analyses. For the processor families below, a native build automatically selects the family optimisations your compiler supports. The same installation gives you both Standard and low-memory (LM) mode. [Linux installation](INSTALL.md#linux) · [Mac installation](../MACOS.md#install)

| Platform | Processor family | Optimised generations |
| --- | --- | --- |
| Linux, x86-64 | Intel Xeon | Cascade Lake (2nd generation), Ice Lake (3rd), Sapphire Rapids (4th), Emerald Rapids (5th) and Granite Rapids (Xeon 6 P-cores) |
| Linux, x86-64 | AMD EPYC | Milan (Zen 3), Genoa (Zen 4) and Turin (Zen 5) |
| Linux, Arm64 | Google Axion | Neoverse V2 (C4A) and Neoverse N3 (N4A) |
| Linux, Arm64 | NVIDIA DGX Spark | GB10 |
| macOS, Apple silicon | Apple M-series | M1, M2, M3, M4, M5 and M6, including base, Pro, Max and Ultra models where available |

The [v1.6.0 benchmark timings and memory measurements](turbohap_benchmarks_201126.md) were taken on the DGX Spark. Performance on your system will also depend on the dataset, thread count and available memory. SSD speed can be particularly important in LM, which reads and writes temporary files during analysis.

## What if my processor is unfamiliar?

On a supported operating system and architecture, you can use the usual installation instructions even if your processor is missing from the table. If no family profile matches, TurboHap builds with the general optimisations supported by your compiler and processor target. You can [check the installation with the small example](../examples/README.md) before committing to a large analysis. If you distribute one executable to several different processors, use the [portable build instructions](INSTALL.md#processor-support).

![A native build uses family optimisations when the processor matches and the compiler supports them. Otherwise it uses general optimisations for the compiler target. Either build can be inspected.](assets/processor_build_selection.svg)

## How can I see which settings my build uses?

On Linux, run this command from the source directory after building.

```sh
./build/turbohap-build-info --build-info
```

On a Mac, the supplied installer puts the same tool in your Applications folder.

```sh
~/Applications/TurboHap/bin/turbohap-build-info --build-info
```

| What you want to check | Fields in the build report |
| --- | --- |
| Which processor the build identified | `configure_cpu_model` and `profile_selection` |
| Which compiler and options it used | `compiler` and `compile_flags` |
| Which optimisations each mode uses | `engines.standard` and `engines.lm` |

If the processor model reads `unknown`, check `profile_selection` and the two engine entries to see what the build actually selected. Add `> turbohap-build.json` to either command to save the report. For the thread count TurboHap would use on the machine running the analysis, use `findhap --cpu-info` from the same installation.

## When should I email the author?

Email [Adrian Carr](mailto:adrian.carr@greenneuron.ai) if installation or the example check fails, if a native build uses general settings on a processor listed above, or if an analysis takes markedly longer or uses more memory than you expected.

Include your processor model, operating system and the build report if available. For an installation problem, include the command and error message. For a performance question, include Standard or LM, thread count, numbers of animals and loci, elapsed time and peak memory if known. Those details are enough to start without sharing any animal records.
