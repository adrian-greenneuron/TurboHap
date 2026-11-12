# TurboHap on Apple Silicon

TurboHap v1.5.0 uses the same source, input files and commands on Apple Silicon and Linux. Build natively on the Mac that will run it. TurboHap uses the CPU; it does not require a GPU or a Fortran compiler.

## Ordinary build for any Apple Silicon Mac

Start with the [Apple Silicon installation instructions](docs/INSTALL.md#macos-apple-silicon). They use native Homebrew GCC, CMake and OpenMP. Use a fresh `build-macos` directory and keep the selected OpenMP runtime installed after building. No compiler profile or extra training step is needed.

Fresh native ARM64 macOS builds use the Apple Silicon memory policy and the ARM64 NEON final matching scan automatically. Both apply across Apple Silicon families. The build also detects the existing M4 Pro CPU profile where applicable; that profile and the optional build below are separate from the shared ARM64 features. Do not copy a natively tuned executable to another Mac without rebuilding and checking it there.

## Run standard or low-memory mode

Run from a writable directory containing `findhap.options`, `pedigree.file`, `chromosome.data` and `genotypes.txt`:

```sh
/absolute/path/to/build-macos/findhap 8
# Or use TurboHap LM in a separate run directory:
/absolute/path/to/build-macos/findhap 8 --low-memory
```

Eight threads is an example. If neither the positional count nor `OMP_NUM_THREADS` is set, automatic mode uses the discovered performance-core count. TurboHap can request higher scheduling priority, but macOS decides where the threads run. It does not pin threads to specific cores or reserve them from other processes. Explicit thread counts and caller settings take precedence. Use `findhap --cpu-info` to inspect the proposed policy without loading data; see [CPU placement and row scheduling](docs/USAGE.md#cpu-placement).

Legacy console messages are the release default. Add `--stdout=modern` for timestamped progress and live haplotype library capacity summaries. This choice does not change the result files. LM also needs temporary storage on the run directory's filesystem, so use a disk-backed location with sufficient free space.

## Optional M4 Pro profile with Apple Clang

The ordinary build is the starting point. The optional M4 Pro profile uses an Apple Clang toolchain that recognises the `apple-m4` CPU target and Homebrew `libomp`. This build is specifically for native Apple Silicon M4 Pro, and it does not automatically activate on any Mac. Use a separate build directory:

```sh
brew install cmake libomp
cmake -S . -B build-macos-m4 -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_CXX_COMPILER=/usr/bin/clang++ \
  -DCMAKE_OSX_SYSROOT="$(xcrun --sdk macosx --show-sdk-path)" \
  -DOpenMP_ROOT="$(brew --prefix libomp)" \
  -DFINDHAP_MAC_COHORT_PROFILE=ON
cmake --build build-macos-m4 --parallel 2
./build-macos-m4/turbohap-build-info --build-info
otool -L ./build-macos-m4/findhap
```

The optional profile links `libomp` as its only OpenMP runtime and needs no training step. Use `--row-schedule=dynamic` explicitly to select the matching runtime schedule; this is independent of the build option. Check the bundled example before using the new build on your data. Use the ordinary build on other Apple Silicon processors.

```sh
# From a directory containing the four input files:
/absolute/path/to/build-macos-m4/findhap 8 --row-schedule=dynamic
```

Add `--low-memory` for LM and `--stdout=modern` if you want timestamped progress. Neither switch is required to build the profile.

## Input audit and build checks

`build-macos/turbohap-validate-inputs --directory /path/to/inputs` checks the input files without running imputation. See [input validation](docs/INPUT_VALIDATION.md) for reports and exit status. To check a build, run `ctest --output-on-failure --parallel 1` from its build directory, or use the [bundled example check](docs/INSTALL.md#check-the-build). Optional reference tests may be skipped when their tools are absent.
