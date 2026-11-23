#pragma once
#include <immintrin.h>
#include <algorithm>
#include <cstdint>
#include <cstddef>

inline void count_expanded_row(const int8_t* row, size_t length, int64_t* frequencies) {
    size_t offset = 0;
    const auto zero = _mm256_setzero_si256();
    while (length - offset >= 32) {
        const size_t vectors = std::min<size_t>(255, (length - offset) / 32);
        __m256i counts[6] = {zero, zero, zero, zero, zero, zero};
        for (size_t v = 0; v < vectors; ++v, offset += 32) {
            const auto calls = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(row + offset));
            for (int k = 0; k < 6; ++k)
                counts[k] = _mm256_sub_epi8(counts[k], _mm256_cmpeq_epi8(calls, _mm256_set1_epi8(k)));
        }
        for (int k = 0; k < 6; ++k) {
            alignas(32) uint64_t totals[4];
            _mm256_store_si256(reinterpret_cast<__m256i*>(totals), _mm256_sad_epu8(counts[k], zero));
            frequencies[k] += totals[0] + totals[1] + totals[2] + totals[3];
        }
    }
    for (; offset < length; ++offset) ++frequencies[row[offset]];
}
