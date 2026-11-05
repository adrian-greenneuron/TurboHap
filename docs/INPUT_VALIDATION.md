# Checking TurboHap input files

Green Neuron AI · TurboHap v1.4.0

`turbohap-validate-inputs` examines the complete supported input set before
inference. It collects structural errors and cross-file inconsistencies into
one report, rather than stopping at the first bad record. Each issue has a
count, an explanation, suggested corrective action and example locations.
It does not repair data or open inference result files.

The tool is separate from `findhap`: the ordinary exhaustive inference checks,
standard/LM implementations and native CPU policies are unchanged. Running the
audit is an additional preparation step; it does not disable inference checks.
The audit is supplied with TurboHap v1.4.0.

## Build and run

From the extracted source directory, using the normal build:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=g++
cmake --build build --target turbohap-validate-inputs

# Read-only audit with a human-readable summary on stdout:
build/turbohap-validate-inputs --directory /path/to/inputs

# Also save a Markdown report and its matching JSON data:
build/turbohap-validate-inputs --directory /path/to/inputs \
  --report-prefix /existing/private/reports/input_audit_051126

# Structured stdout, fewer summary examples and every issue event in a TSV:
build/turbohap-validate-inputs --directory /path/to/inputs \
  --json --max-examples 2 \
  --issues-tsv /existing/private/reports/input_issues_051126.tsv
```

Report parent directories must already exist. `--report-prefix` creates both
`.md` and `.json`; the argument is a basename without an extension. Existing
files and symlinks are refused, and reports cannot use reserved input/result
filenames in the input directory. Report files are created with owner-only
permissions (`0600`). Stdout redirection uses the caller's own shell permissions.

The executable is also included in the CMake installation. It has
no Python runtime dependency. Tests and benchmark orchestration use Python.

## What is examined

Every physical record is read. Genotype and haplotype payloads are examined in
full, rather than sampled. Checks that depend on an invalid file are identified
as blocked; independently usable files continue to be examined.

| File | Checks |
| --- | --- |
| `findhap.options` | All ten mandatory integer controls, supported selectors and numeric domains, error-rate syntax/range, skipped-record layout and the actual rounded segment-length schedule. The final semantic parse uses the engine's options reader, including comma separators, repetitions and supported Fortran real notation. |
| `pedigree.file` | Five required fields, integer ranges, positive unique animal IDs, record width, sex/date review, missing ancestors, self-parentage, identical sire/dam, parental sex/date conflicts and directed ancestry cycles. Extra metadata columns are identified as informational. |
| `chromosome.data` | Header, complete row schema, consistent chip columns, positive indices, consecutive overall/within-chromosome ordinals, chromosome grouping, nondecreasing positions and chip ordinals. Duplicate marker names produce a review warning. |
| `genotypes.txt` | Exact fixed-width header, column-31 separator, every declared genotype call, truncation, extra calls, unsupported bytes, chip/map length agreement and pedigree joins. Duplicate or decreasing animal IDs are reported for review. |
| `hap.listpriors` | When active: headers, percentages, step/segment ordering and sentinels, unique haplotype references, complete `0..2` payloads, consistent geometry, chromosome bounds, full marker coverage and agreement with the options/map schedule. Frequency totals have rounding tolerance and produce review warnings when inconsistent. |
| `hap.foundpriors` | When active: matching headers, steps and sentinels, paired parental records, duplicate assignments, assignment widths and every positive haplotype reference against the corresponding library step/segment. Prior animals outside the current pedigree are reported for review. |
| `hap.Xseg` | When active: both required numeric summary records, nonnegative reference count, valid X-segment boundary and agreement with the final library. Extra nonempty records are reported. |

The four ordinary files are required. Prior import is activated by
`hap.listpriors`; the audit then requires and checks its matching
`hap.foundpriors` and `hap.Xseg`. Without `hap.listpriors`, an existing
`hap.foundpriors` is reported as unused and an existing `hap.Xseg` is identified
as unused. Such files are not audited as active inputs. A previous run can leave
`hap.Xseg` as an output, so its presence alone does not activate prior import.

Nonpositive parent codes are treated as unknown parents. A missing positive
ancestor can be intentional when a pedigree subset is supplied, so it is a
warning. Ordinary inbreeding and repeated ancestors are allowed: a directed
cycle, where an animal becomes its own ancestor, is a separate error. The cycle
check uses iterative graph traversal, supporting deep pedigrees without a
recursive call-stack limit.

## Reading the report

The first section gives the status and total error, warning and informational
counts. A file inventory distinguishes checked files, absent optional files,
unused files, unreadable files and incomplete checks. An issue table groups
findings by filename and issue code. The detailed sections explain each issue
and how to investigate it. Dataset counts, parsed run settings and blocked
checks follow.

| Severity | Meaning | Exit status |
| --- | --- | --- |
| Error | A structural, reference or safety constraint failed; correct the source data before relying on the input set. | `2` |
| Warning | A possible source-data problem or a deliberate unusual layout needs review. It is not automatically a fatal error. | `0` if there are no errors or blocked checks |
| Information | An observation such as unused trailing metadata; no failure by itself. | `0` if there are no errors or blocked checks |

An audit with blocked checks is `incomplete` unless errors already make it
`fail`; both return `2`. Operational failures, such as an unreadable report
destination, also return `2` and explain the failure on stderr. Validation
details are in stdout and the saved reports; normal failures do not print one
stderr message per animal.

**Counts describe issue occurrences, not unique animals.** One row may violate
several rules. For example, 50 rows containing two blank genotype calls each
produce 50 occurrences of `blank_genotype_fields` and 100 affected cells. Other
independent problems on those rows have their own counts. Informational notices
are separate from warnings and errors.

Locations use physical line numbers and one-based **byte** columns, so they can
be found in the original file without relying on valid preceding records.
Column `0` refers to the record/file as a whole. Relationship warnings identify
the related parent's line as well as the child's line. Reports use locations
rather than reproducing animal IDs or complete biological records.

An illustrative issue section looks like this:

```text
error: genotypes.txt / blank_genotype_fields

