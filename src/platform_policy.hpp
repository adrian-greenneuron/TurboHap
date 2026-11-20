#pragma once
#include "macos_build_profile.hpp"
// Standalone and explicitly targeted builds retain the compiler model. A native
// qualification build may supply a CPUID identity after probing the real kernels.
#if defined(__graniterapids__) || defined(FINDHAP_PROBED_GRANITE)
#define TURBOHAP_TARGET_GRANITE 1
#else
#define TURBOHAP_TARGET_GRANITE 0
#endif
#if defined(__cascadelake__) || defined(FINDHAP_PROBED_CASCADE)
#define TURBOHAP_TARGET_CASCADE 1
#else
#define TURBOHAP_TARGET_CASCADE 0
#endif
#if defined(__znver4__) || defined(__znver5__) || defined(FINDHAP_PROBED_TURIN)
#define TURBOHAP_TARGET_ZEN45 1
#else
#define TURBOHAP_TARGET_ZEN45 0
#endif
#if defined(__znver3__) || defined(FINDHAP_PROBED_MILAN)
#define TURBOHAP_TARGET_MILAN 1
#else
#define TURBOHAP_TARGET_MILAN 0
#endif
#ifndef FINDHAP_WORKLOAD_OPTIMISATIONS
#define FINDHAP_WORKLOAD_OPTIMISATIONS 0
#endif
// Keep the existing Spark opt-in independent of the qualified Intel profile.
#if FINDHAP_WORKLOAD_OPTIMISATIONS && defined(__linux__) && defined(__ARM_NEON) && defined(FINDHAP_NATIVE_GB10) && FINDHAP_NATIVE_GB10
#define TURBOHAP_NEON_WORKLOAD_PROFILE 1
#else
#define TURBOHAP_NEON_WORKLOAD_PROFILE 0
#endif
// The qualified Intel server targets share matching and storage kernels. Enable them only
// when the toolchain exposes every instruction required by the wide profile.
#if defined(__linux__) && defined(__x86_64__) && (defined(__sapphirerapids__) || defined(FINDHAP_PROBED_SAPPHIRE)) && \
    !defined(FINDHAP_PROBED_EMERALD) && !defined(__emeraldrapids__) && \
    defined(__AVX2__) && defined(__AVX512F__) && defined(__AVX512BW__) && defined(__AVX512VBMI__) && defined(__BMI2__)
#define TURBOHAP_NATIVE_SAPPHIRE_RAPIDS 1
#else
#define TURBOHAP_NATIVE_SAPPHIRE_RAPIDS 0
#endif
#if defined(__linux__) && defined(__x86_64__) && (defined(__emeraldrapids__) || defined(FINDHAP_PROBED_EMERALD)) && \
    defined(__AVX2__) && defined(__AVX512F__) && defined(__AVX512BW__) && defined(__AVX512VBMI__) && defined(__BMI2__)
#define TURBOHAP_NATIVE_EMERALD_RAPIDS 1
#else
#define TURBOHAP_NATIVE_EMERALD_RAPIDS 0
#endif
#define TURBOHAP_NATIVE_SAPPHIRE_OR_EMERALD (TURBOHAP_NATIVE_SAPPHIRE_RAPIDS || TURBOHAP_NATIVE_EMERALD_RAPIDS)
#if defined(__linux__) && defined(__x86_64__) && (defined(__icelake_server__) || defined(FINDHAP_PROBED_ICE)) && \
    defined(__AVX2__) && defined(__AVX512F__) && defined(__AVX512BW__) && defined(__AVX512VBMI__) && defined(__BMI2__)
#define TURBOHAP_NATIVE_ICE_LAKE 1
#else
#define TURBOHAP_NATIVE_ICE_LAKE 0
#endif
#if TURBOHAP_NATIVE_ICE_LAKE || TURBOHAP_NATIVE_SAPPHIRE_OR_EMERALD || (defined(__linux__) && defined(__x86_64__) && TURBOHAP_TARGET_GRANITE && \
    defined(__AVX2__) && defined(__AVX512F__) && defined(__AVX512BW__) && defined(__AVX512VBMI__) && defined(__BMI2__))
#define TURBOHAP_NATIVE_INTEL_WORKLOAD_PROFILE 1
#else
#define TURBOHAP_NATIVE_INTEL_WORKLOAD_PROFILE 0
#endif
#if defined(__linux__) && defined(__x86_64__) && TURBOHAP_TARGET_CASCADE && \
    defined(__AVX2__) && defined(__BMI2__) && defined(__AVX512F__) && defined(__AVX512BW__)
