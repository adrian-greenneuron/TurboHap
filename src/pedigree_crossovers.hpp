#pragma once
#include <algorithm>
#include "conflict_scan.hpp"

namespace findhap_pedigree {
struct CrossoverScan {
    int count[2] = {0, 0};
    int first[2] = {0, 0};
    int last[2];
};

// Both starting parental haplotypes see the same conflict masks. Only loci
// conflicting with at least one parent can change the original state machine.
// Keep all such loci in ascending order, including two conflicts at one locus.
template<int Backend>
inline CrossoverScan scan_crossovers(const int8_t* child, const int8_t* a,
                                    const int8_t* b, int length) {
    CrossoverScan out;
    out.last[0] = out.last[1] = length + 1;
    int current[2] = {0, 1};
    constexpr int width = Backend == 3 ? 64 : 32;
    for (int base = 0; base < length; base += width) {
        uint64_t masks[2] = {0, 0};
        const int n = std::min(width, length - base);
#if defined(__x86_64__) || defined(__i386__)
        if (n == width && Backend != 0) {
            if constexpr (Backend == 3) {
                masks[0] = findhap_simd::mask64(child + base, a + base);
                masks[1] = findhap_simd::mask64(child + base, b + base);
            } else {
                masks[0] = findhap_simd::mask32(child + base, a + base);
                masks[1] = findhap_simd::mask32(child + base, b + base);
            }
        } else
#endif
        {
            for (int q = 0; q < n; ++q) {
                masks[0] |= uint64_t(std::abs(int(child[base+q])-int(a[base+q])) > 1) << q;
                masks[1] |= uint64_t(std::abs(int(child[base+q])-int(b[base+q])) > 1) << q;
            }
        }
        uint64_t positions = masks[0] | masks[1];
        while (positions) {
            const int q = __builtin_ctzll(positions);
            const uint64_t bit = uint64_t(1) << q;
            positions &= positions - 1;
            for (int start = 0; start < 2; ++start) {
                int& k = current[start];
                if (masks[k] & bit) {
                    ++out.count[start];
                    out.last[start] = base + q + 1;
                    k = 1 - k;
                    if (masks[k] & bit) ++out.count[start];
                } else if (out.count[start] == 0) {
                    out.first[start] = base + q + 1;
                }
            }
        }
    }
    return out;
}
} // namespace findhap_pedigree
