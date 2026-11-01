#pragma once
#include "platform_policy.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#ifndef FINDHAP_NEON_PACK
#define FINDHAP_NEON_PACK 0
#endif
#if defined(__ARM_NEON)
#include <arm_neon.h>
#endif
namespace findhap_packed {
inline void pack(uint8_t* out, const int8_t* in, size_t length) {
    size_t l = 0;
#if FINDHAP_NEON_PACK && defined(__ARM_NEON)
    for (; length - l >= 64; l += 64) {
        auto values = vld4q_u8(reinterpret_cast<const uint8_t*>(in + l));
        for (int j=0;j<4;++j)
            values.val[j] = vbslq_u8(vceqq_u8(values.val[j],vdupq_n_u8(5)),vdupq_n_u8(3),values.val[j]);
        const auto result = vorrq_u8(vorrq_u8(values.val[0],vshlq_n_u8(values.val[1],2)),
                                     vorrq_u8(vshlq_n_u8(values.val[2],4),vshlq_n_u8(values.val[3],6)));
        vst1q_u8(out + l/4, result);
    }
#endif
    auto code=[](int8_t x)->uint8_t{return x==5?3:static_cast<uint8_t>(x);};
    for (; length-l>=4;l+=4)
        out[l/4]=code(in[l]) | (code(in[l+1])<<2) | (code(in[l+2])<<4) | (code(in[l+3])<<6);
    if(l<length) {
        uint8_t tail=0;const size_t byte=l/4;
        for(int shift=0;l<length;++l,shift+=2)tail|=code(in[l])<<shift;
        out[byte]=tail;
    }
}
inline void unpack(int8_t* out,const uint8_t* in,size_t length) {
    size_t l=0;
#if FINDHAP_NEON_PACK && defined(__ARM_NEON)
    auto decode=[](uint8x16_t code) {
        return vaddq_u8(code,vandq_u8(vceqq_u8(code,vdupq_n_u8(3)),vdupq_n_u8(2)));
    };
    for (; length-l>=64;l+=64) {
        const auto bytes=vld1q_u8(in+l/4);
        uint8x16x4_t values;
        values.val[0]=decode(vandq_u8(bytes,vdupq_n_u8(3)));
        values.val[1]=decode(vandq_u8(vshrq_n_u8(bytes,2),vdupq_n_u8(3)));
        values.val[2]=decode(vandq_u8(vshrq_n_u8(bytes,4),vdupq_n_u8(3)));
        values.val[3]=decode(vshrq_n_u8(bytes,6));
        vst4q_u8(reinterpret_cast<uint8_t*>(out+l),values);
    }
    static constexpr uint8_t indices[16]={0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3};
    static constexpr int8_t shifts[16]={0,-2,-4,-6,0,-2,-4,-6,0,-2,-4,-6,0,-2,-4,-6};
    for (;length-l>=16;l+=16) {
        uint32_t word;std::memcpy(&word,in+l/4,4); // exactly four bytes, including guarded tails
        const auto repeated=vqtbl1q_u8(vreinterpretq_u8_u32(vdupq_n_u32(word)),vld1q_u8(indices));
        const auto codes=vandq_u8(vshlq_u8(repeated,vld1q_s8(shifts)),vdupq_n_u8(3));
        vst1q_u8(reinterpret_cast<uint8_t*>(out+l),decode(codes));
    }
#endif
#if FINDHAP_NEON_PACK >= 2
    // Four-byte scalar tails retain the original table throughput on short
    // population segments instead of decoding each remaining locus separately.
    static constexpr auto decoded=[] {
        std::array<std::array<int8_t,4>,256> table{};
        for(int byte=0;byte<256;++byte)for(int q=0;q<4;++q) {
            const int value=(byte>>(2*q))&3;table[byte][q]=value==3?5:value;
        }
        return table;
    }();
    for(;length-l>=4;l+=4)std::memcpy(out+l,decoded[in[l/4]].data(),4);
#endif
    for (;l<length;++l) {
        const auto value=(in[l/4]>>(2*(l%4)))&3;
        out[l]=value==3?5:value;
    }
}
}
