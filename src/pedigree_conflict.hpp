#pragma once
#include <cstdint>
#include <cstdlib>

// A pedigree near-match is accepted only when exactly one locus conflicts.
// Count short blocks so the compiler can vectorise, stopping after the second
// conflict. Do not read beyond the actual segment, including its partial tail.
inline bool pedigree_single_conflict(const int8_t* a, const int8_t* b, int length) {
    int count = 0;
    int i = 0;
    for (; i + 16 <= length; i += 16) {
        for (int k = 0; k < 16; ++k)
            count += std::abs(int(a[i+k]) - int(b[i+k])) > 1;
        if (count > 1) return false;
    }
    for (; i < length; ++i)
        count += std::abs(int(a[i]) - int(b[i])) > 1;
    return count == 1;
}