#define TURBOHAP_NATIVE_CASCADE_LAKE 1
#else
#define TURBOHAP_NATIVE_CASCADE_LAKE 0
#endif
// Zen 3 is the compiler target qualified on the AVX2-only EPYC Milan server.
#if defined(__linux__) && defined(__x86_64__) && TURBOHAP_TARGET_MILAN && \
    defined(__AVX2__) && defined(__BMI2__) && !defined(__AVX512F__)
#define TURBOHAP_NATIVE_MILAN 1
#else
#define TURBOHAP_NATIVE_MILAN 0
#endif
#ifndef FINDHAP_OVERLAP_CROSSOVERS
#define FINDHAP_OVERLAP_CROSSOVERS (TURBOHAP_NEON_WORKLOAD_PROFILE || TURBOHAP_NATIVE_INTEL_WORKLOAD_PROFILE || TURBOHAP_NATIVE_CASCADE_LAKE || TURBOHAP_NATIVE_AMD_WORKLOAD_PROFILE || TURBOHAP_NATIVE_MILAN)
#endif
#ifndef FINDHAP_GROUP_HAPFOUND
#define FINDHAP_GROUP_HAPFOUND (TURBOHAP_NEON_WORKLOAD_PROFILE || TURBOHAP_NATIVE_INTEL_WORKLOAD_PROFILE || TURBOHAP_NATIVE_CASCADE_LAKE || TURBOHAP_NATIVE_AMD_WORKLOAD_PROFILE || TURBOHAP_NATIVE_MILAN)
#endif
// GCC 13 uses the same znver4 target for several AMD processors. Only
// select Genoa after a native CPUID, OS vector-state and kernel build probe.
#if defined(FINDHAP_PROBED_GENOA) && defined(__linux__) && defined(__x86_64__) && \
    defined(__AVX2__) && defined(__AVX512F__) && defined(__AVX512BW__) && \
    defined(__AVX512VBMI__) && defined(__BMI2__)
