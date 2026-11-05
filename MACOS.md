# TurboHap on macOS

TurboHap v1.4.0 supports native Apple Silicon Macs using the same source tree,
input files and commands as the Linux release. Detailed platform benchmarking
is ongoing. No GPU or Fortran compiler is required.

## Build a native executable

Follow the [Apple Silicon installation instructions](docs/INSTALL.md#macos-apple-silicon).
Use native ARM64 Homebrew, install `gcc`, `libomp` and `cmake`, then configure
a fresh build with Homebrew's versioned GNU C++ executable. CMake discovers
the applicable CPU profile and OpenMP runtime. The native executable-validated
route uses GCC 16.2 with libomp. Apple Clang configuration and Intel Mac
builds have not received the same native executable validation.

Keep the complete source package, including the modules in `cmake/`.
They select the Mac defaults, configure temporary-buffer ownership and carry
the chosen compiler/runtime into the test fixtures. Build with
`cmake --build build-macos --parallel 2`, then run the installation checks:

```sh
cd build-macos
ctest --output-on-failure --parallel 1
cd ..
```

The included example checks both standard and LM outputs against saved
Fortran hashes. Linux-only tests are reported as skipped on macOS where
appropriate. Ordinary builds need no training data or profile-guided
optimization step.

## Run standard or LM mode

From the directory containing the four normal FindHap input files:

```sh
/absolute/path/to/build-macos/findhap 8
/absolute/path/to/build-macos/findhap 8 --low-memory
```

The example count of eight is not a requirement or a compiled-in Mac limit.
Choose a thread count for your machine, or omit it to let automatic mode use
the discovered performance-core count. `OMP_NUM_THREADS` and an explicit
positional count take precedence over that default.

TurboHap requests user-initiated quality of service for its workers when
automatic mode is active and caller placement settings are absent. This is
a scheduling hint: macOS decides which physical cores execute the work.
Workers are not pinned to named performance cores, and the program does not
reserve cores against other applications. Explicit OpenMP placement and wait
settings are respected. `--cpu-policy=inherit` leaves scheduling intent and
the default thread count to the caller and OpenMP runtime.

`findhap --cpu-info` reports the discovered topology and proposed settings as
JSON without loading inputs or writing result files. The Linux strict-affinity
`--cpu-policy=performance` option is unavailable on macOS. See
[CPU placement and row scheduling](docs/USAGE.md#cpu-placement) for all options.

## Memory and storage defaults

Fresh native ARM64 macOS builds use the Apple Silicon temporary-buffer policy
automatically across M-series families. It does not depend on identifying the
machine as an M4 Pro. The native CPU instruction profile is selected separately.
Cross/universal builds and Intel Macs retain their existing allocation defaults.

Standard mode retains its hot working genotypes in native memory and reuses
bounded storage across processing steps. LM uses bounded buffers and temporary
disk storage. Large temporary phase mappings are released at their existing
processing boundaries; normal output writing, error checks and LM scratch
synchronization remain enabled. Both modes preserve the same result-file
contract described in [output compatibility](docs/COMPATIBILITY.md).

The run directory must have space for all enabled outputs and LM temporary
files. Temporary files are created on the run directory's filesystem, not
the filesystem named by `TMPDIR`. They are unlinked after creation and released
when closed. Use a separate directory for each result set.

## Existing builds and optional overrides

Use a fresh build directory when moving to this release. Existing CMake
directories preserve cached settings, including an explicit allocation-policy
OFF. They are not automatically migrated.

To deliberately retain the earlier allocation policy, append
`-DFINDHAP_MMAP_PHASE_BUFFERS=OFF` to the configure command in the installation
guide and use a new build directory. If reusing a directory that already cached
the newer GNU link-time partition, also set
`-DFINDHAP_GNU_LTO_PARTITION=1to1`. Fresh default native Apple Silicon builds
select their applicable GNU partition automatically. Normal use needs neither
flag.

Build on the Mac that will run TurboHap. A natively tuned executable must not
be assumed portable to another Apple CPU or a different installed runtime.
Keep GCC and its selected OpenMP runtime installed after building.

## Input audit and build identity

The normal build includes `findhap`, `turbohap-validate-inputs` and `turbohap-build-info`. None needs Python at runtime. After building, run the input auditor with `--directory /path/to/inputs` and `turbohap-build-info --build-info` to record the compiled inference identity. On macOS use the corresponding executables in `build-macos/`. See the [input-validation guide](docs/INPUT_VALIDATION.md) for reports, coverage, privacy and exit codes.

The regression suite has optional tests requiring an independent reference executable or a Fortran compiler. CTest explicitly marks unavailable optional tests as skipped; a skipped test is not a pass.
