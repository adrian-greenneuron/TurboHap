#pragma once

#include <cstddef>
#include <limits>
#include <stdexcept>
#include <string>

namespace findhap {

// These arrays must support both element indexing and byte differences.
template <typename T>
constexpr std::size_t checked_array_count(const char* name, std::size_t rows, std::size_t width) {
    constexpr std::size_t limit = std::numeric_limits<std::ptrdiff_t>::max() / sizeof(T);
    if (width != 0 && rows > limit / width)
        throw std::runtime_error(std::string(name) + " exceeds the supported array size");
    return rows * width;
}

} // namespace findhap
