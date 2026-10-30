#pragma once
#include <cstdio>
#include <cstring>
#include <charconv>
#include <cfenv>
#include <cmath>
#include "integer_output.hpp"

namespace turbohap_output {
inline bool fixed_five(char* destination, double value) {
    if (!std::isfinite(value)) return false;
    char digits[10];
    const auto converted = std::to_chars(digits, digits + sizeof(digits), value,
                                         std::chars_format::fixed, 5);
    if (converted.ec != std::errc()) return false;
    const size_t n = static_cast<size_t>(converted.ptr - digits);
    std::memset(destination, ' ', 10 - n);
    std::memcpy(destination + 10 - n, digits, n);
    return true;
}

// Match the first 58 bytes of the historical snprintf(..., 59, ...) header.
// Preserve printf's fixed conversion and minimum-width/overflow semantics.
template<bool FastFloat = false>
inline void haplist_header(char* destination, float hap_pct, double tot_pct,
                           int hap_num, int chrom, int step, int segment,
                           int start, int length) {
    char header[96]; // 20 float bytes + at most six 11-byte integers + separator.
    int prefix;
    if constexpr (FastFloat) {
        // to_chars uses the C-locale fixed conversion, without changing either
        // input value. Non-nearest rounding and exceptional/wide fields retain
        // libc so the established snprintf behaviour remains authoritative.
        prefix = std::fegetround() == FE_TONEAREST &&
                 fixed_five(header, static_cast<double>(hap_pct)) &&
                 fixed_five(header + 10, tot_pct) ? 20 : -1;
    } else {
        prefix = std::snprintf(header, 21, "%10.5f%10.5f", hap_pct, tot_pct);
    }
    if (prefix != 20) {
        // Nonstandard percentages may overflow the float fields. Preserve the
        // original truncation exactly rather than shifting/rounding fields.
        std::snprintf(destination, 59, "%10.5f%10.5f%10d%3d%3d%7d%8d%6d ",
                      hap_pct, tot_pct, hap_num, chrom, step, segment, start, length);
        return;
    }
    char* end = header + 20;
    end = append_integer(end, hap_num, 10);
    end = append_integer(end, chrom, 3);
    end = append_integer(end, step, 3);
    end = append_integer(end, segment, 7);
    end = append_integer(end, start, 8);
    end = append_integer(end, length, 6);
    *end = ' ';
    std::memcpy(destination, header, 58);
    destination[58] = '\0';
}
}
