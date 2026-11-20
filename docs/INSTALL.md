# Installing TurboHap

The build installs three programs. `findhap` runs Standard or low-memory (LM) inference, `turbohap-validate-inputs` audits your input files, and `turbohap-build-info` identifies the compiler and processor settings in your executable. All three run on the CPU. They require neither Python nor a Fortran compiler for routine use.

Download the v1.6.0 source ZIP from the release’s **Assets**, extract it and open the folder containing `CMakeLists.txt`. Keep the `cmake/`, `src/`, `tools/` and `examples/` directories together. Use a fresh build directory when upgrading or changing CPUs, compilers or OpenMP runtimes. Native builds target the machine on which you compile them.

## Linux

Install CMake 3.16 or newer, a C++17 compiler and OpenMP. On Debian or Ubuntu, these commands build the programs and check the bundled example. Python 3 is used for the example check.

```sh
sudo apt update
sudo apt install build-essential cmake python3
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 2
./build/turbohap-build-info --version
python3 examples/check_installation.py
```

`--parallel 2` here controls compilation. Choose the number of inference threads when running `findhap`. Other Linux distributions can use their equivalent packages. Windows users can follow the Linux instructions under WSL2. Native Windows builds are unsupported.

On a native DGX Spark, a fresh build selects `g++-13` when available. If it is absent, configuration tries Clang with LLVM OpenMP and then the system GNU C++ compiler. An explicit compiler or toolchain takes precedence. The release benchmarks used GNU C++ 13.3.0 with libgomp. To reproduce that compiler choice on a machine with the same toolchain installed, configure a new directory explicitly:

```sh
g++-13 --version
cmake -S . -B build-gcc13 -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=g++-13
cmake --build build-gcc13 --parallel 2
./build-gcc13/turbohap-build-info --build-info
```

## macOS, Apple Silicon

Install Apple’s Command Line Tools and native Homebrew. From the extracted source folder run:

```sh
sh tools/install-macos.sh
```

The installer builds with Apple Clang and Homebrew’s OpenMP runtime, checks both modes, and makes the programs available under `~/Applications/TurboHap/bin/`. [Mac installation and troubleshooting](../MACOS.md)

## Processor support

Build on the machine that will run the analysis so TurboHap can select settings for its processor. The [illustrated processor guide](PROCESSORS.md) lists the optimised generations, explains the fallback for an unfamiliar CPU and shows how to inspect your build.

To share one executable between machines of the **same architecture**, use a portable build in a fresh directory.

```sh
cmake -S . -B build-portable -DCMAKE_BUILD_TYPE=Release -DFINDHAP_PORTABLE_BUILD=ON
cmake --build build-portable --parallel 2
./build-portable/turbohap-build-info --build-info
python3 examples/check_installation.py --binary build-portable/findhap
```

On each destination, run the last two commands from the extracted source folder, using the paths to the executables you copied. The first prints the compiler, OpenMP runtime and processor settings. The second checks whether the portable build reproduces the example on that computer.

## Check the build

The installation check runs the bundled USDA example in Standard and LM at one, two and four threads. It checks all ten outputs from each run against their original FindHap sizes and SHA-256 digests, giving 60 comparisons. To check an executable in another location, supply its path with `--binary`. [Worked example](../examples/README.md)

## Install into your account

You can run the programs directly under `build/` or install them in your home directory:

```sh
cmake --install build --prefix "$HOME/.local"
export PATH="$HOME/.local/bin:$PATH"
command -v findhap
turbohap-build-info --version
```

Check the path if you already have the original FindHap executable installed.

## Validate inputs and run

In a writable directory containing `findhap.options`, `pedigree.file`, `chromosome.data` and `genotypes.txt`, run:

```sh
/absolute/path/to/build/turbohap-validate-inputs --directory .
/absolute/path/to/build/findhap 10
```

To use LM, add `--low-memory`. To save progress, add `--stdout=modern > run.log 2> diagnostics.log`. Use separate directories when keeping more than one result set. [Options and science settings](USAGE.md) · [Understanding the audit report](INPUT_VALIDATION.md)

## Storage for LM

Allow space for final output and the two scratch areas on the **run directory’s filesystem**. Setting `TMPDIR` elsewhere does not redirect them. Use a disk-backed filesystem, preferably a local SSD.

For normally packed data the raw genotype cache needs about one byte per four recorded loci, summed over chip widths. The expanded workspace needs another byte per four full-map loci per genotype record, rounded at segment boundaries. For example, one million records with 80,000 assayed loci each need about 40 GB (37 GiB) for those two areas **before** rounding and output files. Other accepted genotype codes can require one byte per recorded locus in the raw cache. Overlapping segments in imported prior libraries can use an in-memory workspace.
