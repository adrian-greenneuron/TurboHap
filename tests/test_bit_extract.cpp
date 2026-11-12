#include "../src/bit_extract.hpp"
#include <cassert>
#include <cstdint>
#include <random>

static uint64_t scalar_extract(uint64_t bits, uint64_t mask) {
    uint64_t result = 0;
    unsigned destination = 0;
    for (unsigned source = 0; source < 64; ++source) {
        if ((mask >> source) & 1) {
            result |= ((bits >> source) & 1) << destination;
            ++destination;
        }
    }
    return result;
}

int main() {
    findhap_bit_extract::Selector selector;
    std::mt19937_64 random(8112026);
    // Every source position, empty/full selections, arbitrary masks and reuse.
    for (uint64_t mask : {uint64_t(0), UINT64_MAX, uint64_t(1),
                          uint64_t(1) << 63, UINT64_C(0x5555555555555555)}) {
        selector.prepare(mask);
        for (unsigned source = 0; source < 64; ++source) {
            const uint64_t bits = uint64_t(1) << source;
            assert(selector.extract(bits) == scalar_extract(bits, mask));
        }
    }
    for (unsigned trial = 0; trial < 100000; ++trial) {
        const auto mask = random(), bits = random();
        selector.prepare(mask);
        assert(selector.extract(bits) == scalar_extract(bits, mask));
    }
}
