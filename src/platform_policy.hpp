#pragma once
#include "macos_build_profile.hpp"
// One source tree, native compile-time selection. Build on the target CPU.
// Keep unqualified instruction sets out of portable targets. This does not
// make a -march=native executable portable to less capable processors.
// 31 October: activate the byte-qualified Mac-derived bundle on the tested
// Linux native profiles. Keep other processors/OSes on their earlier paths.
// GCC13 identifies the tested Turin as znver4 plus VP2INTERSECT; that marker
// distinguishes its profile from Genoa, rather than implementing the lookup.
#if defined(__linux__) && ((defined(__aarch64__) && defined(__ARM_NEON) && \
    ((defined(FINDHAP_NATIVE_GB10) && FINDHAP_NATIVE_GB10) || \
     (defined(FINDHAP_NATIVE_NEOVERSE_V2) && FINDHAP_NATIVE_NEOVERSE_V2))) || \
    (defined(__x86_64__) && defined(__AVX512VBMI__) && defined(__AVX512BW__) && \
     (defined(__graniterapids__) || \
      ((defined(__znver4__) || defined(__znver5__)) && defined(__AVX512VP2INTERSECT__)))))
#define TURBOHAP_TESTED_MAC_BUNDLE_DEFAULT 1
#else
#define TURBOHAP_TESTED_MAC_BUNDLE_DEFAULT 0
#endif
#ifndef FINDHAP_PEDIGREE_REMATCH
#if defined(__ARM_NEON) && defined(FINDHAP_NATIVE_GB10)
// Segment-owned ungenotyped rematching qualified on Spark, including the
// local private cohort. Other CPUs keep the previous path pending validation.
#define FINDHAP_PEDIGREE_REMATCH 2
#else
#define FINDHAP_PEDIGREE_REMATCH 0
#endif
#endif
#ifndef FINDHAP_LIBRARY_FORMAT
#if defined(FINDHAP_NATIVE_GB10) || defined(FINDHAP_NATIVE_NEOVERSE_V2) || \
    defined(__graniterapids__) || \
    ((defined(__znver4__) || defined(__znver5__)) && defined(__AVX512VP2INTERSECT__))
// Exact fixed-decimal library output qualified on Spark, C4A, C4 and C4D.
// Unmeasured targets (including Milan and Ice Lake) retain libc by default.
#define FINDHAP_LIBRARY_FORMAT 2
#else
#define FINDHAP_LIBRARY_FORMAT 0
#endif
#endif
#ifndef FINDHAP_POP_ENCODE
#if TURBOHAP_TESTED_MAC_BUNDLE_DEFAULT && defined(__aarch64__) && defined(__ARM_NEON)
// Exact sampled-locus NEON gathers from the qualified Mac/Spark/ARM C4A bundle.
#define FINDHAP_POP_ENCODE 5
#elif defined(__znver3__) && defined(__AVX2__)
// Milan qualification: AVX2 gathers retain exactly the sampled loci.
#define FINDHAP_POP_ENCODE 2
#elif defined(__AVX512VBMI__) && defined(__AVX512BW__)
#define FINDHAP_POP_ENCODE 3
#elif defined(__ARM_NEON) && (defined(FINDHAP_NATIVE_NEOVERSE_V2) || defined(FINDHAP_NATIVE_GB10))
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
#ifndef FINDHAP_PEDIGREE_CHROMOSOMES
#define FINDHAP_PEDIGREE_CHROMOSOMES 1
#endif
#ifndef FINDHAP_PEDIGREE_EARLY_REJECT
#define FINDHAP_PEDIGREE_EARLY_REJECT 0
#endif
#ifndef FINDHAP_PEDIGREE_MASK_BACKEND
#if defined(__AVX2__)
#define FINDHAP_PEDIGREE_MASK_BACKEND 2
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

