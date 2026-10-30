#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace turbohap_output {
// Decimal integer formatting only. Float formatting remains with libc.
inline char* append_unsigned(char* destination, uint32_t value) {
    static constexpr char pairs[] =
        "0001020304050607080910111213141516171819"
        "2021222324252627282930313233343536373839"
        "4041424344454647484950515253545556575859"
        "6061626364656667686970717273747576777879"
        "8081828384858687888990919293949596979899";
    char digits[10];
    char* cursor = digits + sizeof(digits);
    while (value >= 100) {
        const uint32_t quotient = value / 100;
        const uint32_t remainder = value - quotient * 100;
        cursor -= 2;
        std::memcpy(cursor, pairs + remainder * 2, 2);
        value = quotient;
    }
    if (value >= 10) {
        cursor -= 2;
        std::memcpy(cursor, pairs + value * 2, 2);
    } else {
        *--cursor = static_cast<char>('0' + value);
    }
    const size_t length = static_cast<size_t>(digits + sizeof(digits) - cursor);
    std::memcpy(destination, cursor, length);
    return destination + length;
}

inline char* append_integer(char* destination, int value, int minimum_width) {
    char digits[11];
    char* end = digits;
    uint32_t magnitude = static_cast<uint32_t>(value);
    if (value < 0) {
        *end++ = '-';
        magnitude = uint32_t(0) - magnitude;
    }
    end = append_unsigned(end, magnitude);
    const size_t length = static_cast<size_t>(end - digits);
    const size_t padding = minimum_width > static_cast<int>(length)
        ? static_cast<size_t>(minimum_width) - length : 0;
    std::memset(destination, ' ', padding);
    destination += padding;
    std::memcpy(destination, digits, length);
    return destination + length;
}
} // namespace turbohap_output