#define TURBOHAP_NATIVE_GENOA 1
#else
#define TURBOHAP_NATIVE_GENOA 0
#endif
#if TURBOHAP_NATIVE_GENOA || (TURBOHAP_TARGET_ZEN45 && defined(__AVX512VP2INTERSECT__))
#define TURBOHAP_TARGET_EPYC_VBMI 1
#else
#define TURBOHAP_TARGET_EPYC_VBMI 0
#endif
#if defined(__linux__) && defined(__x86_64__) && TURBOHAP_TARGET_EPYC_VBMI && defined(__AVX2__) && defined(__AVX512F__) && defined(__AVX512BW__) && defined(__AVX512VBMI__) && defined(__BMI2__)
#define TURBOHAP_NATIVE_AMD_WORKLOAD_PROFILE 1
#else
#define TURBOHAP_NATIVE_AMD_WORKLOAD_PROFILE 0
#endif
#if TURBOHAP_NATIVE_INTEL_WORKLOAD_PROFILE || TURBOHAP_NATIVE_AMD_WORKLOAD_PROFILE || TURBOHAP_NATIVE_CASCADE_LAKE
#define TURBOHAP_NATIVE_X86_WORKLOAD_PROFILE 1
#else
#define TURBOHAP_NATIVE_X86_WORKLOAD_PROFILE 0
#endif
#if TURBOHAP_NEON_WORKLOAD_PROFILE || TURBOHAP_NATIVE_X86_WORKLOAD_PROFILE
#define TURBOHAP_WORKLOAD_PROFILE 1
#else
#define TURBOHAP_WORKLOAD_PROFILE 0
#endif
// 8 November: use the qualified demand-zero library metadata on native Linux
// Spark and qualified Intel/AMD in both engines, at every maxhap.
#ifndef FINDHAP_LIBRARY_METADATA
#if (defined(__linux__) && defined(__ARM_NEON) && defined(FINDHAP_NATIVE_GB10) && FINDHAP_NATIVE_GB10) || TURBOHAP_NATIVE_X86_WORKLOAD_PROFILE || TURBOHAP_NATIVE_MILAN
#define FINDHAP_LIBRARY_METADATA 1
#else
#define FINDHAP_LIBRARY_METADATA 0
#endif
#endif
#ifndef FINDHAP_METADATA_CALLOC
#define FINDHAP_METADATA_CALLOC 1
#endif
#ifndef FINDHAP_METADATA_CHUNK
#define FINDHAP_METADATA_CHUNK 1
#endif
#ifndef FINDHAP_PEDIGREE_VECTOR_BASE
#define FINDHAP_PEDIGREE_VECTOR_BASE (TURBOHAP_WORKLOAD_PROFILE || TURBOHAP_NATIVE_MILAN)
#endif
#ifndef FINDHAP_POP_FINAL_SCAN_NEON
// The exact final validation/fill scan needs ARM64 NEON, not a CPU model or OS.
// An explicit zero retains the scalar scan for differential qualification.
#if (defined(__aarch64__) || defined(__arm64__)) && defined(__ARM_NEON)
#define FINDHAP_POP_FINAL_SCAN_NEON 1
#else
#define FINDHAP_POP_FINAL_SCAN_NEON 0
#endif
#endif
#ifndef FINDHAP_POP_VECTOR_SEGMENT
#define FINDHAP_POP_VECTOR_SEGMENT TURBOHAP_WORKLOAD_PROFILE
#endif
#ifndef FINDHAP_POP_FINAL_SCAN_X86
#if TURBOHAP_NATIVE_INTEL_WORKLOAD_PROFILE
#define FINDHAP_POP_FINAL_SCAN_X86 3
#elif TURBOHAP_NATIVE_CASCADE_LAKE || TURBOHAP_NATIVE_AMD_WORKLOAD_PROFILE || TURBOHAP_NATIVE_MILAN
#define FINDHAP_POP_FINAL_SCAN_X86 2
#else
#define FINDHAP_POP_FINAL_SCAN_X86 0
#endif
#endif
#if FINDHAP_POP_FINAL_SCAN_X86 == 2 && !defined(__AVX2__)
#error "The 32-locus final population scan requires AVX2"
#elif FINDHAP_POP_FINAL_SCAN_X86 == 3 && (!defined(__AVX512F__) || !defined(__AVX512BW__))
#error "The 64-locus final population scan requires AVX-512F and AVX-512BW"
#elif FINDHAP_POP_FINAL_SCAN_X86 != 0 && FINDHAP_POP_FINAL_SCAN_X86 != 2 && FINDHAP_POP_FINAL_SCAN_X86 != 3
#error "FINDHAP_POP_FINAL_SCAN_X86 must be 0, 2 or 3"
#endif
#ifndef FINDHAP_POP_COMPRESS_SIGNATURE
#define FINDHAP_POP_COMPRESS_SIGNATURE TURBOHAP_NEON_WORKLOAD_PROFILE
#endif
#ifndef FINDHAP_POP_FUSED_VALIDITY
#if TURBOHAP_NATIVE_X86_WORKLOAD_PROFILE
#define FINDHAP_POP_FUSED_VALIDITY 1
#else
#define FINDHAP_POP_FUSED_VALIDITY TURBOHAP_NEON_WORKLOAD_PROFILE
#endif
#endif
#ifndef FINDHAP_PROGENY_SEGMENT_OWNED
#define FINDHAP_PROGENY_SEGMENT_OWNED TURBOHAP_WORKLOAD_PROFILE
#endif
#ifndef FINDHAP_COMPACT_PROGENY
#define FINDHAP_COMPACT_PROGENY 1
#endif
#ifndef FINDHAP_LIBRARY_ACTIVE_BOUND
#define FINDHAP_LIBRARY_ACTIVE_BOUND (TURBOHAP_WORKLOAD_PROFILE || TURBOHAP_NATIVE_MILAN)
#endif
#ifndef FINDHAP_PEDIGREE_NEON_NEAR
#define FINDHAP_PEDIGREE_NEON_NEAR TURBOHAP_NEON_WORKLOAD_PROFILE
#endif
// Native qualified x86 backends; explicit zero retains the earlier implementation.
// 2 selects the existing AVX2 conflict predicate, 3 selects AVX-512BW.
#ifndef FINDHAP_POP_CONFLICT_BACKEND
#if TURBOHAP_NATIVE_X86_WORKLOAD_PROFILE || TURBOHAP_NATIVE_MILAN
#define FINDHAP_POP_CONFLICT_BACKEND 2
#else
#define FINDHAP_POP_CONFLICT_BACKEND 0
#endif
#endif
#if FINDHAP_POP_CONFLICT_BACKEND != 0 && FINDHAP_POP_CONFLICT_BACKEND != 2 && FINDHAP_POP_CONFLICT_BACKEND != 3
#error "FINDHAP_POP_CONFLICT_BACKEND must be 0, 2 or 3"
#endif
#if FINDHAP_POP_CONFLICT_BACKEND == 2 && !defined(__AVX2__)
#error "AVX2 population conflicts require an AVX2 native target"
#endif
#if FINDHAP_POP_CONFLICT_BACKEND == 3 && !defined(__AVX512BW__)
#error "AVX-512 population conflicts require an AVX-512BW native target"
#endif
// Independently measured x86 ports. Other native profiles retain zero.
#ifndef FINDHAP_PEDIGREE_CONFLICT_BACKEND
#if TURBOHAP_NATIVE_CASCADE_LAKE || TURBOHAP_NATIVE_MILAN
#define FINDHAP_PEDIGREE_CONFLICT_BACKEND 2
#elif TURBOHAP_NATIVE_X86_WORKLOAD_PROFILE
#define FINDHAP_PEDIGREE_CONFLICT_BACKEND 3
#else
#define FINDHAP_PEDIGREE_CONFLICT_BACKEND 0
#endif
#endif
#ifndef FINDHAP_POP_SEGMENT_BACKEND
#if TURBOHAP_NATIVE_X86_WORKLOAD_PROFILE
#define FINDHAP_POP_SEGMENT_BACKEND 2
#else
#define FINDHAP_POP_SEGMENT_BACKEND 0
#endif
#endif
#ifndef FINDHAP_PEDIGREE_FREQUENCY_BACKEND
#if TURBOHAP_NATIVE_X86_WORKLOAD_PROFILE
#define FINDHAP_PEDIGREE_FREQUENCY_BACKEND 2
#else
#define FINDHAP_PEDIGREE_FREQUENCY_BACKEND 0
#endif
#endif
#if (FINDHAP_PEDIGREE_CONFLICT_BACKEND != 0 && FINDHAP_PEDIGREE_CONFLICT_BACKEND != 2 && FINDHAP_PEDIGREE_CONFLICT_BACKEND != 3) || (FINDHAP_POP_SEGMENT_BACKEND != 0 && FINDHAP_POP_SEGMENT_BACKEND != 2 && FINDHAP_POP_SEGMENT_BACKEND != 3) || (FINDHAP_PEDIGREE_FREQUENCY_BACKEND != 0 && FINDHAP_PEDIGREE_FREQUENCY_BACKEND != 2 && FINDHAP_PEDIGREE_FREQUENCY_BACKEND != 3)
#error "x86 workload backends must be 0, 2 or 3"
#endif
#if (FINDHAP_PEDIGREE_CONFLICT_BACKEND == 2 || FINDHAP_POP_SEGMENT_BACKEND == 2 || FINDHAP_PEDIGREE_FREQUENCY_BACKEND == 2) && !defined(__AVX2__)
#error "AVX2 workload backends require an AVX2 native target"
#endif
#if (FINDHAP_PEDIGREE_CONFLICT_BACKEND == 3 || FINDHAP_POP_SEGMENT_BACKEND == 3 || FINDHAP_PEDIGREE_FREQUENCY_BACKEND == 3) && !defined(__AVX512BW__)
#error "AVX-512 workload backends require an AVX-512BW native target"
#endif
#ifndef FINDHAP_POP_X86_PEXT
#if TURBOHAP_NATIVE_X86_WORKLOAD_PROFILE
#define FINDHAP_POP_X86_PEXT 1
#else
#define FINDHAP_POP_X86_PEXT 0
#endif
#endif
#if FINDHAP_POP_X86_PEXT && !defined(__BMI2__)
#error "PEXT signature extraction requires a BMI2 native target"
#endif
#ifndef FINDHAP_LM_ADAPTIVE_POSITIONS
#define FINDHAP_LM_ADAPTIVE_POSITIONS TURBOHAP_WORKLOAD_PROFILE
#endif
// One source tree, native compile-time selection. Build on the target CPU.
// Keep unqualified instruction sets out of portable targets. This does not
// make a -march=native executable portable to less capable processors.
// 31 October: activate the byte-qualified Mac-derived bundle on the tested
// Linux native profiles. Keep other processors/OSes on their earlier paths.
#if defined(__linux__) && defined(__aarch64__) && defined(__ARM_NEON) && defined(FINDHAP_NATIVE_NEOVERSE_N3) && FINDHAP_NATIVE_NEOVERSE_N3
#define TURBOHAP_NATIVE_AXION_N3 1
#else
#define TURBOHAP_NATIVE_AXION_N3 0
#endif
#if defined(__linux__) && defined(__aarch64__) && defined(__ARM_NEON) && defined(FINDHAP_NATIVE_TEGRA234) && FINDHAP_NATIVE_TEGRA234
#define TURBOHAP_NATIVE_ORIN 1
#else
#define TURBOHAP_NATIVE_ORIN 0
#endif
#if defined(FINDHAP_NATIVE_NEOVERSE_V2) || TURBOHAP_NATIVE_AXION_N3 || TURBOHAP_NATIVE_ORIN
#define TURBOHAP_SHARED_LINUX_ARM_POLICY 1
#else
#define TURBOHAP_SHARED_LINUX_ARM_POLICY 0
#endif
// Genoa and Turin share the same byte-lookup implementation.
#if defined(__linux__) && ((defined(__aarch64__) && defined(__ARM_NEON) && \
    ((defined(FINDHAP_NATIVE_GB10) && FINDHAP_NATIVE_GB10) || \
     (defined(FINDHAP_NATIVE_NEOVERSE_V2) && FINDHAP_NATIVE_NEOVERSE_V2) || TURBOHAP_NATIVE_AXION_N3 || TURBOHAP_NATIVE_ORIN)) || \
    (defined(__x86_64__) && defined(__AVX512VBMI__) && defined(__AVX512BW__) && \
     (TURBOHAP_TARGET_GRANITE || TURBOHAP_NATIVE_ICE_LAKE || TURBOHAP_NATIVE_SAPPHIRE_OR_EMERALD || \
      TURBOHAP_TARGET_EPYC_VBMI)))
