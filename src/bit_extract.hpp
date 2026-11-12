#pragma once

#include <array>
#include <cstdint>

namespace findhap_bit_extract {

// Compact selected bits in their original order. Precompute the six parallel
// prefix movements once per segment, then reuse them for every full bitplane.
// No instruction-set extension or floating-point operation is required.
class Selector {
    uint64_t mask_ = 0;
    std::array<uint64_t, 6> moves_{};

public:
    void prepare(uint64_t selected) {
        mask_ = selected;
        uint64_t remaining = ~selected << 1;
        for (unsigned i = 0; i < moves_.size(); ++i) {
            uint64_t prefix = remaining ^ (remaining << 1);
            prefix ^= prefix << 2;
            prefix ^= prefix << 4;
            prefix ^= prefix << 8;
            prefix ^= prefix << 16;
            prefix ^= prefix << 32;
            const uint64_t moved = prefix & selected;
            moves_[i] = moved;
            selected = (selected ^ moved) | (moved >> (1u << i));
            remaining &= ~prefix;
        }
    }

    uint64_t extract(uint64_t bits) const {
        bits &= mask_;
        for (unsigned i = 0; i < moves_.size(); ++i) {
            const uint64_t moved = bits & moves_[i];
            bits = (bits ^ moved) | (moved >> (1u << i));
        }
        return bits;
    }
};

} // namespace findhap_bit_extract
