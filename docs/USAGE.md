# Using TurboHap

```text
findhap [threads] [--low-memory] [--cpu-policy=auto|inherit|performance]
```

The default is TurboHap. Add `--low-memory` to select TurboHap LM. Both modes
are in the same executable and use the same input and output formats.
The flag may precede or follow the thread count. `findhap --help` prints usage.

Use a positive one- or two-digit thread count, capped by the OpenMP maximum.
Without it, the program uses the OpenMP default, which can be set with
`OMP_NUM_THREADS`. The original two-character interpretation is retained:
`100` means 10, and `010` means 1. Extra unrecognised arguments after the count
are ignored; recognised launcher flags are handled wherever they appear.

## CPU placement

Normal commands use `--cpu-policy=auto`; no extra flag is needed. On a native
Linux DGX Spark build, this discovers the complete faster core group and uses it
when the effective thread count fits. Workers can move within that group;
TurboHap does not add thread binding on Spark. A larger request retains the
available allocation rather than silently reducing the thread count.

Existing CPU restrictions and explicit `OMP_PLACES`, `OMP_PROC_BIND` or
`GOMP_CPU_AFFINITY` settings take precedence in automatic mode, including
`OMP_PROC_BIND=FALSE`. Busy cores remain eligible: this policy does not reserve
cores or manage other jobs.

Native Milan, Cascade Lake and qualified Turin builds default to
`OMP_PROC_BIND=close` and `OMP_PLACES=cores` when no explicit placement setting
is supplied. The launcher re-executes before either engine starts so OpenMP
initialises with those settings. The thread count and allowed CPU allocation
are preserved. Other CPU profiles retain their existing placement policy.

- `--cpu-policy=inherit` keeps the caller's placement without automatic selection
  or binding.
- `--cpu-policy=performance` explicitly requires a sufficient, reliably ranked
  performance-core group within the allowed allocation. It errors if that cannot
  be established or an explicit binding setting conflicts.
- `--cpu-info` prints the discovered topology and proposed placement, then exits
  without reading data or writing results. It is a dry-run, not a record of every
  worker's execution.

Both modes use the same policy. Rebuild on the intended CPU so the qualified
native paths are selected. Automatic performance-core selection on Apple Silicon
has not been implemented or validated.

## Input files

Run in the directory containing these files:

| File | Contents and ordering |
| --- | --- |
| `findhap.options` | Original positional control file, described below. |
| `pedigree.file` | Sex, numeric animal/sire/dam IDs, birth date, animal ID and name; ascending birth date. |
| `genotypes.txt` | Ascending numeric animal ID; three 10-character integer fields (animal, chip, locus count), one separator, then one genotype character per locus. Codes 0, 1, 2 and 5 denote BB, AB, AA and missing. |
| `chromosome.data` | Marker map and chip membership, ordered by chromosome and position; the X-specific chromosome is last. |

Fixed-width fields matter: keep blank genotype fields and chip-specific marker
counts in their original positions. Do not trim records or reorder markers.
The [bundled example](../examples/README.md) illustrates the complete formats.

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
| `maxlen` | Initial haplotype segment length. |
| `minlen` | Target final length; successive rounded lengths retain original behaviour. |
| `steps` | Number of segment-length stages; equal minimum and maximum lengths force one stage. |
| `maxhap` | Maximum distinct haplotypes per segment. Capacity exhaustion exits with status 100 and requests a larger value. |
| `hapout` | 0 suppresses `haplotypes.txt`; 1 writes eligible genotyped/imputed animals. Value 2 retains the original empty-write behaviour, despite the original comment advertising all animals. |
| `genout` | 0 suppresses `genotypes.filled`; 1 keeps codes 0–5; 2 selects calls 0/1/2; 5 changes unresolved 3/4 to 5. |
| `damout` | 0 excludes non-genotyped ancestors from selected outputs; 1 includes eligible females; 2 also includes eligible males. |
| `listout` | 0 suppresses named library outputs; 1 writes all stages; −1 writes only the final stage. |
| `errate` | Single-precision allowed mismatch fraction used in haplotype matching and inheritance correction. |

The supplied example keeps `maxhap=50000`. Both modes honour the value in
your `findhap.options`; the limit applies to each segment independently.

If 50,000 is insufficient, `maxhap=65535` is an intermediate option that
provides approximately **31.1% more haplotype capacity** while retaining
16-bit internal haplotype IDs. A setting of 65,536 or higher selects 32-bit
IDs, based on the requested capacity even if fewer haplotypes are ultimately
found. The identifiers and output formats remain unchanged.

The extra capacity uses more memory even below the 32-bit threshold: the
haplotype library and several other buffers are sized using `maxhap`.
Increasing it from 50,000 to 65,535 makes those capacity-dependent allocations
approximately **31.1% larger**, while the arrays storing haplotype IDs retain
their size. The percentage change in total peak memory depends on the dataset
and mode. Crossing the threshold doubles storage for the affected ID arrays,
in addition to increasing the capacity-dependent buffers; it does not double
all program memory. Higher capacities also increase some initialization and
scanning work, so keep 50,000 when it is sufficient and raise it when needed.

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

## Choosing a mode

TurboHap prioritises speed with resident genotype storage and indexed haplotype
search. TurboHap LM reduces resident storage using chromosome processing,
packed data, bounded working buffers and temporary disk files. It retains the
same output decisions and bytes, at the cost of more I/O and some additional
computation. See the [measured trade-offs](turbohap_benchmarks_301026.md).

Peak memory depends on pedigree size, marker counts, chromosome lengths,
haplotype diversity, options and threads. TurboHap LM also uses temporary disk
space. Allow room for both output and temporary files, and use disk-backed
storage when reducing RAM use is the objective.
