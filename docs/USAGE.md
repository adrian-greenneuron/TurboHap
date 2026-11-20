# Using TurboHap

```text
findhap [threads] [--low-memory] [--cpu-policy=auto|inherit|performance]
        [--row-schedule=auto|static|dynamic] [--cpu-info]
        [--input-validation=strict|warn] [--stdout=legacy|modern]
        [--x-progeny=legacy|corrected] [--processing-scope=legacy|chromosome]
```

The default is TurboHap. Add `--low-memory` to select TurboHap LM. Both modes
are in the same executable and use the same input and output formats.
The flag may precede or follow the thread count. `findhap --help` prints usage.

Use a positive one- or two-digit thread count, capped by the OpenMP maximum.
Without it, Linux uses the OpenMP default, which can be set with
`OMP_NUM_THREADS`. Native Apple Silicon automatic mode instead uses the
performance-core count when neither a positional count nor `OMP_NUM_THREADS`
is supplied. The original two-character interpretation is retained:
`100` means 10, and `010` means 1. TurboHap still ignores trailing positional
arguments from legacy FindHap commands. Unrecognised long options such as
`--lowmemory` cause an error. Use `findhap --help` to check the option names.

## CPU placement

Normal commands use `--cpu-policy=auto`; no extra flag is needed. On a native
Linux DGX Spark build, this discovers the complete faster core group and uses it
when the effective thread count fits. A larger request retains the available
allocation. When TurboHap controls placement and there are fewer chromosomes
than threads, it pins workers for the main pedigree pass and gives faster cores
more segment work. It restores each worker's prior affinity after the pass.

Existing CPU restrictions and explicit `OMP_PLACES`, `OMP_PROC_BIND` or
`GOMP_CPU_AFFINITY` settings take precedence in automatic mode, including
`OMP_PROC_BIND=FALSE`. Busy cores remain eligible: this policy does not reserve
cores or manage other jobs.

Qualifying AMD EPYC Milan, Genoa and Turin builds, and Intel Cascade Lake builds, default to
`OMP_PROC_BIND=close` and `OMP_PLACES=cores` when no explicit placement setting
is supplied. The launcher re-executes before either engine starts so OpenMP
initialises with those settings. The thread count and allowed CPU allocation
are preserved. `--cpu-info` shows the proposed OpenMP binding on those builds.
The run's CPU diagnostic reports whether the OpenMP runtime activated it.

On native Apple Silicon, automatic mode discovers the host's performance-core
count and requests user-initiated quality of service for the main thread and
workers. This is an advisory scheduling request; macOS retains control of actual
core placement. No fixed Mac model or core count is required. Explicit
`OMP_NUM_THREADS`, positional counts and caller OpenMP placement settings take
precedence. Caller wait-policy settings are also retained. See the
[Mac guide](../MACOS.md).

- `--cpu-policy=inherit` keeps the caller's placement without automatic selection
  or binding. On Apple Silicon it also leaves scheduling priority and the default
  thread count to the caller and OpenMP runtime.
- `--cpu-policy=performance` explicitly requires a sufficient, reliably ranked
  performance-core group within the allowed Linux allocation. It errors if that
  cannot be established or an explicit binding setting conflicts. This strict
  affinity policy is unavailable on macOS.
- `--cpu-info` prints the discovered topology and proposed placement as JSON,
  then exits without reading data or writing results. It is a dry-run, not a
  record of every worker's execution.