Legacy reading treats each blank as code 0; an extra separator can shift every marker.

Suggested action: Check column 31/32 alignment and re-export. Use explicit code 5
for missing calls; never silently delete internal blanks.

- genotypes.txt line 12, column 32 [genotypes]:
  2 blanks in the declared payload. The first call is blank: likely an extra separator.
```

The default is **five examples per issue type**, not five lines scanned. All
occurrences are still counted. `--max-examples` accepts `0..100`. The JSON report
includes each exact count, affected-value count, saved examples and number of
omitted examples; its schema version is `2`.

Use `--issues-tsv` when all locations are needed. It streams one row per issue
event with severity, file, issue code, line, byte column, field, affected-value
count, detail and suggested action. Multiple bad cells under the same rule on
one input row form one event with their count and first affected column. The
TSV therefore gives every affected row/rule, rather than a separate event per
bad cell. It can be large and is optional.

## Scope, limits and data handling

- This is an exhaustive **structural and cross-file audit**, not a biological
  accuracy certificate. It cannot establish the true genome assembly, marker
  order, allele encoding or Mendelian/genomic correctness from these files.
- Reports distinguish review warnings from definite structural errors. Resolve
  biological inconsistencies using source records; the tool never guesses
  replacements or deletes animals to make a pedigree pass.
- Invalid dependencies can prevent particular cross-checks. For example, a bad
  marker map blocks chip/map agreement while genotype-byte checks still run.
  Repair the earlier problems and rerun; a failed audit is not a complete
  inventory of every consequence of malformed data.
- Option-derived prior geometry is compared for consecutive chromosome codes.
  Gapped chromosome codes produce an explicit review warning because that
  schedule comparison is not performed. Coverage, chromosome bounds and prior
  references are still checked.
- Input buffers have file-specific record-size bounds. Oversized rows are
  reported and consumed without retaining their entire contents, then scanning
  resumes at the next physical row. Retained options text is limited to 16 MiB;
  more than 100,000 effective schedule steps cannot be certified. These limits
  prevent malformed inputs from causing unbounded parser allocations; they are
  not a guarantee that any accepted configuration fits available inference RAM.
- Summary examples are bounded, but pedigree IDs/links, marker labels and active
  prior references are retained for exact cross-file checks. Audit memory grows
  with their counts. The program does not retain the complete genotype matrix.
- Inputs must be stable regular files or symlinks to regular files. File size
  and modification time are checked again at completion. This detects ordinary
  concurrent changes; it is not a cryptographic snapshot or protection against
  a writer deliberately preserving metadata. Revalidate after editing.
- Reports can reveal private cohort properties even without animal identifiers.
  Keep real-data reports and detailed TSVs local/private; do not add them to Git
  or public releases. The committed regression fixtures are synthetic.


## Inference checks and diagnostic warning mode

Normal `findhap` runs keep strict exhaustive genotype-layout and marker-map checks enabled in both modes. They stop at a structural error, print a filename, physical line and byte-column diagnostic where applicable, and return a nonzero status before opening legacy result files. The external audit checks a wider set of relationships and collects issue counts across usable files; it does not disable or replace inference checks.

`findhap 10 --input-validation=warn` is an explicit diagnostic option for safe legacy layout ambiguities. It preserves blank genotype fields as zero and ignores extra trailing genotype digits as the legacy reader did; results may consequently remain misaligned. At most five detailed warnings are printed before aggregate counts. Unsafe headers, unsupported bytes, truncation, chip/count disagreements and invalid maps remain errors. Use correctly exported inputs for production inference.

For new input exports, run the external audit first, investigate errors and review warnings, then run ordinary strict inference. Revalidate after editing an input. Reports and detailed issue TSVs can contain private identifiers and cohort properties; store them privately.
