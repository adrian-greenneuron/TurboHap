#pragma once
// One source tree, native compile-time selection. Build on the target CPU.
// Keep unqualified instruction sets out of portable targets. This does not
// make a -march=native executable portable to less capable processors.
#ifndef FINDHAP_POP_ENCODE
#if defined(__znver3__) && defined(__AVX2__)
// Milan qualification: AVX2 gathers retain exactly the sampled loci.
#define FINDHAP_POP_ENCODE 2
#elif defined(__AVX512VBMI__) && defined(__AVX512BW__)
#define FINDHAP_POP_ENCODE 3
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
#ifndef FINDHAP_PEDIGREE_SERIAL_FINAL
#define FINDHAP_PEDIGREE_SERIAL_FINAL 0
#endif

#if defined(__znver3__) && defined(__AVX2__)
#define TURBOHAP_PLATFORM_POLICY "AMD Zen 3 / AVX2 gather"
#elif defined(__AVX512VBMI__) && defined(__AVX512BW__)
#define TURBOHAP_PLATFORM_POLICY "x86 AVX-512 VBMI / compact full signatures"
#elif defined(__ARM_NEON)
#define TURBOHAP_PLATFORM_POLICY "ARM NEON / scalar signatures"
#else
#define TURBOHAP_PLATFORM_POLICY "portable signatures / available conflict SIMD"
#endif
