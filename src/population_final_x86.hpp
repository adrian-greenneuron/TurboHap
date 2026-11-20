#pragma once

#include <cstdint>
#if FINDHAP_POP_FINAL_SCAN_X86 == 2
#include <cstdlib>
#endif
#include <immintrin.h>

namespace findhap_population {

#if FINDHAP_POP_FINAL_SCAN_X86 == 3
inline bool record_vector64(__m512i segment, __m512i haplotype, int offset,
                            int* fill, int& count) {
    const auto one = _mm512_set1_epi8(1);
    const auto difference = _mm512_subs_epi8(segment, haplotype);
    const uint64_t conflicts = _mm512_cmpgt_epi8_mask(difference, one) |
                               _mm512_cmpgt_epi8_mask(_mm512_set1_epi8(-1), difference);
    uint64_t positions = _mm512_cmpeq_epi8_mask(haplotype, one) & ~_mm512_cmpeq_epi8_mask(segment, one);
    // A rejected scalar search has already emitted fills strictly before its first conflict.
    if (conflicts) positions &= (conflicts & -conflicts) - 1;
    while (positions) {
        fill[count++] = offset + __builtin_ctzll(positions);
        positions &= positions - 1;
    }
    return conflicts == 0;
}
#endif

inline bool final_fill_positions(const int8_t* segment, const int8_t* haplotype,
                                 int length, int* fill, int& count) {
    count = 0;
    int locus = 0;
#if FINDHAP_POP_FINAL_SCAN_X86 == 3
    for (; length - locus >= 64; locus += 64) {
        if (!record_vector64(_mm512_loadu_si512(segment + locus), _mm512_loadu_si512(haplotype + locus),
                             locus, fill, count)) return false;
    }
    if (locus == length) return true;
    // Masked-off lanes read no input and compare two zeros, including at a page boundary.
    const __mmask64 active = (uint64_t{1} << (length - locus)) - 1;
    return record_vector64(_mm512_maskz_loadu_epi8(active, segment + locus),
                           _mm512_maskz_loadu_epi8(active, haplotype + locus), locus, fill, count);
#else
    const auto one = _mm256_set1_epi8(1);
    const auto minus_one = _mm256_set1_epi8(-1);
    for (; length - locus >= 32; locus += 32) {
        const auto s = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(segment + locus));
        const auto h = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(haplotype + locus));
        const auto difference = _mm256_subs_epi8(s, h);
        const auto conflicts = _mm256_or_si256(_mm256_cmpgt_epi8(difference, one),
                                               _mm256_cmpgt_epi8(minus_one, difference));
        const auto positions = _mm256_andnot_si256(_mm256_cmpeq_epi8(s, one), _mm256_cmpeq_epi8(h, one));
        const uint32_t rejected = static_cast<uint32_t>(_mm256_movemask_epi8(conflicts));
        uint32_t fills = static_cast<uint32_t>(_mm256_movemask_epi8(positions));
        if (rejected) fills &= (rejected & -rejected) - 1;
        while (fills) {
            fill[count++] = locus + __builtin_ctz(fills);
            fills &= fills - 1;
        }
        if (rejected) return false;
    }
    for (; locus < length; ++locus) {
        if (std::abs(int(segment[locus]) - int(haplotype[locus])) > 1) return false;
        if (haplotype[locus] == 1 && segment[locus] != 1) fill[count++] = locus;
    }
    return true;
#endif
}

} // namespace findhap_population
