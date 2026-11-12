# Installing TurboHap

One executable, `findhap`, provides both TurboHap and TurboHap LM. Build it with CMake 3.16 or newer and a C++17 compiler with OpenMP. Python 3.11 or newer enables the bundled Python checks; it is not needed to run any of the installed programs. A GPU, CUDA and a Fortran compiler are not required.

| Platform | Build route |
| --- | --- |
| Linux ARM64 or Intel/AMD x86-64 | Native GCC, its OpenMP runtime and CMake. |
| macOS on Apple Silicon | Native Homebrew GCC, OpenMP and CMake. |
| Windows through WSL2 | Use the Linux instructions inside WSL2. This builds a Linux executable. |

TurboHap works on Apple Silicon Macs, Linux ARM64 and Intel/AMD x86-64 systems. Detailed benchmarking across these systems is ongoing. The instructions below target those native Linux and Apple Silicon builds. Native Windows is not supported.

## Get the source

Download the source ZIP from the
[v1.5.0 release page](https://github.com/adrian-greenneuron/TurboHap/releases/tag/v1.5.0)
and extract it, or clone the release tag:

```sh
git clone --branch v1.5.0 --depth 1 https://github.com/adrian-greenneuron/TurboHap.git
cd TurboHap
```

Run the build commands from the directory containing `CMakeLists.txt`. Keep the complete source tree, including `cmake/`, `src/`, `tests/` and `examples/`. Use a fresh build directory when updating or changing compiler, architecture or OpenMP runtime. CMake keeps options from older configurations in its cache.

## Linux

On Ubuntu or Debian, install GCC and CMake:

```sh
sudo apt update
sudo apt install build-essential cmake
```

Configure and build on the machine that will run the program:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=g++
cmake --build build --parallel 2
./build/findhap --help
./build/turbohap-build-info --version
```

The compiler targets the build CPU. The build selects supported ARM64 NEON or x86 vector code and recognised native CPU profiles automatically, with fallback paths for other targets. No processor-specific flags are needed for normal native builds. Rebuild for a different processor rather than assuming a copied binary is compatible. `--parallel 2` controls compilation only; it does not set the number of runtime threads.

## macOS, Apple Silicon

Use a native terminal and [native Homebrew](https://docs.brew.sh/Installation), outside Rosetta. `uname -m` should print `arm64`. Install [Apple's Command Line Tools](https://developer.apple.com/documentation/xcode/installing-the-command-line-tools) if needed:

```sh
xcode-select --install
```

Install GCC, the LLVM OpenMP runtime and CMake:

```sh
brew install gcc libomp cmake python
```

Choose the versioned GNU C++ compiler from the Homebrew GCC installation. The system `/usr/bin/g++` invokes Apple Clang instead. From the source directory:

```sh
for turbohap_cxx in "$(brew --prefix gcc)"/bin/g++-[0-9]*; do
  test -x "$turbohap_cxx" && break
done
test -x "$turbohap_cxx" || { echo "Homebrew GNU C++ compiler not found"; exit 1; }
cmake -S . -B build-macos -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_CXX_COMPILER="$turbohap_cxx"
cmake --build build-macos --parallel 2
./build-macos/findhap --help
./build-macos/turbohap-build-info --version
```

CMake selects the applicable CPU profile and OpenMP library. It can use Homebrew `libomp` for the detected M4 Pro profile, while GCC's usual OpenMP runtime can be selected on other Macs. Keep the compiler and selected runtime installed. Use `otool -L build-macos/findhap` to check the linked libraries. No M4-specific options are needed for ordinary native Apple Silicon builds. See the [Mac guide](../MACOS.md) for thread selection and the separate optional M4 Pro build.

## Check the build

From the source directory:

```sh
(cd build && ctest --output-on-failure --parallel 1)
```

On macOS use `build-macos` instead of `build`. With Python 3.11 or newer available when you configure, CTest includes a small example that compares both modes with saved Fortran output checksums. Optional tests may require a Fortran compiler or an independent reference executable. An omitted or skipped test is not a pass. To run just the example:

```sh
python3 tests/check_example.py --binary build/findhap
```

Use `--binary build-macos/findhap` for the ordinary Mac build. If you only need the three programs and no test targets, configure with `-DBUILD_TESTING=OFF`.

## Validate inputs and run

Start from a writable working directory containing `findhap.options`, `pedigree.file`, `chromosome.data` and `genotypes.txt`. Use separate run directories to retain different result sets. A standalone input audit is optional; normal inference still uses strict input validation.

```sh
cd /path/to/run-directory
/absolute/path/to/build/turbohap-validate-inputs --directory .
/absolute/path/to/build/findhap 10
# Or run LM in its own working directory:
/absolute/path/to/build/findhap 10 --low-memory
```

The number is the requested CPU thread count. Omit it to use the applicable OpenMP or platform default; `findhap --cpu-info` reports the proposed CPU policy. Legacy FindHap-style console messages are the release default. Add `--stdout=modern` for UTC timestamps, elapsed progress and live haplotype capacity summaries. It does not change result files. See [input and runtime options](USAGE.md) and the [input-validation guide](INPUT_VALIDATION.md).

Allow space for the result files and any LM scratch files. LM uses temporary files on the run directory's filesystem; `TMPDIR` does not choose the location. Use disk-backed storage when choosing LM to reduce RAM usage.

## Install into your account

You can use the build executables directly or install them:

```sh
cmake --install build --prefix "$HOME/.local"
export PATH="$HOME/.local/bin:$PATH"
command -v findhap
turbohap-build-info --version
```

On macOS use `build-macos` instead of `build`. Check the `findhap` path if you also have original FindHap installed. `turbohap-build-info --build-info` records the compiler, runtime and built executable identity without reading any dataset.