#define TURBOHAP_TESTED_MAC_BUNDLE_DEFAULT 1
#else
#define TURBOHAP_TESTED_MAC_BUNDLE_DEFAULT 0
#endif
#ifndef FINDHAP_PEDIGREE_REMATCH
#if (defined(__ARM_NEON) && defined(FINDHAP_NATIVE_GB10)) || TURBOHAP_NATIVE_INTEL_WORKLOAD_PROFILE || TURBOHAP_NATIVE_CASCADE_LAKE || TURBOHAP_NATIVE_AMD_WORKLOAD_PROFILE || TURBOHAP_NATIVE_MILAN
// Each thread owns a disjoint destination segment during ancestor rematching.
#define FINDHAP_PEDIGREE_REMATCH 2
#else
#define FINDHAP_PEDIGREE_REMATCH 0
#endif
#endif
#ifndef FINDHAP_LM_WRITEBACK_BATCH_BYTES
#if (defined(__linux__) && defined(FINDHAP_NATIVE_GB10) && defined(__ARM_NEON)) || \
    ((TURBOHAP_NATIVE_X86_WORKLOAD_PROFILE || TURBOHAP_NATIVE_MILAN) && defined(FINDHAP_LOW_MEMORY))
#define FINDHAP_LM_WRITEBACK_BATCH_BYTES (128 * 1024 * 1024)
#else
#define FINDHAP_LM_WRITEBACK_BATCH_BYTES 0
#endif
#endif
#ifndef FINDHAP_LIBRARY_FORMAT
#if defined(FINDHAP_NATIVE_GB10) || TURBOHAP_SHARED_LINUX_ARM_POLICY || \
    TURBOHAP_TARGET_GRANITE || TURBOHAP_NATIVE_ICE_LAKE || TURBOHAP_NATIVE_SAPPHIRE_OR_EMERALD || TURBOHAP_NATIVE_CASCADE_LAKE || \
    TURBOHAP_TARGET_EPYC_VBMI