#if TURBOHAP_APPLE_M4_PROFILE
#define TURBOHAP_PLATFORM_POLICY "Apple M4 Pro / bounded ordered pedigree and active library"
#elif defined(__znver3__) && defined(__AVX2__)
#define TURBOHAP_PLATFORM_POLICY "AMD Zen 3 / AVX2 gather"
#elif defined(__AVX512VBMI__) && defined(__AVX512BW__)
#define TURBOHAP_PLATFORM_POLICY "x86 AVX-512 VBMI / compact full signatures"
#elif defined(__ARM_NEON) && defined(FINDHAP_NATIVE_NEOVERSE_V2)
#define TURBOHAP_PLATFORM_POLICY "ARM Neoverse V2 / NEON signatures"
#elif defined(__ARM_NEON) && defined(FINDHAP_NATIVE_GB10)
#define TURBOHAP_PLATFORM_POLICY "ARM GB10 / NEON signatures"
#elif defined(__ARM_NEON)
#define TURBOHAP_PLATFORM_POLICY "ARM NEON / scalar signatures"
#else
#define TURBOHAP_PLATFORM_POLICY "portable signatures / available conflict SIMD"
#endif

// Qualified native profiles. GCC 13 reports Turin as znver4, but its native
// VP2INTERSECT feature distinguishes the tested C4D build from Genoa. Do not
// enable these defaults for every znver4 target or for Milan/znver3.
// VP2INTERSECT is a profile discriminator here, not a lookup requirement.
#if defined(__ARM_NEON) || defined(__icelake_server__) || defined(__graniterapids__) || \
    ((defined(__znver4__) || defined(__znver5__)) && defined(__AVX512VP2INTERSECT__))
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
// Enabled on ARM, Ice Lake, Granite Rapids and the tested Turin feature profile.
// Milan retains its earlier LM path while its timing guard remains unresolved.
// The cap includes page rounding per concurrently active population worker;
// it is independent of animals, loci and user haplotype capacity.
#ifndef FINDHAP_LM_ORDERED_BYTES
#if TURBOHAP_FAST_OUTPUT_LOOKUP_DEFAULT
#define FINDHAP_LM_ORDERED_BYTES 4194304
#else
#define FINDHAP_LM_ORDERED_BYTES 0
#endif
#endif

// 29 October confirmation: output on Granite Rapids/native V2, and on the
// tested Turin profile together with core binding. Spark uses the full bundle
// without binding. Generic ARM, Ice Lake and Milan retain their earlier policy.
#if defined(__graniterapids__) || (defined(__ARM_NEON) && \
    (defined(FINDHAP_NATIVE_NEOVERSE_V2) || defined(FINDHAP_NATIVE_GB10))) || \
    ((defined(__znver4__) || defined(__znver5__)) && defined(__AVX512VP2INTERSECT__))
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
#if ((defined(__znver4__) || defined(__znver5__)) && defined(__AVX512VP2INTERSECT__)) || \
    (defined(__znver3__) && defined(__AVX2__)) || \
    (defined(__cascadelake__) && defined(__AVX2__))
#define FINDHAP_TURIN_BINDING 1
#else
#define FINDHAP_TURIN_BINDING 0
#endif
#endif

#ifndef FINDHAP_IO_BUFFER_BYTES
#if TURBOHAP_TESTED_MAC_BUNDLE_DEFAULT
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
#define FINDHAP_ORDERED_SCAN_ORDER 0
#endif
#ifndef FINDHAP_ORDERED_SCAN_NEON
#define FINDHAP_ORDERED_SCAN_NEON 0
#endif

// Library allocation: 0 eagerly clears capacity; 1 initializes published rows;
// 2 poisons unused capacity for diagnostics; 3 uses fresh step mappings and
// initializes published rows, releasing old physical pages between steps.
#ifndef FINDHAP_HAPLIST_INIT
#if TURBOHAP_TESTED_MAC_BUNDLE_DEFAULT
#define FINDHAP_HAPLIST_INIT 3
#else
#define FINDHAP_HAPLIST_INIT 0
#endif
#endif
#ifndef FINDHAP_HAPLIST_ASAN
#define FINDHAP_HAPLIST_ASAN 0
#endif
#ifndef FINDHAP_ORDERED_LAYOUT
#define FINDHAP_ORDERED_LAYOUT TURBOHAP_TESTED_MAC_BUNDLE_DEFAULT
#endif

#ifndef FINDHAP_CROSSOVER_BATCH_BYTES
#define FINDHAP_CROSSOVER_BATCH_BYTES 33554432
#endif
#ifndef FINDHAP_CROSSOVER_BATCH_ROWS
#define FINDHAP_CROSSOVER_BATCH_ROWS 256
#endif

#ifndef FINDHAP_NHAP_STRIDE
#define FINDHAP_NHAP_STRIDE 1
#endif

#ifndef FINDHAP_ORDERED_RECHECK
#define FINDHAP_ORDERED_RECHECK 1
#endif
