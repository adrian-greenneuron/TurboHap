#pragma once

#include <array>
#include <cstdint>
#include <cstring>
#include <memory>
#include <stdexcept>

// Immutable parsed records. Common four-state rows use two bits per locus;
// every other row keeps its exact input bytes. Records start on byte boundaries.
class GenotypeCache {
    size_t records_, capacity_, count_ = 0, used_ = 0;
    std::unique_ptr<uint8_t[]> data_, packed_;
    std::unique_ptr<size_t[]> offsets_;

    static bool packable(const int8_t* row, size_t length) {
        uint8_t other = 0;
        for (size_t l = 0; l < length; ++l)
            other |= row[l] != 0 && row[l] != 1 && row[l] != 2 && row[l] != 5;
        return other == 0;
    }

    static uint8_t code(int8_t value) {
        return value == 5 ? 3 : static_cast<uint8_t>(value);
    }

public:
    GenotypeCache(size_t records, size_t bytes)
        : records_(records), capacity_(bytes), data_(new uint8_t[bytes]),
          packed_(new uint8_t[records]), offsets_(new size_t[records]) {}

    static size_t encoded_bytes(const int8_t* row, size_t length) {
        return packable(row, length) ? length / 4 + (length % 4 != 0) : length;
    }

    void append(const int8_t* row, size_t length) {
        const bool packed = packable(row, length);
        const size_t bytes = packed ? length / 4 + (length % 4 != 0) : length;
        if (count_ >= records_ || bytes > capacity_ - used_)
            throw std::length_error("genotypes.txt changed while loading the cache");
        offsets_[count_] = used_;
        packed_[count_++] = packed;
        uint8_t* out = data_.get() + used_;
        if (packed) {
            size_t l = 0;
            for (; length - l >= 4; l += 4)
                out[l / 4] = code(row[l]) | (code(row[l + 1]) << 2) |
                             (code(row[l + 2]) << 4) | (code(row[l + 3]) << 6);
            if (l < length) {
                uint8_t tail = 0;
                const size_t byte = l / 4;
                for (int shift = 0; l < length; ++l, shift += 2)
                    tail |= code(row[l]) << shift;
                out[byte] = tail;
            }
        } else {
            std::memcpy(out, row, length);
        }
        used_ += bytes;
    }

    const int8_t* row(size_t record, size_t length, int8_t* scratch) const {
        const uint8_t* in = data_.get() + offsets_[record];
        if (!packed_[record]) return reinterpret_cast<const int8_t*>(in);
        static constexpr auto decoded = [] {
            std::array<std::array<int8_t, 4>, 256> table{};
            for (int byte = 0; byte < 256; ++byte)
                for (int l = 0; l < 4; ++l) {
                    const int value = (byte >> (2 * l)) & 3;
                    table[byte][l] = value == 3 ? 5 : value;
                }
            return table;
        }();
        size_t l = 0;
        for (; length - l >= 4; l += 4)
            std::memcpy(scratch + l, decoded[in[l / 4]].data(), 4);
        for (; l < length; ++l) {
            const uint8_t value = (in[l / 4] >> (2 * (l % 4))) & 3;
            scratch[l] = value == 3 ? 5 : value;
        }
        return scratch;
    }

    bool complete() const { return count_ == records_ && used_ == capacity_; }
    size_t bytes() const { return capacity_; }
};