// Exact fixed-decimal output on the qualified ARM, Cascade Lake, Granite Rapids and Turin builds.
// Other profiles retain the standard library formatter by default.
#define FINDHAP_LIBRARY_FORMAT 2
#else
#define FINDHAP_LIBRARY_FORMAT 0
#endif
#endif
#ifndef FINDHAP_POP_ENCODE
#if TURBOHAP_TESTED_MAC_BUNDLE_DEFAULT && defined(__aarch64__) && defined(__ARM_NEON)
// Exact sampled-locus NEON gathers from the qualified Mac/Spark/ARM C4A bundle.
#define FINDHAP_POP_ENCODE 5
#elif (TURBOHAP_TARGET_MILAN && defined(__AVX2__)) || TURBOHAP_NATIVE_CASCADE_LAKE
// On Milan and Cascade Lake the AVX2 gather retains exactly the sampled loci.
#define FINDHAP_POP_ENCODE 2
#elif defined(__AVX512VBMI__) && defined(__AVX512BW__)
#define FINDHAP_POP_ENCODE 3
#elif defined(__ARM_NEON) && (TURBOHAP_SHARED_LINUX_ARM_POLICY || defined(FINDHAP_NATIVE_GB10))
#define FINDHAP_POP_ENCODE 4
#else
#define FINDHAP_POP_ENCODE 0
#endif
#endif
#ifndef FINDHAP_POP_COMPACT
#define FINDHAP_POP_COMPACT 1
#endif
#ifndef FINDHAP_POP_FULL
#if FINDHAP_POP_ENCODE == 3 && defined(__AVX512VBMI__) && defined(__AVX512BW__)
#define FINDHAP_POP_FULL 2
#elif TURBOHAP_TESTED_MAC_BUNDLE_DEFAULT && defined(__ARM_NEON) && FINDHAP_POP_ENCODE == 5
#define FINDHAP_POP_FULL 2
#else
#define FINDHAP_POP_FULL 0
#endif
#endif
#ifndef FINDHAP_POP_COMPLETE
#define FINDHAP_POP_COMPLETE 0
#endif
#ifndef FINDHAP_PEDIGREE_COORDINATION
#define FINDHAP_PEDIGREE_COORDINATION 2
#endif
#if defined(__linux__) && FINDHAP_PEDIGREE_COORDINATION >= 2 && \
    TURBOHAP_NEON_WORKLOAD_PROFILE