Both modes use the same placement policy. Rebuild on the intended CPU so the
applicable native instruction paths are selected. The [installation guide](INSTALL.md#processor-support) lists supported compiler and processor combinations.

## Row scheduling

`--row-schedule=auto` is the default. It chooses how independent per-animal
work is distributed among workers without changing inference decisions or
result-file order.

On Linux, automatic mode uses dynamic chunks of four rows only when the OS
reports complete, consistently ranked core classes and the requested workers
exceed the faster physical-core group while fitting the permitted physical
cores. Static scheduling is retained when the request fits the faster group,
all cores have one class, metadata is incomplete, the request requires SMT or
oversubscription, or explicit OpenMP placement/scheduling or dynamic-team
settings are present. Merely crossing the physical-core count on an x86 system
does not enable the heterogeneous-core scheduler.

Native Apple Silicon currently retains static rows in automatic mode. Its
advisory scheduling and performance-core discovery are separate from Linux's
strict affinity and capacity metadata.

- `--row-schedule=static` distributes independent rows using the original static
  schedule.
- `--row-schedule=dynamic` explicitly selects chunks of four rows.

An explicit row-schedule option takes precedence over automatic selection.
This affects the independent row loops, not every stage of the program.
Inspect the startup diagnostic or `--cpu-info` for the selected policy.

## Input files

Run in the directory containing these files:

| File | Contents and ordering |
| --- | --- |
| `findhap.options` | Original positional control file, described below. |
| `pedigree.file` | Sex, numeric animal/sire/dam IDs, birth date, animal ID and name; ascending birth date. |
| `genotypes.txt` | Ascending numeric animal ID; three 10-character integer fields (animal, chip, locus count), one separator, then one genotype character per locus. Codes 0, 1, 2 and 5 denote BB, AB, AA and missing. |
| `chromosome.data` | Marker map and chip membership, ordered by chromosome and position; the X-specific chromosome is last. |

Fixed-width fields matter: retain the three header fields, one separator and
chip-specific marker count. Use explicit supported genotype codes; strict mode
rejects blank calls because they can indicate shifted columns. Do not trim
records or reorder markers.
The [bundled example](../examples/README.md) illustrates the complete formats.

Supply at most one genotype record per pedigree animal. A duplicate stops a
run in both strict and warn modes and reports the two line numbers in
`genotypes.txt`. Resolve duplicate calls in the input export before rerunning.

Optional `hap.listpriors`, `hap.foundpriors` and `hap.Xseg` contain compatible
prior haplotype results. Keep these files together when reusing a reference or
imputing additional animals. Their formats and interpretation are unchanged.

## Options

Keep an existing valid `findhap.options` file unchanged. The first ten integer
fields are positional; the next two records are skipped, then `errate` is read.
For example:

```text
4 30 100 50 3 50000 1 1 1 1
iters Xchrom maxlen minlen steps maxhap hapout genout damout listout

.004
errate
```

These are example values, not built-in defaults. The reader supports the
validated original Fortran numeric forms, including commas, continued lists,
repeat counts such as `4*1`, and real exponents such as `4D-3`.

| Field | Meaning |
| --- | --- |
| `iters` | Maximum iterations; values below 3 become 3. Prior-assisted runs begin at the final iteration. |
| `Xchrom` | Final chromosome number for X-specific handling; 0 disables it. |
| `maxlen` | Initial maximum haplotype segment length, in loci (not base pairs). |
| `minlen` | Target final length, in loci; successive rounded lengths retain original behaviour. |
| `steps` | Number of segment-length stages; equal minimum and maximum lengths force one stage. |
| `maxhap` | Maximum distinct haplotypes per segment. Capacity exhaustion exits with status 100 and requests a larger value. |
| `hapout` | 0 suppresses `haplotypes.txt`; 1 writes eligible genotyped/imputed animals. Value 2 retains the original empty-write behaviour, despite the original comment advertising all animals. |
| `genout` | 0 suppresses `genotypes.filled`; 1 keeps codes 0–5; 2 selects calls 0/1/2; 5 changes unresolved 3/4 to 5. |
| `damout` | 0 excludes non-genotyped ancestors from selected outputs; 1 includes eligible females; 2 also includes eligible males. |
| `listout` | 0 suppresses named library outputs; 1 writes all stages; −1 writes only the final stage. |
| `errate` | Finite single-precision mismatch fraction from 0 to 1 inclusive. Used in haplotype matching and inheritance correction. |

The supplied example keeps `maxhap=50000`. Both modes honour the value in
your `findhap.options`; the limit applies to each segment independently.

If 50,000 is insufficient, `maxhap=65535` is an intermediate option that
provides approximately **31.1% more haplotype capacity** while retaining
16-bit internal haplotype IDs. A setting of 65,536 or higher selects 32-bit
IDs, based on the requested capacity even if fewer haplotypes are ultimately
found. The identifiers and output formats remain unchanged.

Supported native builds avoid eagerly committing all unused
frequency/order metadata to physical RAM. This weakens the relationship between
maxhap and peak resident memory; it does not remove every capacity-dependent
allocation. Other buffers and the genotype data still need memory. Crossing the
65,535 boundary doubles the affected per-animal ID storage, not all process
memory. The separate accelerated LM lookup widens its candidate positions based
on live indexed entries, within its memory budget. Neither extra headroom nor a
larger maxhap is a promise of faster inference. Set adequate capacity and use
the modern completion summary to inform later runs with comparable inputs.

## Output files

| File | Result |
| --- | --- |
| `genotypes.filled` | Imputed genotypes, controlled by `genout`. |
| `haplotypes.txt` | Paternal and maternal alleles at each locus, controlled by `hapout`. |
| `hap.list`, `hap.found` | Segment haplotype library and per-animal assignments. |
| `hap.inherit`, `cross.overs` | Inheritance and crossover records. |
| `hap.filled` | Per-animal imputation summary. |
| `allele.frequency` | Marker allele frequencies and missingness. |
| `hap.Xseg` | X-segment and reference information used with prior haplotypes. |

Additional files created by the original options combinations, including
`do_not_distrib` and implicit `fort.26`, are retained where applicable.
The normal-completion message is `Program findhap completed normally`.
Inspect completion and errors as well as output files: an old output file can
survive a run that writes no records to it.
After a reported failure, files written by that run can contain incomplete
results. Earlier files that received no new data keep their contents. Use a
different working directory to retain a previous result set when rerunning.

## Choosing a mode

TurboHap prioritises speed with resident genotype storage and indexed haplotype
search. TurboHap LM reduces resident storage using chromosome processing,
packed data, bounded working buffers and temporary disk files. It retains the
same output decisions and bytes, at the cost of more I/O and some additional
computation. See the [measured trade-offs](turbohap_benchmarks_201126.md).

Peak memory depends on pedigree size, marker counts, chromosome lengths,
haplotype diversity, options and threads. TurboHap LM also uses temporary disk
space. Allow room for both output and temporary files, and use disk-backed
storage when reducing RAM use is the objective.

## Input validation

Strict exhaustive genotype-layout and marker-map checks run by default before inference opens legacy output files. The separate `turbohap-validate-inputs --directory DIR` auditor collects structural and cross-file issues across every ordinary input and active prior file. See [input validation](INPUT_VALIDATION.md) for report examples, error counts, line/byte locations, optional JSON/TSV and privacy guidance.

Use `--input-validation=warn` only to investigate safe legacy layout ambiguities. It retains the old blank/trailing-digit interpretation and can leave misaligned calls; unsafe inputs still fail. Warning detail is capped at five examples, followed by counts. Normal runs should retain the strict default.

## Correcting X-dependent progeny scoring

When FindHap v3 infers haplotypes for an ungenotyped male from his offspring,
the presence of an X-specific chromosome can cause it to score candidate
haplotypes against the dam's haplotypes alone, even on autosomes. The default
`--x-progeny=legacy` preserves that behaviour for compatibility.

Use `--x-progeny=corrected` to use the available sire and dam evidence on autosomes
and diploid pseudoautosomal segments. On X-specific segments, TurboHap uses
the existing maternal scoring rule. It identifies the X-specific chromosome
from `XChrom` in `findhap.options`.

```sh
/path/to/findhap 20 --x-progeny=corrected
/path/to/findhap 20 --low-memory --x-progeny=corrected
```

Corrected scoring can change inferred haplotypes, filled genotypes and derived
result files. TurboHap records its use in the startup diagnostics on stderr.
[See how the two rules use evidence from a bull’s parents and calves](X_PROGENY.md).

## Processing each chromosome independently

`--processing-scope=legacy` is the default. It uses genome-wide coverage and
quality scores and writes a single set of results in the working directory.

`--processing-scope=chromosome` uses each chromosome's coverage to decide when
to include genotyped animals in population matching. It applies the ancestor
and genotyped-animal output quality filters separately on each chromosome.
An animal can qualify for reporting on one chromosome and be excluded on
another. [Follow one cow through the two analyses](PROCESSING_SCOPE.md).

Chromosome scope automatically uses corrected X-progeny scoring. TurboHap
prints a startup warning and reports whether it overrode an explicit
`--x-progeny=legacy` request.

```sh
/path/to/findhap 20 --processing-scope=chromosome
/path/to/findhap 20 --low-memory --processing-scope=chromosome
```

TurboHap reads the input as a whole and writes the enabled result files for
each chromosome under `chromosome-results/chrN/`, where `N` is the chromosome
number in the input map. Marker and segment indices in those files start at
one within each chromosome. Start in a directory without an existing `chromosome-results/`. TurboHap refuses to replace a previous result tree. TurboHap publishes `chromosome-results/` after closing all result files.

A multi-chromosome input with active priors cannot run in chromosome scope: the prior supplies only one reference count for the entire map. To use chromosome-specific priors, prepare one chromosome at a time, with its own `hap.listpriors`, `hap.foundpriors` and `hap.Xseg`, then run it separately.

## Console output and run logs

**Legacy is the default in release builds.** It keeps console progress close to
FindHap's familiar format. Result-file compatibility is a separate contract;
elapsed timings and launcher diagnostics are not expected to be identical.

Choose modern output for clearer phase progress, UTC date/time stamps, elapsed
step timings and a peak live-haplotype capacity summary:

```sh
/path/to/findhap 20 --stdout=modern > run.log 2> diagnostics.log
/path/to/findhap 20 --low-memory --stdout=modern > lm-run.log 2> lm-diagnostics.log
```

These are ordinary shell redirects; TurboHap does not silently create an extra
log file. `--stdout=legacy` explicitly selects the default style. Modern output
is useful for checking phase durations and choosing maxhap on future runs.
The peak live count can exceed the retained library count printed after pruning.
The previous peak describes that completed dataset/options combination; it is
not a guarantee for newly added animals or different options. Capacity summaries
are diagnostic and do not change any of the ten legacy output-file formats.

Modern output also states the actual maximum segment length at every stage,
in loci rather than base pairs. For example, `Step 2 of 3: maximum segment
length 71 loci` records the rounded value used by the algorithm.

## Sizing memory and scratch storage

Choose `maxhap` above the largest **live** haplotype count seen with comparable animals, marker panels and settings. The program still stops when any segment exhausts its capacity. TurboHap sizes its library reservation using the actual rounded segment widths and segment counts at each stage. Size arithmetic uses wide offsets and rejects unsupported capacities before inference opens output files.

LM writes temporary genotype data on the same filesystem as the run directory. For a scratch-space estimate, allow about one byte per four assayed calls for the raw packed cache, plus about one byte per four full-map loci per input genotype record for the expanded working data. Include rounding at segment boundaries and space for the result files. [Scratch-space estimates](INSTALL.md#storage-for-lm)
