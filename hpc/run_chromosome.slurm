#!/bin/bash
# Run TurboHap on one chromosome per SLURM task.
#
# Layout: one directory per chromosome, each holding findhap.options,
# pedigree.file, chromosome.data and genotypes.txt (pedigree.file can be a
# symlink to a shared copy):
#   $RUN_ROOT/chr1/  $RUN_ROOT/chr2/  ...
# Outputs are written into the same directory. Use a scratch filesystem:
# --low-memory also writes its temporary files there.
#
# Examples:
#   # all 29 autosomes, each task with 16 cores
#   sbatch --array=1-29 --export=ALL,RUN_ROOT=/scratch/$USER/imputation hpc/run_chromosome.slurm
#   # one chromosome only
#   sbatch --export=ALL,RUN_ROOT=/scratch/$USER/imputation,CHR=7 hpc/run_chromosome.slurm
#   # using the Apptainer image instead of a native build
#   sbatch --array=1-29 --export=ALL,RUN_ROOT=...,SIF=$HOME/turbohap.sif hpc/run_chromosome.slurm
#
#SBATCH --job-name=turbohap
#SBATCH --cpus-per-task=16
#SBATCH --mem=24G
#SBATCH --time=24:00:00
#SBATCH --output=turbohap_%A_%a.log
##SBATCH --partition=<partition>
##SBATCH --account=<account>
set -euo pipefail

RUN_ROOT="${RUN_ROOT:?set RUN_ROOT to the folder that contains chr1, chr2, ...}"
CHR="${CHR:-${SLURM_ARRAY_TASK_ID:-}}"
[ -n "$CHR" ] || { echo "set CHR or submit with --array" >&2; exit 1; }
RUN="$RUN_ROOT/chr$CHR"
THREADS="${SLURM_CPUS_PER_TASK:-1}"
FINDHAP="${FINDHAP:-$HOME/opt/turbohap-1.5.0-modified/bin/findhap}"
SIF="${SIF:-}"
EXTRA="${EXTRA:-}"              # e.g. EXTRA=--low-memory

for f in findhap.options pedigree.file chromosome.data genotypes.txt; do
    [ -e "$RUN/$f" ] || { echo "missing $RUN/$f" >&2; exit 1; }
done

export OMP_NUM_THREADS="$THREADS" OMP_DYNAMIC=FALSE LC_ALL=C
# Keep each thread on its own core (usually faster on HPC nodes).
export OMP_PLACES="${OMP_PLACES:-cores}" OMP_PROC_BIND="${OMP_PROC_BIND:-close}"

echo "chromosome $CHR in $RUN, $THREADS threads, host $(hostname), start $(date)"
cd "$RUN"
start=$(date +%s)
if [ -n "$SIF" ]; then
    # shellcheck disable=SC2086
    apptainer run --bind "$RUN" --pwd "$RUN" "$SIF" "$THREADS" $EXTRA
else
    # shellcheck disable=SC2086
    "$FINDHAP" "$THREADS" $EXTRA
fi
echo "chromosome $CHR finished in $(( $(date +%s) - start )) s, $(date)"
