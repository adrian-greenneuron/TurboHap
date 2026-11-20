# Modifications to TurboHap 1.5.0 (version 1.5.0-modified.2, unofficial)

This is **not an official TurboHap release**. It is TurboHap 1.5.0
(Copyright (c) 2026 Adrian Carr, Green Neuron AI) with the changes listed
below, made with the help of Claude (Anthropic) for a user who runs one
chromosome at a time (about 4,000 SNPs, about 2 million animals) on a
generic Linux cluster. The TurboHap licence still applies in full: free for
non-commercial academic research and teaching; commercial use requires a
paid licence from Green Neuron AI.

Every change keeps all output files byte-for-byte identical to unmodified
TurboHap 1.5.0 on the datasets tested (see *Validation*).

An earlier package of these changes, built on TurboHap 1.4.0, was also
labelled "1.5.0" (`1.5.0-unofficial`). It is not the official 1.5.0 and is
replaced by this one.

## Why the changes still matter in 1.5.0

Most of the new 1.5.0 speed and memory work is enabled only on the CPU
profiles the author has qualified (native builds for Intel Granite Rapids,
AMD Turin, NVIDIA GB10 Spark, Apple M4 Pro). A portable build, such as the
`x86-64-v3` container image, or a native build on any other CPU, keeps the
1.4.0 behaviour: the haplotype library is reserved for the worst case and
cleared at every step, and a one-chromosome run synchronises all threads
twice per animal in the pedigree phase.

## Changes

1. **Haplotype library sized per step** (`src/findhap.cpp`).
   1.5.0 reserves (largest segment length) x `maxhap` x (largest segment
   count). The modified build reserves the largest per-step product
   (segments x length) x `maxhap`. On a 4,000-SNP chromosome with
   600 / 75 / 3 steps this is 4,200 x `maxhap` bytes instead of
   32,400 x `maxhap`.

2. **Lazy library and demand-zero metadata on every Linux target**
   (`src/platform_policy.hpp`). The 1.5.0 paths `FINDHAP_HAPLIST_INIT=3`
   (fresh library mapping per step, rows initialized when created) and
   `FINDHAP_LIBRARY_METADATA` (frequency/order tables allocated demand-zero
   per step) are enabled on all Linux targets, not only on the qualified
   profiles.

3. **No up-front reservation check for the library**
   (`src/haplotype_initialization.hpp`). The library mapping uses
   `MAP_NORESERVE` (retried without it if refused), so the kernel's
   overcommit heuristic does not refuse a reservation that is mostly never
   touched. With changes 1 and 2 this lets whole-genome runs with a large
   `maxhap` start.

4. **Portable parts of the 1.5.0 workload bundle on every Linux target**
   (`src/platform_policy.hpp`): the segment-owned progeny stage
   (`FINDHAP_PROGENY_SEGMENT_OWNED`), the active library bound
   (`FINDHAP_LIBRARY_ACTIVE_BOUND`), early single-conflict rejection in the
   pedigree phase (`FINDHAP_PEDIGREE_EARLY_REJECT`) and the base-frequency
   loop (`FINDHAP_PEDIGREE_VECTOR_BASE`). These are upstream code; their
   CPU-specific kernels keep their own guards and use the scalar fallback
   on other CPUs.

5. **Segment-owned pedigree passes when chromosomes < threads**
   (`src/findhap.cpp`, `FINDHAP_PEDIGREE_SEGMENT_OWNERS`, default 1).
   1.5.0 runs one thread per chromosome when there are at least as many
   chromosomes as threads; otherwise (for example one chromosome per run)
   every thread processes its share of segments for each animal and the
   team synchronises twice per animal. The modified build adds a path for
   the non-final passes: each thread owns segments and walks all animals in
   order; the cross-segment orientation of an animal runs in animal order
   once every owner has finished it; progeny wait only for their parents;
   the parents' haplotype IDs read by orientation are snapshotted per
   segment; parent cursors are precomputed as in the chromosome pass. Males
   with unknown sire on X segments keep the original lockstep. The final
   pass (crossover output) keeps the 1.5.0 traversal.

6. **Overlapped output** (`src/findhap.cpp`). Summary lines
   (`hap.filled` / `do_not_distrib`) and `hap.inherit` lines are formatted
   by the worker threads, and two buffer sets let one thread write batch k
   while the others format batch k + 1. Records that do not fit their slot
   fall back to the original code.

7. **Builds with GCC 9 and 10** (`src/library_output.hpp`). Without
   floating-point `std::to_chars` (GCC < 11) the fast library-header
   formatter reports "not converted" and the existing `snprintf` path is
   used. This allows an Ubuntu 20.04 build, which runs on older Docker
   versions.

7a. **Whole-segment allele bitsets in the haplotype search**
   (`src/candidate_index.hpp`, `src/findhap.cpp`, `FINDHAP_MOD_WIDE`).
   1.5.0 has an exact bitset comparison only for segments up to 128 loci
   and only on its qualified CPUs; elsewhere each candidate that passes the
   64-locus sample is compared byte by byte with the library row (600 bytes
   in the first step). The modified build keeps, for every haplotype of the
   segment being searched, one bit per locus for "allele 0" and "allele 2",
   and for the query three bitsets (conflicts with 0, with 2, with unknown),
   which also cover the out-of-range values of a backsolved second
   haplotype. A candidate is then accepted or rejected with a few word
   operations, with the same conflict count and the same fill positions as
   the byte loops. Used in standard mode on Linux AVX2 builds (such as the container
   images) for segments longer than 64 loci; costs
   16 bytes per 64 loci per haplotype while a segment is searched.

