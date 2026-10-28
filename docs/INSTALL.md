# Installing TurboHap

TurboHap and TurboHap LM are included in one CPU executable, `findhap`.
A GPU, CUDA, Python and a Fortran compiler are not required to build or run it.
Python 3 is optional for the example and launcher checks.

## Requirements and platform status

Use CMake 3.16 or newer and a C++17 compiler with OpenMP and its runtime library.

| Platform | Toolchain | Status |
| --- | --- | --- |
| Ubuntu 24.04, ARM64 | GCC 13.3, CMake | Build, correctness and performance validated on DGX Spark. |
| Ubuntu 24.04, Intel and AMD x86-64 | GCC 13.3, CMake | Build and output checks passed; detailed benchmarks are ongoing. |
| Other Linux systems | GCC, CMake | Require local validation. |
| macOS, Apple Silicon or Intel | Homebrew GCC 13, CMake | Build route below is untested. |
| Windows through WSL2 | Ubuntu, GCC, CMake | Build route below is untested. |
| Native Windows | — | Not currently supported; the source uses POSIX file APIs. |

## Get the source

Download the [version 1.1.0 source ZIP](https://github.com/adrian-greenneuron/TurboHap/archive/refs/tags/v1.1.0.zip),
then extract it. Open a terminal in the extracted directory containing
`CMakeLists.txt`; run the build commands below from that directory.

## Linux

On Ubuntu/Debian, install the build tools:

```sh
sudo apt update
sudo apt install build-essential cmake
```

From the directory containing this release's `CMakeLists.txt`:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=g++
cmake --build build --parallel 2
./build/findhap --help
```

The result is `build/findhap`. Compilation parallelism does not set the runtime
thread count. To build only the executable, add `-DBUILD_TESTING=OFF` at configure time.

## macOS — untested

Install Apple's Command Line Tools with `xcode-select --install` and install
[Homebrew](https://docs.brew.sh/Installation). Use the
[Homebrew GCC 13 package](https://formulae.brew.sh/formula/gcc@13) explicitly:

```sh
brew install cmake gcc@13
cmake -S . -B build-macos -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_CXX_COMPILER="$(brew --prefix gcc@13)/bin/g++-13"
cmake --build build-macos --parallel 2
./build-macos/findhap --help
```

Use a Homebrew installation native to your Mac's architecture. The macOS
binary needs independent output validation; Linux measurements do not establish
macOS correctness or speed.

## Windows through WSL2 — untested

Follow [Microsoft's WSL installation instructions](https://learn.microsoft.com/en-us/windows/wsl/install)
to install Ubuntu under WSL2, then use the Linux commands inside Ubuntu.
Keep the source and run directory in WSL's Linux filesystem for performance;
see [Microsoft's filesystem guidance](https://learn.microsoft.com/en-us/windows/wsl/filesystems).
This produces a Linux executable, rather than a native Windows application.

## Install without sudo

Installation is optional; an absolute executable path works directly.
To install into your account:

```sh
cmake --install build --prefix "$HOME/.local"
export PATH="$HOME/.local/bin:$PATH"
command -v findhap
```

On macOS use `build-macos` instead of `build`. Add the PATH line to your shell's
startup file if needed. Check which `findhap` your shell finds before replacing
an existing installation.

## Check the build

```sh
cd build
ctest --output-on-failure
cd ..
```

On macOS use `build-macos` instead of `build`.
With Python 3 available, this includes running the bundled example in both
modes and checking result hashes against saved original-Fortran outputs.
Tests use temporary run directories and leave the example inputs intact.

## Run directory and storage

Run in the directory containing the original input and options files. Output
files are written there, so use a separate directory to retain earlier results.
TurboHap LM also creates temporary files on that directory's filesystem;
it requires writable, sufficiently large storage. The temporary files are
unlinked on creation and released when their descriptors close. `TMPDIR`
does not select their location.

Native CPU tuning (`-march=native`) makes the executable specific to the build
CPU's capabilities. Rebuild on another machine rather than assuming a copied
binary is compatible. Keep the matching C++ and OpenMP runtime libraries installed.
Use a fresh build directory when changing compiler. If CMake cannot find OpenMP,
check the selected compiler and its runtime installation.
