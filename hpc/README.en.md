# TurboHap 1.5.0-modified.2: user manual (containers and HPC)

TurboHap 1.5.0-modified.2 is the official TurboHap 1.5.0 with speed and
memory changes for generic Linux clusters (see `MODIFICATIONS.md`). Result
files are byte-for-byte identical to the official 1.5.0. It is an unofficial
build: the TurboHap licence (non-commercial academic research and teaching;
commercial use needs a licence from Green Neuron AI) applies in full.
An Italian version of this manual is in `hpc/README.md`.

Three ways to run it, with the same program and the same results:

| Form | File | When to use it |
| --- | --- | --- |
| A. Docker image | `turbohap-1.5.0-modified.2-docker.tar.gz` | servers with Docker, including older versions |
| B. Apptainer image | `turbohap-1.5.0-modified.2.sif` | HPC clusters (Rocky Linux and others) |
| C. Static binaries or native build | `turbohap-1.5.0-modified.2-static-x86-64-v3.tar.gz`, `hpc/build_native.sh` | any x86-64 Linux; native build for the best speed on the cluster's CPUs |

The ready-made builds target `x86-64-v3` (Intel Haswell, 2013, and AMD Zen
onwards) on an Ubuntu 20.04 base, so they also run under older Docker
versions. If "Illegal instruction" appears, the CPU is older: rebuild with
`MARCH=x86-64-v2`.

## Environment variables

The start-up script `turbohap-run` reads:

| Variable | Meaning |
| --- | --- |
| `FINDHAP_DIR` | data folder; outputs are written there (default: current folder) |
| `OMP_NUM_THREADS` or `FINDHAP_THREADS` | number of threads (`FINDHAP_THREADS` wins; default: all CPUs available) |
| `FINDHAP_LOW_MEMORY=1` | low-memory mode: genotypes stay in temporary disk files |
| `FINDHAP_INPUT_VALIDATION=warn` | report input problems without stopping (default: `strict`) |
| `FINDHAP_VALIDATE_ONLY=1` | check the inputs only, with a detailed report, no imputation |
| `FINDHAP_CPU_POLICY`, `FINDHAP_ROW_SCHEDULE` | advanced options; leave unset |
| `fped`, `fgeno`, `fxhsome`, `fparams`, `fgenout`, `fhapout`, `fhapfill`, `fhapinh`, `fhaplist`, `fhapfd`, `fcross`, `fnotdist`, `fhappriors`, `fhapfoundpriors`, `fhap`, `fallele` | file names, as in the earlier findhap container; unset means the standard FindHap name |

`docker run --rm turbohap:1.5.0-modified.2 --help` lists them all.

## A. Docker

```sh
gunzip -c turbohap-1.5.0-modified.2-docker.tar.gz | docker load
docker run --rm -v "$RUNNING_DIR":/home/galaxy/data \
    -e FINDHAP_DIR=/home/galaxy/data -e OMP_NUM_THREADS=16 \
    -e fped=pedigree.file -e fgeno=genotypes.txt -e fxhsome=chromosome.data \
    -e fparams=findhap.options -e fgenout=genotypes.filled \
    --name v15m_chr7_run1 turbohap:1.5.0-modified.2
```

With the container left running, as in the original pipeline:

```sh
docker run -itd -v "$RUNNING_DIR":/home/galaxy/data --entrypoint sleep \
    --name v15m_chr7_run1 turbohap:1.5.0-modified.2 infinity
docker exec -e FINDHAP_DIR=/home/galaxy/data -e OMP_NUM_THREADS=16 v15m_chr7_run1 turbohap-run
docker rm -f v15m_chr7_run1
```

Without a log file, the output goes to the screen: add
`> "$RUNNING_DIR"/findhap.log 2>&1` to the command, or use
`docker logs <container name>`.

To rebuild the image from source:
`docker build --build-arg MARCH=x86-64-v3 -t turbohap:1.5.0-modified.2 .`
(`--build-arg STATIC=1` links the binaries statically).

## B. Apptainer

```sh
apptainer run --cleanenv --bind "$RUNNING_DIR":/home/galaxy/data \
    --env FINDHAP_DIR=/home/galaxy/data,OMP_NUM_THREADS=16 \
    turbohap-1.5.0-modified.2.sif
```

With many variables, use a file (`--env-file turbohap.env`), one
`NAME=value` per line. `--cleanenv` keeps out the node's variables, such as
an `OMP_NUM_THREADS` set by SLURM.

To rebuild the `.sif` from the Docker image (after `gunzip`):
`apptainer build turbohap.sif docker-archive://turbohap-1.5.0-modified.2-docker.tar`.
`hpc/turbohap.def` builds from Rocky Linux 9 instead.

## C. Static binaries and native build

Static binaries:

```sh
tar xzf turbohap-1.5.0-modified.2-static-x86-64-v3.tar.gz
export PATH=$PWD/turbohap-1.5.0-modified.2-static-x86-64-v3:$PATH
FINDHAP_DIR=$RUNNING_DIR OMP_NUM_THREADS=16 turbohap-run
```

