#pragma once
#include <cstdint>
#include "platform_policy.hpp"
#if defined(__ARM_NEON)
#include <arm_neon.h>
#endif
namespace findhap_text {
// Preserve the Fortran I1 rule: each blank is a zero locus, never skipped.
inline void decode(const char* source, int8_t* destination, int length) {
    int locus=0;
#if FINDHAP_NEON_TEXT && defined(__ARM_NEON)
    for(;locus+16<=length;locus+=16) {
        const auto fields=vld1q_u8(reinterpret_cast<const uint8_t*>(source+locus));
        const auto values=vbslq_u8(vceqq_u8(fields,vdupq_n_u8(' ')),vdupq_n_u8(0),vsubq_u8(fields,vdupq_n_u8('0')));
        vst1q_s8(destination+locus,vreinterpretq_s8_u8(values));
    }
#endif
    for(;locus<length;++locus)
        destination[locus]=source[locus]==' '?0:static_cast<int8_t>(source[locus]-'0');
}
}