#define FINDHAP_PEDIGREE_PLACEMENT 1
#else
#define FINDHAP_PEDIGREE_PLACEMENT 0
#endif
#ifndef FINDHAP_PEDIGREE_CHROMOSOMES
#define FINDHAP_PEDIGREE_CHROMOSOMES 1
#endif
#ifndef FINDHAP_PEDIGREE_EARLY_REJECT
#define FINDHAP_PEDIGREE_EARLY_REJECT (TURBOHAP_WORKLOAD_PROFILE || TURBOHAP_NATIVE_MILAN)
#endif
#ifndef FINDHAP_PEDIGREE_MASK_BACKEND
#if defined(__AVX2__)
#define FINDHAP_PEDIGREE_MASK_BACKEND 2
#elif TURBOHAP_WORKLOAD_PROFILE
#define FINDHAP_PEDIGREE_MASK_BACKEND (TURBOHAP_WORKLOAD_PROFILE ? 4 : 0)
#else
#define FINDHAP_PEDIGREE_MASK_BACKEND 0
#endif
#endif
#ifndef FINDHAP_PEDIGREE_ORDERED_FINAL
#define FINDHAP_PEDIGREE_ORDERED_FINAL TURBOHAP_TESTED_MAC_BUNDLE_DEFAULT
#endif
#ifndef FINDHAP_PEDIGREE_SERIAL_FINAL
#define FINDHAP_PEDIGREE_SERIAL_FINAL 0
#endif

#if TURBOHAP_NATIVE_EMERALD_RAPIDS
#define TURBOHAP_PLATFORM_POLICY "Intel Emerald Rapids / native byte matching and demand-zero library"
#elif TURBOHAP_NATIVE_SAPPHIRE_RAPIDS
#define TURBOHAP_PLATFORM_POLICY "Intel Sapphire Rapids / native byte matching and demand-zero library"
#elif TURBOHAP_NATIVE_ICE_LAKE
#define TURBOHAP_PLATFORM_POLICY "Intel Ice Lake / native byte matching and demand-zero library"
#elif TURBOHAP_NATIVE_INTEL_WORKLOAD_PROFILE
#define TURBOHAP_PLATFORM_POLICY "Intel Granite Rapids / native byte matching and demand-zero library"
#elif TURBOHAP_NATIVE_CASCADE_LAKE
#define TURBOHAP_PLATFORM_POLICY "Intel Cascade Lake / AVX2 matching and demand-zero library"
#elif TURBOHAP_NATIVE_GENOA
#define TURBOHAP_PLATFORM_POLICY "AMD Genoa / native byte matching and demand-zero library"
#elif TURBOHAP_NATIVE_AMD_WORKLOAD_PROFILE
#define TURBOHAP_PLATFORM_POLICY "AMD Turin / native byte matching and demand-zero library"
#elif TURBOHAP_APPLE_M4_PROFILE && defined(FINDHAP_APPLE_SILICON_COHORT) && FINDHAP_APPLE_SILICON_COHORT && \
      !(defined(FINDHAP_NATIVE_APPLE_M4PRO) && FINDHAP_NATIVE_APPLE_M4PRO)