7b. **Wider ordered scan** (`src/high_memory_index.hpp`,
   `FINDHAP_MOD_SCAN`, AVX2 builds). The ordered candidate lookup filters
   256 positions per step instead of 64 and keeps the filtered block
   between the calls of one search, so resuming after a rejected candidate
   does not repeat the filtering. The earliest surviving position is the
   same. Standard mode only.

8. Version label `1.5.0-modified.2` in `turbohap-build-info`; `--help`
   states that this is an unofficial modified build.

9. Containers and HPC: `Dockerfile`, `docker/turbohap-run` (entry point
   configured by environment variables: folder, threads, low-memory mode,
   input validation, CPU policy, row schedule, input-check-only mode, and
   the `fped`, `fgeno`, `fgenout`, ... file names), `hpc/` (native build,
   Apptainer definition, SLURM job, split/merge per chromosome) and
   `tools/` comparison scripts. User manual: `hpc/README.en.md` (English)
   and `hpc/README.md` (Italian).

10. Documentation: a notice at the top of `README.md` says that this is an
   unofficial modified build; the rest of `README.md`, including its
   benchmarks, describes the official TurboHap 1.5.0.

## Measured effect

Simulated data, options 4 / 600 / 75 / 3, GCC 13. Times on a 2-core
machine; runs with more threads than cores are not representative and are
not reported. Portable build = `-march=haswell` (the CPU target of the
container images), which is how 1.5.0 behaves on any CPU outside its
qualified profiles.

| Dataset | Threads | 1.5.0 | 1.5.0-modified (before 7a and 7b) |
| --- | --- | --- | --- |
| 1 chromosome, 4,000 SNPs, 50,000 animals | 1 | 32.9 s, 0.90 GiB | 32.5 s, 0.33 GiB |
| same | 2 | 19.8 s, 0.90 GiB | 17.7 s, 0.33 GiB |
| 1 chromosome, 4,000 SNPs, 200,000 animals | 2 | 123.5 s, 1.39 GiB | 107.7 s, 0.89 GiB |
| 1 chromosome, 50,000 animals, `maxhap` 1,000,000 | 2 | stops: `std::bad_alloc` | 17.6 s, 0.28 GiB |
| 10 chromosomes x 4,000 SNPs, 5,000 animals, `maxhap` 1,000,000 | 2 | stops: `std::bad_alloc` | 17.8 s, 0.36 GiB |

High-diversity test for the haplotype search (changes 7a and 7b): one
chromosome, 4,000 SNPs, 60,000 animals simulated from 300 ancestral
haplotypes with genotyping errors, up to 44,582 haplotypes in a segment.
Portable build: 1.5.0 takes 95.3 s with 1 thread and 55.5 s with 2;
1.5.0-modified.2 takes 65.7 s and 36.3 s, with identical files. On the same
kind of data with 200,000 animals (up to 137,897 haplotypes in a segment,
2 threads), 1.5.0-modified takes 372 s and 1.5.0-modified.2 223 s. The
larger the library, the more changes 7a and 7b matter.

The `maxhap` 1,000,000 runs give files identical to 1.5.0 with a smaller
`maxhap` (150,000 and 10,000), because these datasets never fill it.

On this machine (Intel Granite Rapids) a native build of 1.5.0 already
uses its qualified profile: there the modified build adds only the
pedigree and output changes (14.4 s vs 14.2 s at 2 threads). The pedigree
and output gains come from overlapping work between threads and should
grow with more cores.

## Containers

The container image uses Ubuntu 20.04 (GCC 9.4, glibc 2.31) so that it
runs under older Docker versions. Static binaries (`STATIC=1`) link
`libpthread` with `--whole-archive` and pass the static OpenMP libraries
to CMake explicitly.

## Validation

Every output file compared byte for byte with unmodified TurboHap 1.5.0
built with the same compiler and flags, both for the portable
(`-march=haswell`) and the native (Granite Rapids) build:

- bundled example (`tests/check_example.py`, against the original Fortran
  outputs), both modes, 1 and 4 threads; GCC 9 and GCC 13 builds;
- simulated 15,000 animals on 3 chromosomes with X-specific handling, the
  same with 20 pedigree cycles, a single chromosome with 50,000 animals,
  and a 10-chromosome genome with 5,000 animals: both modes, 1, 2 and 16
  threads;
- prior-haplotype runs (`hap.listpriors`, `hap.foundpriors`, `hap.Xseg`),
  1 and 4 threads;
- option variants `genout` 2 and 5, `damout` 0 and 2, `listout` -1 and 1,
  2 and 4 threads;
- Docker image, Apptainer image and static binaries on the single
  chromosome dataset.

Unmodified 1.5.0 itself gives files identical to 1.4.0 on all the same
datasets, modes and thread counts.
