#pragma once
#include <cstdint>
#ifdef __ARM_NEON
#include <arm_neon.h>
namespace findhap_neon {
inline uint16_t lane_mask(uint8x16_t predicate) {
    static constexpr uint8_t weights[16] = {1,2,4,8,16,32,64,128,1,2,4,8,16,32,64,128};
    const auto weighted = vandq_u8(predicate, vld1q_u8(weights));
    return uint16_t(vaddv_u8(vget_low_u8(weighted))) |
           (uint16_t(vaddv_u8(vget_high_u8(weighted))) << 8);
}

}
#endif
