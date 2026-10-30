#pragma once
#include <cstdint>
#include "platform_policy.hpp"
#ifndef FINDHAP_OUTPUT_STREAMS
#define FINDHAP_OUTPUT_STREAMS 0
#endif
#ifdef __ARM_NEON
#include <arm_neon.h>
#endif
#if defined(__AVX2__)
#include <immintrin.h>
#endif

namespace turbohap_output {
inline void reconstruct(const int8_t* a, const int8_t* b, int8_t* __restrict__ out,
                        int length, const int table[6][6], const uint8_t* byte_table,
                        bool male_x) {
    int locus = 0;
#if FINDHAP_OUTPUT_STREAMS >= 2 && defined(__ARM_NEON)
    const uint8x16x3_t lookup = {{vld1q_u8(byte_table), vld1q_u8(byte_table + 16),
                                 vld1q_u8(byte_table + 32)}};
    for (; locus + 16 <= length; locus += 16) {
        const auto first = a ? vld1q_u8(reinterpret_cast<const uint8_t*>(a + locus)) : vdupq_n_u8(5);
        const auto second = b ? vld1q_u8(reinterpret_cast<const uint8_t*>(b + locus)) : vdupq_n_u8(5);
        const auto index = vaddq_u8(vmulq_u8(first, vdupq_n_u8(6)), second);
        auto values = vqtbl3q_u8(lookup, index);
        if (male_x)
            values = vbslq_u8(vorrq_u8(vceqq_u8(values, vdupq_n_u8(1)),
                                       vcgtq_u8(values, vdupq_n_u8(2))), vdupq_n_u8(5), values);
        vst1q_s8(out + locus, vreinterpretq_s8_u8(values));
    }
#elif FINDHAP_OUTPUT_STREAMS >= 2 && defined(__AVX512VBMI__) && defined(__AVX512VL__)
    const auto lookup0 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(byte_table));
    const auto lookup1 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(byte_table + 32));
    for (; locus + 32 <= length; locus += 32) {
        const auto first = a ? _mm256_loadu_si256(reinterpret_cast<const __m256i*>(a + locus)) : _mm256_set1_epi8(5);
        const auto second = b ? _mm256_loadu_si256(reinterpret_cast<const __m256i*>(b + locus)) : _mm256_set1_epi8(5);
        const auto six_a = _mm256_add_epi8(_mm256_add_epi8(first, first), _mm256_slli_epi16(first, 2));
        const auto index = _mm256_add_epi8(six_a, second);
        auto values = _mm256_permutex2var_epi8(lookup0, index, lookup1);
        if (male_x)
            values = _mm256_blendv_epi8(values, _mm256_set1_epi8(5),
                _mm256_or_si256(_mm256_cmpeq_epi8(values, _mm256_set1_epi8(1)),
                                 _mm256_cmpgt_epi8(values, _mm256_set1_epi8(2))));
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(out + locus), values);
    }
#endif
    for (; locus < length; ++locus) {
        int value = table[a ? a[locus] : 5][b ? b[locus] : 5];
        if (male_x && (value == 1 || value > 2)) value = 5;
        out[locus] = static_cast<int8_t>(value);
    }
}

inline void scalar_haplotypes(const int8_t* a, const int8_t* b, char* out, int length) {
    for (int locus=0; locus<length; ++locus) {
        const int first = a ? a[locus] : 1, second = b ? b[locus] : 1;
        out[2*locus] = static_cast<char>((first == 1 ? 1 : first / 2 + 1) + '0');
        out[2*locus+1] = static_cast<char>((second == 1 ? 2 : second / 2 + 1) + '0');
    }
}

inline void encode_haplotypes(const int8_t* a, const int8_t* b,
                              char* __restrict__ out, int length) {
    int locus = 0;
#if FINDHAP_OUTPUT_STREAMS >= 2 && defined(__ARM_NEON)
    for (; locus + 16 <= length; locus += 16) {
        const auto first = a ? vld1q_u8(reinterpret_cast<const uint8_t*>(a + locus)) : vdupq_n_u8(1);
        const auto second = b ? vld1q_u8(reinterpret_cast<const uint8_t*>(b + locus)) : vdupq_n_u8(1);
        if (vminvq_s8(vreinterpretq_s8_u8(vorrq_u8(first,second))) < 0) {
            scalar_haplotypes(a ? a+locus : nullptr, b ? b+locus : nullptr, out+2*locus,16);
            continue;
        }
        uint8x16x2_t digits;
        digits.val[0] = vaddq_u8(vshrq_n_u8(first, 1), vdupq_n_u8('1'));
        digits.val[1] = vaddq_u8(vbslq_u8(vceqq_u8(second, vdupq_n_u8(1)),
                              vdupq_n_u8(1), vshrq_n_u8(second, 1)), vdupq_n_u8('1'));
        vst2q_u8(reinterpret_cast<uint8_t*>(out + 2*locus), digits);
    }
#elif FINDHAP_OUTPUT_STREAMS >= 2 && defined(__AVX2__)
    for (; locus + 32 <= length; locus += 32) {
        const auto first = a ? _mm256_loadu_si256(reinterpret_cast<const __m256i*>(a + locus)) : _mm256_set1_epi8(1);
        const auto second = b ? _mm256_loadu_si256(reinterpret_cast<const __m256i*>(b + locus)) : _mm256_set1_epi8(1);
        if (_mm256_movemask_epi8(_mm256_or_si256(first,second))) {
            scalar_haplotypes(a ? a+locus : nullptr, b ? b+locus : nullptr,out+2*locus,32);
            continue;
        }
        const auto shift_first = _mm256_and_si256(_mm256_srli_epi16(first, 1), _mm256_set1_epi8(127));
        const auto shift_second = _mm256_and_si256(_mm256_srli_epi16(second, 1), _mm256_set1_epi8(127));
        const auto d0 = _mm256_add_epi8(shift_first, _mm256_set1_epi8('1'));
        const auto d1 = _mm256_add_epi8(_mm256_blendv_epi8(shift_second, _mm256_set1_epi8(1),
                                     _mm256_cmpeq_epi8(second, _mm256_set1_epi8(1))), _mm256_set1_epi8('1'));
        const auto low = _mm256_unpacklo_epi8(d0, d1), high = _mm256_unpackhi_epi8(d0, d1);
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(out + 2*locus), _mm256_permute2x128_si256(low, high, 0x20));
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(out + 2*locus + 32), _mm256_permute2x128_si256(low, high, 0x31));
    }
#endif
    scalar_haplotypes(a ? a+locus : nullptr, b ? b+locus : nullptr,out+2*locus,length-locus);
}
} // namespace turbohap_output
