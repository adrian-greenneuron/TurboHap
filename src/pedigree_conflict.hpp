#pragma once
#include "platform_policy.hpp"
#include <cstdint>
#include <cstdlib>
#if FINDHAP_PEDIGREE_CONFLICT_BACKEND
#include "conflict_scan.hpp"
#endif
#if defined(__ARM_NEON) && FINDHAP_PEDIGREE_NEON_NEAR
#include <arm_neon.h>
#endif

// A pedigree near-match is accepted only when exactly one locus conflicts.
// Count short blocks so the compiler can vectorise, stopping after the second
// conflict. Do not read beyond the actual segment, including its partial tail.
inline bool pedigree_single_conflict(const int8_t* a, const int8_t* b, int length) {
    int count = 0;
    int i = 0;
#if FINDHAP_PEDIGREE_CONFLICT_BACKEND
#if FINDHAP_PEDIGREE_CONFLICT_BACKEND == 3
    for (; length-i >= 64; i += 64) {
        count += __builtin_popcountll(findhap_simd::mask64(a+i,b+i));
        if (count > 1) return false;
    }
#endif
    for (; length-i >= 32; i += 32) {
        count += __builtin_popcount(findhap_simd::mask32(a+i,b+i));
        if (count > 1) return false;
    }
#endif
    for (; i + 16 <= length; i += 16) {
#if defined(__ARM_NEON) && FINDHAP_PEDIGREE_NEON_NEAR
        const auto difference = vreinterpretq_u8_s8(vabdq_s8(vld1q_s8(a+i), vld1q_s8(b+i)));
        count += vaddvq_u8(vshrq_n_u8(vcgtq_u8(difference, vdupq_n_u8(1)), 7));
#elif FINDHAP_PEDIGREE_CONFLICT_BACKEND
        count += __builtin_popcount(findhap_simd::mask16(a+i,b+i));
#else
        for (int k = 0; k < 16; ++k)
            count += std::abs(int(a[i+k]) - int(b[i+k])) > 1;
#endif
        if (count > 1) return false;
    }
    for (; i < length; ++i)
        count += std::abs(int(a[i]) - int(b[i])) > 1;
    return count == 1;
}
