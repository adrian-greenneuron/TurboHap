#!/usr/bin/env bash
# Build TurboHap 1.5.0-modified.2 (unofficial) natively on an HPC cluster, no root needed.
#
# Run it ON A COMPUTE NODE of the partition you will use, for example
#   srun -p <partition> -c 4 --mem=8G --pty bash hpc/build_native.sh
# because the default build is tuned to the CPU it is built on
# (-march=native), which also lets TurboHap pick its CPU-specific paths.
# A binary built on the login node may be slower or fail with
# "Illegal instruction" on compute nodes with a different CPU.
#
# Settings (environment variables):
#   PREFIX  install location            (default: $HOME/opt/turbohap-1.5.0-modified)
#   MARCH   CPU target: native, x86-64-v3, x86-64-v2, ...   (default: native)
#   CXX     C++ compiler                (default: g++; GCC 9 or newer)
#   JOBS    parallel compile jobs       (default: 4)
#
# The build runs the bundled byte-for-byte check against the original
# Fortran outputs and stops if anything differs.
set -euo pipefail

SRC="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PREFIX="${PREFIX:-$HOME/opt/turbohap-1.5.0-modified}"
MARCH="${MARCH:-native}"
CXX="${CXX:-g++}"
JOBS="${JOBS:-4}"

die() { echo "ERROR: $*" >&2; exit 1; }

command -v "$CXX" >/dev/null || die "compiler '$CXX' not found (try: module load gcc)"
gcc_major=$("$CXX" -dumpversion | cut -d. -f1)
if [ "$gcc_major" -lt 9 ]; then
    die "GCC $gcc_major is too old. On Rocky 8 load a newer compiler, e.g.
       module load gcc            (if your cluster provides modules), or
       scl enable gcc-toolset-13 bash   (then run this script again)"
fi

command -v cmake >/dev/null || die "cmake not found. Try: module load cmake
       or: python3 -m pip install --user cmake   (then add ~/.local/bin to PATH)"
cmake_version=$(cmake --version | head -1 | awk '{print $3}')
if [ "$(printf '%s\n3.16\n' "$cmake_version" | sort -V | head -1)" != "3.16" ]; then
    die "cmake $cmake_version is older than 3.16 (module load cmake, or pip install --user cmake)"
fi
command -v python3 >/dev/null || die "python3 not found (needed for the output check)"

if ! echo | "$CXX" -march="$MARCH" -x c++ -E - >/dev/null 2>&1; then
    # GCC before 11 does not know the x86-64-v2/v3/v4 names.
    case "$MARCH" in
        x86-64-v2) new=nehalem ;;
        x86-64-v3) new=haswell ;;
        x86-64-v4) new=skylake-avx512 ;;
        *) die "$CXX does not accept -march=$MARCH" ;;
    esac
    echo "== $CXX $gcc_major does not know -march=$MARCH; using the equivalent -march=$new"
    MARCH=$new
fi

BUILD="$(mktemp -d "${TMPDIR:-/tmp}/turbohap-build-XXXXXX")"
trap 'rm -rf "$BUILD"' EXIT

echo "== Compiler: $("$CXX" --version | head -1); CPU target: $MARCH"
cmake -S "$SRC" -B "$BUILD" -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_CXX_COMPILER="$CXX" -DBUILD_TESTING=OFF \
      "-DFINDHAP_RELEASE_FLAGS=-O3 -march=${MARCH} -funroll-loops -flto -ffp-contract=off"
cmake --build "$BUILD" --parallel "$JOBS"

echo "== Checking outputs against the original Fortran (bundled example)"
LC_ALL=C OMP_DYNAMIC=FALSE python3 "$SRC/tests/check_example.py" --binary "$BUILD/findhap"

cmake --install "$BUILD" --prefix "$PREFIX"
install -m 755 "$SRC/docker/turbohap-run" "$PREFIX/bin/turbohap-run"
install -m 644 "$SRC/MODIFICATIONS.md" "$PREFIX/share/doc/turbohap/MODIFICATIONS.md"
"$PREFIX/bin/turbohap-build-info" --build-info
echo
echo "== Installed: $PREFIX/bin/findhap (and turbohap-run, turbohap-validate-inputs)"