They are linked against glibc 2.31 and also run inside containers based on
older systems.

Native build, on a compute node of the partition you will use:

```sh
srun -p <partition> -c 4 --mem=8G --pty bash hpc/build_native.sh
```

It installs into `~/opt/turbohap-1.5.0-modified` and needs GCC 9 or newer,
CMake 3.16 or newer and Python 3. On Rocky 8 load a newer compiler first
(`module load gcc` or `scl enable gcc-toolset-13 bash`). On Intel Granite
Rapids or AMD Turin a native build also turns on the 1.5.0 optimizations
qualified for those CPUs.

## Per chromosome or whole genome

Both work. For large data, running one chromosome per job is usually better:

| | Per chromosome | Whole genome |
| --- | --- | --- |
| Memory per job | a few GB (about animals x SNPs of the chromosome) | tens or hundreds of GB |
| Parallelism | one job per chromosome on different nodes (SLURM array) | one job on one node |
| High-density threshold (40% of the loci) | computed on the chromosome's SNPs | computed on all SNPs |
| Restart after a failure | only the affected chromosome | the whole genome |

Because the 40% threshold is computed on the loci of the run, per-chromosome
and whole-genome results can differ: an animal whose chip covers one
chromosome poorly can enter the first iteration of a whole-genome run but
not that chromosome's run. This is why a whole-genome run can show many more
haplotypes per segment on some chromosomes. A masking test tells which
strategy imputes better on your data.

With `maxhap` 1,000,000 on a whole genome of about 80,000 SNPs, the modified
build maps about 120 GB of address space for the library but uses only the
rows filled. On clusters with `vm.overcommit_memory = 2` that mapping counts
in full: lower `maxhap` or run per chromosome. The official 1.5.0 tries to
reserve about 640 GB in the same case.

## Running the chromosomes with SLURM

One folder per chromosome, each with the four input files:

```sh
sbatch --array=1-29 --export=ALL,RUN_ROOT=/scratch/$USER/imputation hpc/run_chromosome.slurm
sbatch --array=1-29 --export=ALL,RUN_ROOT=/scratch/$USER/imputation,SIF=$HOME/turbohap-1.5.0-modified.2.sif hpc/run_chromosome.slurm
```

Splitting and merging:

- `python3 hpc/split_by_chromosome.py INPUT_FOLDER RUN_ROOT` splits
  whole-genome inputs into `RUN_ROOT/chr1`, `chr2`, ...
- `python3 hpc/merge_filled.py RUN_ROOT OUTPUT_FILE` merges the
  `genotypes.filled` files. `RUN_ROOT` is the path of the folder that holds
  the chromosome folders, named `chr1`, `chr2`, ... or `1`, `2`, ... `30`;
  they are joined in numeric order.
- `python3 hpc/merge_filled.py RUN_ROOT OUTPUT_FILE --file genotypes.txt`
  merges the original genotypes the same way. Give the output a name other
  than `genotypes.txt`, or put it in another folder.
- Every chromosome must list the same animals in the same order; otherwise
  the script stops and names the first difference.

## Reading the log

- `NNNN high  density genotypes used in iter 1`: the reference population,
  i.e. animals with observed genotypes (0, 1 or 2) at more than 40% of the
  run's loci. Code 5 and SNPs not on the animal's chip count as missing, so
  padding the genotypes with 5 does not change it. The same number is on
  the first line of `hap.Xseg`.
- `NNNN most haplotypes found in segment ...`: the largest number of
  haplotypes found in one segment, one line per step. On a whole genome it
  is the maximum over all chromosomes; the table
  `Number of haplotypes found within each segment` at the end of the log
  gives every chromosome (last step only).
- `More haplotypes than maxhap`: a segment exceeded `maxhap`; the run stops.
- The first line, `TurboHap CPU policy=... effective_threads=...`, shows how
  many threads are in use and on which CPUs.

## Inputs

- **Input checks.** TurboHap checks the input files before it starts and
  stops on errors. `FINDHAP_VALIDATE_ONLY=1` runs the check alone, with a
  detailed report. Prior-haplotype files from the old findhap (a `hap.Xseg`
  in another format) are reported as errors and must be regenerated with
  TurboHap.
- **Pedigree.** Sort by birth date, parents before progeny. An animal
  without a birth date takes `0` in the date field; never leave the field
  empty, or the next number on the line is read as the date. Pedigree cycles
  (an animal that is its own ancestor) do not stop the run but worsen the
  imputation of the families involved: the input check reports them as
  `ancestry_cycle`.

## Comparing with the official build

On your own data and machine:

```sh
python3 tools/compare_builds.py OFFICIAL/findhap MODIFIED/findhap input_folder --threads 16
```

It runs both builds, reports time and peak memory, and checks that every
output file is identical.
