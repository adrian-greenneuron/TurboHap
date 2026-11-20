# TurboHap on Apple Silicon

Build TurboHap on the Mac where you will use it. The installer detects your M-series generation, builds both inference modes and checks a small dataset before making the programs available.

## Install

1. Open Terminal. Install Apple’s Command Line Tools with `xcode-select --install` if they are missing, and complete the macOS installer.
2. Install native [Homebrew](https://brew.sh/) if you do not already have it. A native Terminal reports `arm64` for `uname -m`.
3. Open the extracted TurboHap source directory in Terminal and run:

   ```sh
   sh tools/install-macos.sh
   ```

The installer obtains CMake and libomp from Homebrew as needed, uses Apple Clang, and checks the ten example outputs in Standard and LM at one, two and four threads. After the check succeeds, your programs are under `~/Applications/TurboHap/bin/`. Keep Homebrew’s libomp installed. If this path already holds an unrelated installation, move it before installing.

## Run

From a writable directory containing your four FindHap inputs:

```sh
~/Applications/TurboHap/bin/turbohap-validate-inputs --directory .
~/Applications/TurboHap/bin/findhap --stdout=modern > run.log 2> diagnostics.log
```

The second command runs Standard with the default genome-wide analysis. Use `--low-memory` for LM, and a separate working directory when keeping another run’s results. LM needs scratch space on the run directory’s filesystem. [Working-space estimates](docs/INSTALL.md#storage-for-lm)

If you omit the thread count, automatic mode uses the performance-core count unless you set `OMP_NUM_THREADS`. To request eight threads, use `~/Applications/TurboHap/bin/findhap 8`. macOS decides where those threads run. Use `~/Applications/TurboHap/bin/findhap --cpu-info` to inspect the proposed policy before loading any data.

## What the native build selects

TurboHap automatically chooses settings for your M-series generation. If your compiler does not recognise that generation, the installer tries a compatible target. [Processor generations and how to check your build](docs/PROCESSORS.md)

To inspect your installed compiler, architecture, OpenMP link and selected engines:

```sh
~/Applications/TurboHap/bin/turbohap-build-info --build-info
```

## Manual and portable builds

For a manual native build, from a fresh source directory:

```sh
brew install cmake libomp
cmake -S . -B build-macos -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_CXX_COMPILER=/usr/bin/clang++ \
  -DOpenMP_ROOT="$(brew --prefix libomp)"
cmake --build build-macos --parallel 3
python3 examples/check_installation.py --binary build-macos/findhap
```

The last command checks the ten example outputs in both modes at one, two and four threads. To distribute one binary across Apple Silicon generations, configure a fresh directory with `-DFINDHAP_PORTABLE_BUILD=ON -DCMAKE_OSX_ARCHITECTURES=arm64`. Match the deployment target and arm64 OpenMP library to the oldest macOS version you intend to support.
