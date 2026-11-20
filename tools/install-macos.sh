#!/bin/sh
# Build and check TurboHap on the Mac where it will run.
set -eu

die() { printf '%s\n' "$*" >&2; exit 1; }
[ "$(uname -s)" = Darwin ] || die "This installer is for an Apple Silicon Mac."
if [ "$(uname -m)" != arm64 ]; then
    if [ "$(/usr/sbin/sysctl -in sysctl.proc_translated 2>/dev/null || :)" = 1 ]; then
        exec /usr/bin/arch -arm64 /bin/sh "$0" "$@"
    fi
    die "This installer requires an Apple M-series processor."
fi
[ "$#" -eq 0 ] || die "Run this installer without arguments."
src=$(CDPATH='' cd -P "$(dirname "$0")/.." && pwd)
app="$HOME/Applications/TurboHap"
if [ -e "$app" ] || [ -L "$app" ]; then
    [ -L "$app" ] && [ -f "$app/.turbohap-install" ] ||
        die "A different installation already exists at $app. Move it before installing here."
fi
if [ -x /opt/homebrew/bin/brew ]; then
    brew=/opt/homebrew/bin/brew
else
    brew=$(command -v brew) || die "Install Homebrew from https://brew.sh and run this command again."
fi
xcrun --find clang++ >/dev/null 2>&1 ||
    die "Install the Apple Command Line Tools with xcode-select --install, then run this command again."

for formula in cmake libomp; do
    if ! "$brew" list --versions "$formula" >/dev/null 2>&1; then
        "$brew" install "$formula"
    fi
done
cmake=$("$brew" --prefix cmake)/bin/cmake
omp=$("$brew" --prefix libomp)
work=$(mktemp -d "${TMPDIR:-/tmp}/turbohap-build.XXXXXX")
stage=''
cleanup() {
    rm -rf "$work"
    if [ -n "$stage" ] && [ "$(readlink "$app" 2>/dev/null || :)" != "$stage" ]; then
        rm -rf "$stage"
    fi
}
trap cleanup EXIT
trap 'exit 1' HUP INT TERM
"$cmake" -S "$src" -B "$work" -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_CXX_COMPILER=/usr/bin/clang++ -DOpenMP_ROOT="$omp"
"$cmake" --build "$work" --parallel 3

mkdir -p "$HOME/Applications"
stage=$(mktemp -d "$HOME/Applications/.TurboHap.XXXXXX")
"$cmake" --install "$work" --prefix "$stage"
/usr/bin/python3 "$stage/share/doc/turbohap/examples/check_installation.py" --binary "$stage/bin/findhap"
touch "$stage/.turbohap-install"
# Atomically switch all three programs together after the installed example passes.
ln -s "$stage" "$stage/activate"
/usr/bin/python3 -c 'import os, sys; os.replace(sys.argv[1], sys.argv[2])' "$stage/activate" "$app"
printf '\nTurboHap is installed. From your input directory, run:\n  ~/Applications/TurboHap/bin/findhap\n'
