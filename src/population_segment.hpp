#pragma once
#include <cstdint>
#include "platform_policy.hpp"
#if FINDHAP_POP_FINAL_SCAN_NEON && defined(__ARM_NEON)
#include <cstdlib>
#include "neon_bits.hpp"
#endif
#if FINDHAP_POP_SEGMENT_BACKEND
#include <immintrin.h>
#endif
#if defined(__ARM_NEON)
#include <arm_neon.h>
#endif
namespace findhap_population {
inline int normalise(const int8_t* __restrict__ raw,int8_t* __restrict__ segment,int length) {
    int l=0,known=0;
#if FINDHAP_POP_SEGMENT_BACKEND == 3
    for(;length-l>=64;l+=64) {
        const auto input=_mm512_loadu_si512(raw+l);
        const auto missing=_mm512_cmpeq_epi8_mask(input,_mm512_set1_epi8(5));
        const auto value=_mm512_mask_mov_epi8(input,missing,_mm512_set1_epi8(1));
        _mm512_storeu_si512(segment+l,value);
        known+=64-__builtin_popcountll(_mm512_cmpeq_epi8_mask(value,_mm512_set1_epi8(1)));
    }
#endif
#if FINDHAP_POP_SEGMENT_BACKEND
    for(;length-l>=32;l+=32) {
        const auto input=_mm256_loadu_si256(reinterpret_cast<const __m256i*>(raw+l));
        const auto value=_mm256_blendv_epi8(input,_mm256_set1_epi8(1),_mm256_cmpeq_epi8(input,_mm256_set1_epi8(5)));
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(segment+l),value);
        known+=32-__builtin_popcount(unsigned(_mm256_movemask_epi8(_mm256_cmpeq_epi8(value,_mm256_set1_epi8(1)))));
    }
#endif
#if defined(__ARM_NEON)
    for(;l+16<=length;l+=16) {
        const auto input=vld1q_s8(raw+l);
        const auto value=vbslq_s8(vceqq_s8(input,vdupq_n_s8(5)),vdupq_n_s8(1),input);
        vst1q_s8(segment+l,value);
        known+=16-vaddvq_u8(vshrq_n_u8(vceqq_s8(value,vdupq_n_s8(1)),7));
    }
#endif
    for(;l<length;++l){segment[l]=raw[l]==5?1:raw[l];known+=segment[l]!=1;}
    return known;
}
inline int backsolve(int8_t* __restrict__ segment,const int8_t* __restrict__ hap,
        const int8_t* __restrict__ raw,int length) {
    int l=0,known=0;
#if FINDHAP_POP_SEGMENT_BACKEND == 3
    for(;length-l>=64;l+=64) {
        const auto current=_mm512_loadu_si512(segment+l);
        const auto missing=_mm512_cmpeq_epi8_mask(_mm512_loadu_si512(raw+l),_mm512_set1_epi8(5));
        const auto difference=_mm512_sub_epi8(_mm512_add_epi8(current,current),_mm512_loadu_si512(hap+l));
        const auto value=_mm512_mask_mov_epi8(difference,missing,_mm512_set1_epi8(1));
        _mm512_storeu_si512(segment+l,value);
        known+=64-__builtin_popcountll(_mm512_cmpeq_epi8_mask(value,_mm512_set1_epi8(1)));
    }
#endif
#if FINDHAP_POP_SEGMENT_BACKEND
    for(;length-l>=32;l+=32) {
        const auto current=_mm256_loadu_si256(reinterpret_cast<const __m256i*>(segment+l));
        const auto missing=_mm256_cmpeq_epi8(_mm256_loadu_si256(reinterpret_cast<const __m256i*>(raw+l)),_mm256_set1_epi8(5));
        const auto difference=_mm256_sub_epi8(_mm256_add_epi8(current,current),_mm256_loadu_si256(reinterpret_cast<const __m256i*>(hap+l)));
        const auto value=_mm256_blendv_epi8(difference,_mm256_set1_epi8(1),missing);
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(segment+l),value);
        known+=32-__builtin_popcount(unsigned(_mm256_movemask_epi8(_mm256_cmpeq_epi8(value,_mm256_set1_epi8(1)))));
    }
#endif
#if defined(__ARM_NEON)
    for(;l+16<=length;l+=16) {
        const auto value=vbslq_s8(vceqq_s8(vld1q_s8(raw+l),vdupq_n_s8(5)),vdupq_n_s8(1),
            vsubq_s8(vshlq_n_s8(vld1q_s8(segment+l),1),vld1q_s8(hap+l)));
        vst1q_s8(segment+l,value);
        known+=16-vaddvq_u8(vshrq_n_u8(vceqq_s8(value,vdupq_n_s8(1)),7));
    }
#endif
    for(;l<length;++l){segment[l]=raw[l]==5?1:int8_t(2*int(segment[l])-int(hap[l]));known+=segment[l]!=1;}
    return known;
}
#if FINDHAP_POP_FINAL_SCAN_NEON && defined(__ARM_NEON)
// Match the scalar scan's entire fill prefix and untouched suffix on failure.
inline bool final_fill_positions(const int8_t* segment, const int8_t* hap,
                                 int length, int* fill, int& miss) {
    miss=0; int l=0;
    const auto one=vdupq_n_s8(1);
    for(;length-l>=16;l+=16) {
        const auto s=vld1q_s8(segment+l), h=vld1q_s8(hap+l);
        const auto difference=vreinterpretq_u8_s8(vabdq_s8(s,h));
        const unsigned conflicts=findhap_neon::lane_mask(vcgtq_u8(difference,vdupq_n_u8(1)));
        unsigned positions=findhap_neon::lane_mask(vandq_u8(vceqq_s8(h,one),vmvnq_u8(vceqq_s8(s,one))));
        if(conflicts)positions&=(1u<<__builtin_ctz(conflicts))-1u;
        while(positions) {
            ++miss; fill[miss-1]=l+__builtin_ctz(positions); positions&=positions-1;
        }
        if(conflicts)return false;
    }
    for(;l<length;++l) {
        if(std::abs(int(segment[l])-int(hap[l]))>1)return false;
        if(hap[l]==1 && segment[l]!=1) {++miss;fill[miss-1]=l;}
    }
    return true;
}
#endif
}