#define TURBOHAP_PLATFORM_POLICY "Apple Silicon / bounded ordered pedigree and active library"
#elif TURBOHAP_APPLE_M4_PROFILE
#define TURBOHAP_PLATFORM_POLICY "Apple M4 Pro / bounded ordered pedigree and active library"
#elif TURBOHAP_TARGET_MILAN && defined(__AVX2__)
#define TURBOHAP_PLATFORM_POLICY "AMD Zen 3 / AVX2 gather"
#elif defined(__AVX512VBMI__) && defined(__AVX512BW__)
#define TURBOHAP_PLATFORM_POLICY "x86 AVX-512 VBMI / compact full signatures"
#elif TURBOHAP_NATIVE_AXION_N3
#define TURBOHAP_PLATFORM_POLICY "Google Axion Neoverse N3 / NEON signatures"
#elif TURBOHAP_NATIVE_ORIN
#define TURBOHAP_PLATFORM_POLICY "NVIDIA Jetson Orin / shared ARM64 NEON"
#elif defined(__ARM_NEON) && defined(FINDHAP_NATIVE_NEOVERSE_V2)
#define TURBOHAP_PLATFORM_POLICY "ARM Neoverse V2 / NEON signatures"
#elif defined(__ARM_NEON) && defined(FINDHAP_NATIVE_GB10)
#define TURBOHAP_PLATFORM_POLICY "ARM GB10 / NEON signatures"
#elif defined(__ARM_NEON)
#define TURBOHAP_PLATFORM_POLICY "ARM NEON / scalar signatures"
#else
#define TURBOHAP_PLATFORM_POLICY "portable signatures / available conflict SIMD"
#endif

// The EPYC server profiles share the output lookup. A generic znver4 target
// alone does not identify the server and retains its existing defaults.
#if defined(__ARM_NEON) || defined(__icelake_server__) || TURBOHAP_TARGET_GRANITE || TURBOHAP_NATIVE_ICE_LAKE || TURBOHAP_NATIVE_SAPPHIRE_OR_EMERALD || TURBOHAP_NATIVE_CASCADE_LAKE || \
    TURBOHAP_TARGET_EPYC_VBMI
#define TURBOHAP_FAST_OUTPUT_LOOKUP_DEFAULT 1
#else
#define TURBOHAP_FAST_OUTPUT_LOOKUP_DEFAULT 0
#endif

// Keep explicit compile-time overrides, including disabling either feature.
#ifndef FINDHAP_BUFFER_INHERIT
#if TURBOHAP_FAST_OUTPUT_LOOKUP_DEFAULT
#define FINDHAP_BUFFER_INHERIT 1
#else
#define FINDHAP_BUFFER_INHERIT 0
#endif
#endif
// The cap includes page rounding per concurrently active population worker;
// it is independent of animals, loci and user haplotype capacity.
#ifndef FINDHAP_LM_ORDERED_BYTES
#if TURBOHAP_FAST_OUTPUT_LOOKUP_DEFAULT || (TURBOHAP_NATIVE_MILAN && defined(FINDHAP_LOW_MEMORY))
#define FINDHAP_LM_ORDERED_BYTES 4194304
#else
#define FINDHAP_LM_ORDERED_BYTES 0
#endif
#endif

// 29 October confirmation: output on Granite Rapids/native V2, and on the
// tested Turin profile together with core binding. Spark uses the full bundle
// without binding. Generic ARM and Milan retain their earlier policy.
#if TURBOHAP_TARGET_GRANITE || TURBOHAP_NATIVE_ICE_LAKE || TURBOHAP_NATIVE_SAPPHIRE_OR_EMERALD || TURBOHAP_NATIVE_CASCADE_LAKE || (defined(__ARM_NEON) && \
    (TURBOHAP_SHARED_LINUX_ARM_POLICY || defined(FINDHAP_NATIVE_GB10))) || \
    TURBOHAP_TARGET_EPYC_VBMI
#define TURBOHAP_MODERN_OUTPUT_DEFAULT 1
#else
#define TURBOHAP_MODERN_OUTPUT_DEFAULT 0
#endif
#ifndef FINDHAP_INHERIT_RECORD_OUTPUT
#define FINDHAP_INHERIT_RECORD_OUTPUT TURBOHAP_MODERN_OUTPUT_DEFAULT
#endif
#ifndef FINDHAP_OUTPUT_STREAMS
#if TURBOHAP_MODERN_OUTPUT_DEFAULT
#define FINDHAP_OUTPUT_STREAMS 2
#else
#define FINDHAP_OUTPUT_STREAMS 0
#endif
#endif
#ifndef FINDHAP_OUTPUT_BATCH_WRITE
#define FINDHAP_OUTPUT_BATCH_WRITE TURBOHAP_MODERN_OUTPUT_DEFAULT
#endif

