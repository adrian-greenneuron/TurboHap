#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>
#include <new>
#include <sys/mman.h>
#include <unistd.h>

namespace lm_lookup_storage {
inline size_t page_size() {
    static const size_t value = size_t(sysconf(_SC_PAGESIZE));
    return value;
}
// Give each optional index allocation an explicit OS lifetime. Repeated segment
// passes must not accumulate freed index buffers in the allocator's arenas.
template<class T>
class Array {
    T* data_ = nullptr;
    size_t count_ = 0, bytes_ = 0;
    void release() {
        if (data_) munmap(data_, bytes_);
        data_ = nullptr; count_ = bytes_ = 0;
    }
public:
    Array() = default;
    Array(const Array&) = delete;
    Array& operator=(const Array&) = delete;
    ~Array() { release(); }
    void assign(size_t count, T value) {
        if (count != count_) {
            release();
            if (!count) return;
            const size_t page = page_size();
            const size_t bytes = ((count * sizeof(T) + page - 1) / page) * page;
            void* memory = mmap(nullptr, bytes, PROT_READ | PROT_WRITE,
                                MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
            if (memory == MAP_FAILED) throw std::bad_alloc();
            data_ = static_cast<T*>(memory); count_ = count; bytes_ = bytes;
        }
        std::fill_n(data_, count, value);
    }
    size_t size() const { return count_; }
    size_t allocated_bytes() const { return bytes_; }
    T& operator[](size_t index) { return data_[index]; }
    const T& operator[](size_t index) const { return data_[index]; }
};
}

// Bounded LM counterpart of OrderedCandidateLookup. Allele signatures stay in
// CandidateIndex; this index stores only planes, reverse positions and 16-bit
// tree winners. Every tree tie retains the earliest original order position.
class LowMemoryCandidateLookup {
    using Position = uint16_t;
    int width_, count_ = 0, capacity_ = 0;
    bool track_frequencies_ = false;
    const int* order_ = nullptr;
    const int* frequencies_ = nullptr;
    const void* entries_ = nullptr;
    using Signature = std::array<uint64_t, 2>;
    Signature (*read_signature_)(const void*, int) = nullptr;
    lm_lookup_storage::Array<Position> positions_, minima_;
    lm_lookup_storage::Array<uint64_t> zero_, two_, active_;

