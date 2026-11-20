# TurboHap 1.5.0-modified.2 (unofficial modified build of TurboHap 1.5.0) - container image
#
# Build (from this source folder):
#   docker build --build-arg MARCH=x86-64-v3 -t turbohap:1.5.0-modified.2 .
#
# Run (settings from environment variables, see docker/turbohap-run):
#   docker run --rm -v "$RUNNING_DIR":/home/galaxy/data \
#       -e FINDHAP_DIR=/home/galaxy/data -e OMP_NUM_THREADS=16 \
#       -e fgeno=genotypes.txt -e fgenout=genotypes.filled turbohap:1.5.0-modified.2
# Check the inputs only (no inference):
#   docker run --rm -v "$RUNNING_DIR":/data -e FINDHAP_DIR=/data \
#       -e FINDHAP_VALIDATE_ONLY=1 turbohap:1.5.0-modified.2
# Add --user "$(id -u):$(id -g)" so output files belong to you, not root.
#
# Base image: Ubuntu 20.04 by default. It works with older Docker versions
# too (before 20.10.10 they block the clone3 call used by glibc 2.34+, which
# breaks apt and thread creation in newer bases). With a recent Docker,
# --build-arg UBUNTU_VERSION=24.04 uses a newer compiler.
#
# CPU: TurboHap compiles with -march=native, so by default the image runs
# only on CPUs like the one that built it. For an image that moves between
# machines choose a portable target: MARCH=x86-64-v3 (Intel Haswell / AMD Zen
# and newer; translated to "haswell" for GCC 9/10) or x86-64-v2 (older).
# A native build on the target machine can use more CPU-specific paths.
#
# STATIC=1 links glibc, libstdc++ and libgomp statically into findhap.
#
# Licence: TurboHap is free for non-commercial academic research and teaching
# only; commercial use requires a paid licence from Green Neuron AI. This is
# an unofficial modified build: see MODIFICATIONS.md. Licence and notices are
# installed in /usr/local/share/doc/turbohap/.

ARG UBUNTU_VERSION=20.04

############################ build stage ############################
FROM ubuntu:${UBUNTU_VERSION} AS build

ARG MARCH=native
ARG BUILD_JOBS=2
ARG STATIC=0
ARG DEBIAN_FRONTEND=noninteractive

RUN apt-get update \
 && apt-get install -y --no-install-recommends \
        g++ cmake make python3 ca-certificates \
 && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY . .

# The CPU target goes into TurboHap's own FINDHAP_RELEASE_FLAGS (its published
# flags with -march changed). GCC before 11 does not know the x86-64-v2/v3/v4
# names; use the equivalent CPU names there (nehalem / haswell / skylake-avx512).
RUN target="$MARCH"; \
    if ! echo | g++ -march="$target" -x c++ -E - >/dev/null 2>&1; then \
        case "$target" in \
            x86-64-v2) target=nehalem ;; \
            x86-64-v3) target=haswell ;; \
            x86-64-v4) target=skylake-avx512 ;; \
            *) echo "g++ $(g++ -dumpversion) does not accept -march=$target" >&2; exit 1 ;; \
        esac; \
        echo "g++ $(g++ -dumpversion) does not know -march=$MARCH; using the equivalent -march=$target"; \
    fi; \
    static_flags=""; link_flags=""; \
    if [ "$STATIC" = "1" ]; then \
        static_flags="-DOpenMP_gomp_LIBRARY=$(g++ -print-file-name=libgomp.a) -DOpenMP_pthread_LIBRARY=$(g++ -print-file-name=libpthread.a) -DOpenMP_dl_LIBRARY=$(g++ -print-file-name=libdl.a)"; \
        link_flags="-static -Wl,--whole-archive -lpthread -Wl,--no-whole-archive"; \
    fi; \
    cmake -S . -B build \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_CXX_COMPILER=g++ \
        -DBUILD_TESTING=OFF \
        "-DFINDHAP_RELEASE_FLAGS=-O3 -march=${target} -funroll-loops -flto -ffp-contract=off" \
        "-DCMAKE_EXE_LINKER_FLAGS=$link_flags" \
        $static_flags \
 && cmake --build build --parallel "${BUILD_JOBS}" \
 && cmake --install build --prefix /opt/turbohap \
 && install -m 755 docker/turbohap-run /opt/turbohap/bin/turbohap-run \
 && install -m 644 MODIFICATIONS.md /opt/turbohap/share/doc/turbohap/MODIFICATIONS.md

# Both modes, 1 and 4 threads, byte-for-byte against the saved original
# Fortran outputs. The image build stops if anything differs.
RUN LC_ALL=C OMP_DYNAMIC=FALSE python3 tests/check_example.py --binary /opt/turbohap/bin/findhap \
 && /opt/turbohap/bin/turbohap-build-info --build-info

########################### runtime stage ###########################
FROM ubuntu:${UBUNTU_VERSION}

LABEL org.opencontainers.image.title="TurboHap 1.5.0-modified.2 (unofficial modified build of TurboHap 1.5.0)" \
      org.opencontainers.image.description="FindHap v3 reimplemented in C++ (genotype imputation and haplotype phasing)" \
      org.opencontainers.image.source="https://github.com/adrian-greenneuron/TurboHap" \
      org.opencontainers.image.licenses="LicenseRef-TurboHap-NonCommercial-Academic"

ARG DEBIAN_FRONTEND=noninteractive
RUN apt-get update \
 && apt-get install -y --no-install-recommends libgomp1 libstdc++6 \
 && apt-get clean \
 && rm -rf /var/lib/apt/lists/* /var/cache/apt/*.bin

COPY --from=build /opt/turbohap /usr/local

ENV LC_ALL=C \
    OMP_DYNAMIC=FALSE

WORKDIR /data
ENTRYPOINT ["turbohap-run"]
