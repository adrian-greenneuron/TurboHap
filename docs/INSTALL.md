# Installing TurboHap

TurboHap and TurboHap LM are included in one CPU executable, `findhap`.
A GPU, CUDA, Python and a Fortran compiler are not required to build or run it.
Python 3 is optional for the example check; the complete Linux test suite
requires Python 3.11 or newer.

## Requirements and platforms

Use CMake 3.16 or newer and a C++17 compiler with OpenMP and its runtime library.
TurboHap works on **Apple Silicon Macs, Linux ARM64, and Intel/AMD x86-64
systems**. Detailed platform benchmarking is ongoing.

| Platform | Build route |
| --- | --- |
| Linux ARM64 or Intel/AMD x86-64 | GCC and CMake; GCC 13.3 is the published Linux comparison toolchain. |
| macOS, Apple Silicon | Native Homebrew GCC, OpenMP and CMake, as described below. |
| macOS, Intel | CMake supports native GCC builds; this release does not claim native Intel Mac validation. |
| Windows through WSL2 | Use the Linux instructions inside Ubuntu; this route has not received independent Windows-host validation. |
| Native Windows | Not supported; the source uses POSIX file APIs. |

## Get the source

Download the [v1.3.0 source ZIP](https://github.com/adrian-greenneuron/TurboHap/archive/refs/tags/v1.3.0.zip)
and extract it, or select the release with Git:

```sh
git clone --branch v1.3.0 --depth 1 https://github.com/adrian-greenneuron/TurboHap.git
cd TurboHap
```

Run the following commands from the directory containing `CMakeLists.txt`.
Keep the complete extracted source tree, including `cmake/`, `src/`, `tests/`
and `examples/`; the build modules and checks must not be copied separately.

## Linux

On Ubuntu/Debian, install the build tools:

```sh
sudo apt update
sudo apt install build-essential cmake
```

Configure and build in a fresh directory:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=g++
cmake --build build --parallel 2
./build/findhap --help
./build/findhap --cpu-info
```

The executable is `build/findhap`. Compilation parallelism does not set the
runtime thread count. To build only the executable, add `-DBUILD_TESTING=OFF`
at configure time. Native CPU tuning is selected automatically; build on the
machine that will run the program.

## macOS, Apple Silicon

Use a terminal and Homebrew installation native to the Mac's ARM64 architecture,
rather than an Intel installation running under Rosetta. `uname -m` should
print `arm64`. Install Apple's Command Line Tools if needed:

```sh
xcode-select --install
```

Install [Homebrew](https://docs.brew.sh/Installation), then the build tools:

```sh
brew install gcc libomp cmake
```

Select Homebrew's versioned GNU C++ compiler explicitly. `/usr/bin/g++` is
Apple Clang and does not select Homebrew GCC. The loop below finds the GNU
compiler supplied by the installed Homebrew `gcc` formula:

```sh
for turbohap_cxx in "$(brew --prefix gcc)"/bin/g++-[0-9]*; do
  test -x "$turbohap_cxx" && break
done
test -x "$turbohap_cxx" || { echo "Homebrew GNU C++ compiler not found"; exit 1; }
"$turbohap_cxx" --version
cmake -S . -B build-macos -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_CXX_COMPILER="$turbohap_cxx"
cmake --build build-macos --parallel 2
./build-macos/findhap --help
./build-macos/findhap --cpu-info
```

CMake selects the native CPU profile and OpenMP runtime. The validated native
Mac build route uses GCC 16.2 with libomp; explicit OpenMP settings take
precedence over automatic discovery. Keep the selected compiler and runtime
installed. Apple Clang configuration is supported, but it is not the
executable-validated build route documented here.

Fresh native Apple Silicon builds select the Mac allocation policy
automatically on all M-series families. No M4-specific setting or manual core
count is required. Different CPU instruction profiles are selected only when
recognized; other Macs retain their applicable conservative paths. These
ordinary build commands do not train or require profile-guided optimization.

When neither a positional thread count nor `OMP_NUM_THREADS` is supplied,
automatic mode uses the discovered performance-core count. macOS controls
actual placement; TurboHap requests scheduling priority without pinning workers
to particular cores. Explicit thread counts remain supported. See the
[Mac guide](../MACOS.md) and [CPU placement reference](USAGE.md#cpu-placement).

## Windows through WSL2

Follow [Microsoft's WSL installation instructions](https://learn.microsoft.com/en-us/windows/wsl/install)
to install Ubuntu under WSL2, then use the Linux commands inside Ubuntu.
Keep the source and run directory in WSL's Linux filesystem; see
[Microsoft's filesystem guidance](https://learn.microsoft.com/en-us/windows/wsl/filesystems).
This produces a Linux executable rather than a native Windows application.

## Check the build

From the source directory:

```sh
cd build
ctest --output-on-failure --parallel 1
cd ..
```

Use `build-macos` instead of `build` on macOS. With a suitable Python interpreter
available, the suite includes running the bundled example in both modes and
checking result hashes against saved original-Fortran outputs. Tests use
temporary run directories and leave the example inputs intact. A
platform-specific skipped test is reported separately from a passing test.

To run just the example check:

```sh
python3 tests/check_example.py --binary build/findhap
```

On macOS, use `--binary build-macos/findhap`.

## Run and install

Run the absolute executable path from a directory containing
`findhap.options`, `pedigree.file`, `chromosome.data` and `genotypes.txt`:

```sh
/absolute/path/to/findhap 10
/absolute/path/to/findhap 10 --low-memory
```

Both modes write the original output filenames. Use a separate run directory
when retaining earlier results. See [inputs and options](USAGE.md) and the
[small example](../examples/README.md).

Installation is optional. To install into your account:

```sh
cmake --install build --prefix "$HOME/.local"
export PATH="$HOME/.local/bin:$PATH"
command -v findhap
```

Use `build-macos` on macOS. Add the PATH line to your shell's startup file if
needed. Check which `findhap` your shell finds before replacing an existing
installation.

## Storage and rebuilding

The run directory must be writable and have room for the enabled output files.
TurboHap LM also creates temporary files on that directory's filesystem.
They are unlinked on creation and released when their descriptors close;
`TMPDIR` does not select their location. Use disk-backed storage when the aim
is to reduce RAM consumption.

Native CPU tuning makes the executable specific to the build CPU's
capabilities. Rebuild on a different machine rather than assuming a copied
binary is compatible. Use a fresh build directory when changing compiler,
architecture or OpenMP runtime. Existing CMake directories retain cached
settings; they are not forcibly migrated to new defaults. If OpenMP discovery
fails, check the selected compiler and its runtime installation before adding
manual flags.