    Position winner(Position a, Position b) const {
        if (!a) return b;
        if (!b) return a;
        const int af = frequencies_[order_[a] - 1], bf = frequencies_[order_[b] - 1];
        return af < bf || (af == bf && a < b) ? a : b;
    }
    void update_minimum(int position) {
        if (!track_frequencies_) return;
        int leaf = capacity_ + position - 1;
        minima_[leaf] = order_[position] ? Position(position) : 0;
        for (leaf /= 2; leaf; leaf /= 2)
            minima_[leaf] = winner(minima_[leaf * 2], minima_[leaf * 2 + 1]);
    }
    void change_signature(int position, Signature previous, Signature current) {
        const size_t word = size_t(position - 1) / 64;
        const uint64_t bit = uint64_t(1) << ((position - 1) % 64);
        const size_t words = active_.size();
        uint64_t changes = previous[0] ^ current[0];
        while (changes) {
            zero_[size_t(__builtin_ctzll(changes)) * words + word] ^= bit;
            changes &= changes - 1;
        }
        changes = previous[1] ^ current[1];
        while (changes) {
            two_[size_t(__builtin_ctzll(changes)) * words + word] ^= bit;
            changes &= changes - 1;
        }
    }
    Signature signature(int hap) const {
        return hap ? read_signature_(entries_, hap) : Signature{};
    }
public:
    explicit LowMemoryCandidateLookup(int width) : width_(width) {}
    static size_t storage_bound(int width, int count) {
        if (count < 0 || count > UINT16_MAX) return SIZE_MAX;
        size_t capacity = 64;
        while (capacity < size_t(count)) capacity *= 2;
        const size_t words = capacity / 64;
        return (2 * size_t(width) * words + words) * sizeof(uint64_t) +
               (3 * capacity + 1) * sizeof(Position) + 5 * (lm_lookup_storage::page_size() - 1);
    }
    size_t resident_bytes() const {
        return positions_.allocated_bytes() + minima_.allocated_bytes() +
               zero_.allocated_bytes() + two_.allocated_bytes() + active_.allocated_bytes();
    }
    template<class Entries>
    bool prepare(const int* order, const int* frequencies, int count, const Entries& entries,
                 bool track_frequencies = true) {
        if (count > UINT16_MAX) return false;
        order_ = order; frequencies_ = frequencies; entries_ = &entries;
        read_signature_ = [](const void* pointer, int hap) -> Signature {
            const auto& value = (*static_cast<const Entries*>(pointer))[hap - 1];
            return {value.zero, value.two};
        };
        const bool enable_minima = track_frequencies && !track_frequencies_;
        track_frequencies_ = track_frequencies;
        if (enable_minima && capacity_) {
            minima_.assign(size_t(capacity_) * 2, 0);
            for (int position = 1; position <= count_; ++position)
                if (order_[position]) minima_[capacity_ + position - 1] = Position(position);
            for (int node = capacity_ - 1; node; --node)
                minima_[node] = winner(minima_[node * 2], minima_[node * 2 + 1]);
        }
        if (count == count_) return true;
        if (count > capacity_) {
            capacity_ = 64;
            while (capacity_ < count) capacity_ *= 2;
            const size_t words = size_t(capacity_) / 64;
            zero_.assign(size_t(width_) * words, 0);
            two_.assign(size_t(width_) * words, 0);
            active_.assign(words, 0);
            positions_.assign(capacity_ + 1, 0);
            if (track_frequencies_) minima_.assign(size_t(capacity_) * 2, 0);
            count_ = 0;
        }
        for (int position = count_ + 1; position <= count; ++position) {
            const int hap = order_[position];
            if (!hap) continue;
            if (hap < 0 || hap > count || positions_[hap]) return false;
            positions_[hap] = Position(position);
            active_[size_t(position - 1) / 64] |= uint64_t(1) << ((position - 1) % 64);
            change_signature(position, {}, {entries[hap - 1].zero, entries[hap - 1].two});
            update_minimum(position);
        }
        count_ = count;
        return true;
    }
    // CandidateIndex snapshots old alleles before updating its keyed entry.
    void refresh(int hap, uint64_t old_zero, uint64_t old_two) {
        if (hap <= count_ && positions_[hap])
            change_signature(positions_[hap], {old_zero, old_two}, signature(hap));
    }
    template<class Entries>
    void changed_positions(int first, int second, const Entries&) {
        if (first == second) {
            if (first > 0 && first <= count_) update_minimum(first);
            return;
        }
        // The caller has swapped these order positions. Read both pre-swap
        // signatures before updating either reverse position or plane.
        const Signature old_first = signature(order_[second]);
        const Signature old_second = signature(order_[first]);
        if (first > 0 && first <= count_) {
            const int hap = order_[first];
            if (hap) positions_[hap] = Position(first);
            change_signature(first, old_first, old_second);
            update_minimum(first);
        }
        if (second > 0 && second <= count_) {
            const int hap = order_[second];
            if (hap) positions_[hap] = Position(second);
            change_signature(second, old_second, old_first);
            update_minimum(second);
        }
    }
    void frequency_changed(int hap) {
        if (hap <= count_ && positions_[hap]) update_minimum(positions_[hap]);
    }
    int next(int first, int last, uint64_t query_zero, uint64_t query_two) const {
        if (first > last) return last + 1;
        if (order_[first]) {
            const Signature value = signature(order_[first]);
            if (!((query_zero & value[1]) | (query_two & value[0]))) return first;
        }
        const size_t words = active_.size();
        for (size_t word = size_t(first - 1) / 64; word <= size_t(last - 1) / 64; ++word) {
            uint64_t candidates = active_[word];
            if (word == size_t(first - 1) / 64) candidates &= UINT64_MAX << ((first - 1) % 64);
            uint64_t sites = query_zero;
            while (sites && candidates) {
                candidates &= ~two_[size_t(__builtin_ctzll(sites)) * words + word];
                sites &= sites - 1;
            }
            sites = query_two;
            while (sites && candidates) {
                candidates &= ~zero_[size_t(__builtin_ctzll(sites)) * words + word];
                sites &= sites - 1;
            }
            if (candidates) return std::min(last + 1, int(word * 64 + __builtin_ctzll(candidates) + 1));
        }
        return last + 1;
    }
    void minimum(int first, int last, int& frequency, int& position) const {
        Position best = 0;
        for (int left = capacity_ + first - 1, right = capacity_ + last;
             left < right; left /= 2, right /= 2) {
            if (left & 1) best = winner(best, minima_[left++]);
            if (right & 1) best = winner(best, minima_[--right]);
        }
        if (best && frequencies_[order_[best] - 1] < frequency) {
            frequency = frequencies_[order_[best] - 1];
            position = best;
        }
    }
};