// Native Milan and Cascade Lake join the tested Turin binding policy following
// the 30 October comparison. Keep the historical override name for all three.
// This changes placement only; their inference/output guards remain separate.
#ifndef FINDHAP_TURIN_BINDING
#if TURBOHAP_TARGET_EPYC_VBMI || \
    (TURBOHAP_TARGET_MILAN && defined(__AVX2__)) || \
    (TURBOHAP_TARGET_CASCADE && defined(__AVX2__))
#define FINDHAP_TURIN_BINDING 1
#else
#define FINDHAP_TURIN_BINDING 0
#endif
#endif

#ifndef FINDHAP_IO_BUFFER_BYTES
#if TURBOHAP_TESTED_MAC_BUNDLE_DEFAULT || TURBOHAP_NATIVE_CASCADE_LAKE
#define FINDHAP_IO_BUFFER_BYTES 65536
#else
#define FINDHAP_IO_BUFFER_BYTES 0
#endif
#endif
#ifndef FINDHAP_OUTPUT_BUFFER_BYTES
#define FINDHAP_OUTPUT_BUFFER_BYTES 33554432
#endif
#ifndef FINDHAP_PAD_SCRATCH
#define FINDHAP_PAD_SCRATCH 0
#endif
#ifndef FINDHAP_DARWIN_NOCACHE
#define FINDHAP_DARWIN_NOCACHE 0
#endif

#ifndef FINDHAP_NEON_TEXT
#define FINDHAP_NEON_TEXT 0
#endif

#ifndef FINDHAP_CROSSOVER_FORMAT
#define FINDHAP_CROSSOVER_FORMAT 0
#endif
#ifndef FINDHAP_CROSSOVER_BATCH_WRITE
#define FINDHAP_CROSSOVER_BATCH_WRITE 0
#endif
#ifndef FINDHAP_ORDERED_MIN_HAPS
#define FINDHAP_ORDERED_MIN_HAPS 1024
#endif

#ifndef FINDHAP_ORDERED_SCAN_ORDER
#if TURBOHAP_WORKLOAD_PROFILE
#define FINDHAP_ORDERED_SCAN_ORDER (TURBOHAP_WORKLOAD_PROFILE ? 2 : 0)
#else
#define FINDHAP_ORDERED_SCAN_ORDER 0
#endif
#endif
#ifndef FINDHAP_ORDERED_SCAN_NEON
#define FINDHAP_ORDERED_SCAN_NEON 0
#endif

// Library allocation: 0 eagerly clears capacity; 1 initializes published rows;
// 2 poisons unused capacity for diagnostics; 3 uses fresh step mappings and
// initializes published rows, releasing old physical pages between steps.
#ifndef FINDHAP_HAPLIST_INIT
#if TURBOHAP_TESTED_MAC_BUNDLE_DEFAULT || TURBOHAP_NATIVE_CASCADE_LAKE || TURBOHAP_NATIVE_MILAN
#define FINDHAP_HAPLIST_INIT 3
#else
#define FINDHAP_HAPLIST_INIT 0
#endif
#endif
#ifndef FINDHAP_HAPLIST_ASAN
#define FINDHAP_HAPLIST_ASAN 0
#endif
#ifndef FINDHAP_ORDERED_LAYOUT
#if TURBOHAP_TESTED_MAC_BUNDLE_DEFAULT || TURBOHAP_NATIVE_CASCADE_LAKE
#define FINDHAP_ORDERED_LAYOUT 1
#else
#define FINDHAP_ORDERED_LAYOUT 0
#endif
#endif

#ifndef FINDHAP_CROSSOVER_BATCH_BYTES
#define FINDHAP_CROSSOVER_BATCH_BYTES 33554432
#endif
#ifndef FINDHAP_CROSSOVER_BATCH_ROWS
#define FINDHAP_CROSSOVER_BATCH_ROWS 256
#endif

#ifndef FINDHAP_NHAP_STRIDE
#if (defined(__linux__) && defined(__ARM_NEON) && defined(FINDHAP_NATIVE_GB10)) || TURBOHAP_NATIVE_INTEL_WORKLOAD_PROFILE
// Keep each independently written segment counter on its own 64-byte line.
#define FINDHAP_NHAP_STRIDE (64u / sizeof(int))
#else
#define FINDHAP_NHAP_STRIDE 1
#endif
#endif

#ifndef FINDHAP_ORDERED_RECHECK
#define FINDHAP_ORDERED_RECHECK 1
#endif
