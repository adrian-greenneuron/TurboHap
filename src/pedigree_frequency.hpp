#pragma once
#include <cstdint>
#include "platform_policy.hpp"
#if FINDHAP_PEDIGREE_FREQUENCY_BACKEND
#include <immintrin.h>
#endif
#if defined(__ARM_NEON)
#include <arm_neon.h>
#endif
namespace findhap_pedigree {
inline void add_base_frequency(const int8_t* __restrict__ values,
        int* __restrict__ sums,int* __restrict__ counts,int length) {
    int l=0;
#if FINDHAP_PEDIGREE_FREQUENCY_BACKEND == 3
    for(;length-l>=16;l+=16) {
        const auto bytes=_mm_loadu_si128(reinterpret_cast<const __m128i*>(values+l));
        const auto wide=_mm512_cvtepi8_epi32(bytes);
        const auto known=_mm512_cmpneq_epi32_mask(wide,_mm512_set1_epi32(1));
        const auto s=_mm512_mask_add_epi32(_mm512_loadu_si512(sums+l),known,_mm512_loadu_si512(sums+l),wide);
        const auto c=_mm512_mask_add_epi32(_mm512_loadu_si512(counts+l),known,_mm512_loadu_si512(counts+l),_mm512_set1_epi32(1));
        _mm512_storeu_si512(sums+l,s);_mm512_storeu_si512(counts+l,c);
    }
#elif FINDHAP_PEDIGREE_FREQUENCY_BACKEND == 2
    for(;length-l>=8;l+=8) {
        const auto bytes=_mm_loadl_epi64(reinterpret_cast<const __m128i*>(values+l));
        const auto wide=_mm256_cvtepi8_epi32(bytes);
        const auto missing=_mm256_cmpeq_epi32(wide,_mm256_set1_epi32(1));
        const auto amounts=_mm256_andnot_si256(missing,wide);
        const auto increments=_mm256_andnot_si256(missing,_mm256_set1_epi32(1));
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(sums+l),_mm256_add_epi32(_mm256_loadu_si256(reinterpret_cast<const __m256i*>(sums+l)),amounts));
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(counts+l),_mm256_add_epi32(_mm256_loadu_si256(reinterpret_cast<const __m256i*>(counts+l)),increments));
    }
#endif
#if defined(__ARM_NEON)
    for(;l+16<=length;l+=16) {
        const auto v=vld1q_s8(values+l);
        const auto known=vmvnq_u8(vceqq_s8(v,vdupq_n_s8(1)));
        const auto amounts=vreinterpretq_s8_u8(vandq_u8(vreinterpretq_u8_s8(v),known));
        const auto c0=vmovl_s8(vreinterpret_s8_u8(vget_low_u8(known)));
        const auto c1=vmovl_s8(vreinterpret_s8_u8(vget_high_u8(known)));
        const auto a0=vmovl_s8(vget_low_s8(amounts)),a1=vmovl_s8(vget_high_s8(amounts));
        vst1q_s32(sums+l,vaddq_s32(vld1q_s32(sums+l),vmovl_s16(vget_low_s16(a0))));
        vst1q_s32(sums+l+4,vaddq_s32(vld1q_s32(sums+l+4),vmovl_s16(vget_high_s16(a0))));
        vst1q_s32(sums+l+8,vaddq_s32(vld1q_s32(sums+l+8),vmovl_s16(vget_low_s16(a1))));
        vst1q_s32(sums+l+12,vaddq_s32(vld1q_s32(sums+l+12),vmovl_s16(vget_high_s16(a1))));
        vst1q_s32(counts+l,vsubq_s32(vld1q_s32(counts+l),vmovl_s16(vget_low_s16(c0))));
        vst1q_s32(counts+l+4,vsubq_s32(vld1q_s32(counts+l+4),vmovl_s16(vget_high_s16(c0))));
        vst1q_s32(counts+l+8,vsubq_s32(vld1q_s32(counts+l+8),vmovl_s16(vget_low_s16(c1))));
        vst1q_s32(counts+l+12,vsubq_s32(vld1q_s32(counts+l+12),vmovl_s16(vget_high_s16(c1))));
    }
#endif
    for(;l<length;++l)if(values[l]!=1){sums[l]+=values[l];++counts[l];}
}
}
