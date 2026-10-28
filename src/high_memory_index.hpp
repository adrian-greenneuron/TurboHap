#pragma once

#include <algorithm>
#include <cstdint>
#include <limits>
#include <vector>

#ifndef FINDHAP_POP_MINIMUM
#define FINDHAP_POP_MINIMUM 0
#endif

// Population-pass-local lookup in the original order positions. Allele planes
// reject only known opposite alleles. A range-minimum tree retains the earliest
// minimum-frequency position, including every candidate skipped by the planes.
class OrderedCandidateLookup {
    int width_, count_ = 0, capacity_ = 0;
    bool track_frequencies_ = false;
    const int* order_ = nullptr;
    const int* frequencies_ = nullptr;
    std::vector<int> positions_;
    std::vector<uint64_t> zero_, two_, active_, minima_, slot_zero_, slot_two_;

    static uint64_t frequency_key(int frequency, int position) {
        return (uint64_t(uint32_t(frequency) ^ 0x80000000u) << 32) | uint32_t(position);
    }

    void update_minimum(int position) {
        if (!track_frequencies_) return;
        int leaf = capacity_ + position - 1;
        const int hap = order_[position];
        minima_[leaf] = hap ? frequency_key(frequencies_[hap - 1], position) : UINT64_MAX;
        for (leaf /= 2; leaf; leaf /= 2) {
            const uint64_t value = std::min(minima_[leaf * 2], minima_[leaf * 2 + 1]);
#if FINDHAP_POP_MINIMUM
            if (minima_[leaf] == value) break;
#endif
            minima_[leaf] = value;
        }
    }

    void set_signature(int position, uint64_t zero, uint64_t two) {
        const size_t word = size_t(position - 1) / 64;
        const uint64_t bit = uint64_t(1) << ((position - 1) % 64);
        const size_t words = active_.size();
        uint64_t changes = zero ^ slot_zero_[position - 1];
        while (changes) {
            const int locus = __builtin_ctzll(changes);
            zero_[size_t(locus) * words + word] ^= bit;
            changes &= changes - 1;
        }
        changes = two ^ slot_two_[position - 1];
        while (changes) {
            const int locus = __builtin_ctzll(changes);
            two_[size_t(locus) * words + word] ^= bit;
            changes &= changes - 1;
        }
        slot_zero_[position - 1] = zero;
        slot_two_[position - 1] = two;
    }

public:
    explicit OrderedCandidateLookup(int width) : width_(width) {}

    template<class Entries>
    bool prepare(const int* order, const int* frequencies, int count, const Entries& entries,
                 bool track_frequencies = true) {
        // Bound optional index memory independently of the user's haplotype
        // capacity; larger libraries retain the qualified scalar search.
        if (count > (1 << 20)) return false;
        order_ = order;
        frequencies_ = frequencies;
        const bool enable_minima = track_frequencies && !track_frequencies_;
        track_frequencies_ = track_frequencies;
        if (enable_minima && capacity_) {
            minima_.assign(size_t(capacity_) * 2, UINT64_MAX);
            for (int position = 1; position <= count_; ++position) {
                const int hap = order_[position];
                if (hap)
                    minima_[capacity_ + position - 1] =
                        frequency_key(frequencies_[hap - 1], position);
            }
            for (int node = capacity_ - 1; node; --node)
                minima_[node] = std::min(minima_[node * 2], minima_[node * 2 + 1]);
        }
        if (count == count_) return true;
        if (count > capacity_) {
            capacity_ = 64;
            while (capacity_ < count) capacity_ *= 2;
            const size_t words = size_t(capacity_) / 64;
            zero_.assign(size_t(width_) * words, 0);
            two_.assign(size_t(width_) * words, 0);
            active_.assign(words, 0);
            slot_zero_.assign(capacity_, 0);
            slot_two_.assign(capacity_, 0);
            if (track_frequencies_) minima_.assign(size_t(capacity_) * 2, UINT64_MAX);
            positions_.assign(capacity_ + 1, 0);
            count_ = 0;
        }
        for (int position = count_ + 1; position <= count; ++position) {
            const int hap = order[position];
            if (!hap) continue;
            // A non-permutation order retains the original scalar path.
            if (hap < 0 || hap > count || positions_[hap]) return false;
            positions_[hap] = position;
            active_[size_t(position - 1) / 64] |= uint64_t(1) << ((position - 1) % 64);
            set_signature(position, entries[hap - 1].zero, entries[hap - 1].two);
            update_minimum(position);
        }
        count_ = count;
        return true;
    }

    void refresh(int hap, uint64_t zero, uint64_t two) {
        if (hap <= count_ && positions_[hap]) set_signature(positions_[hap], zero, two);
    }

    template<class Entries>
    void changed_positions(int first, int second, const Entries& entries) {
        // A hit already at the minimum position does not move either slot.
        if (first == second) {
            if (first > 0 && first <= count_) update_minimum(first);
            return;
        }
        for (int position : {first, second}) {
            if (position <= 0 || position > count_) continue;
            const int hap = order_[position];
            if (hap) {
                positions_[hap] = position;
                set_signature(position, entries[hap - 1].zero, entries[hap - 1].two);
            } else {
                set_signature(position, 0, 0);
            }
            update_minimum(position);
        }
    }

    void frequency_changed(int hap) {
        if (hap <= count_ && positions_[hap]) update_minimum(positions_[hap]);
    }

    int next(int first, int last, uint64_t query_zero, uint64_t query_two) const {
        if (first > last) return last + 1;
        // Common haplotypes often occupy the first position. Test that one
        // signature before intersecting all allele planes for its whole word.
        if (order_[first] &&
            !((query_zero & slot_two_[first - 1]) | (query_two & slot_zero_[first - 1])))
            return first;
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
        if (first == last) {
            const int hap = order_[first];
            if (hap && frequencies_[hap - 1] < frequency) {
                frequency = frequencies_[hap - 1];
                position = first;
            }
            return;
        }
        uint64_t key = UINT64_MAX;
        for (int left = capacity_ + first - 1, right = capacity_ + last;
             left < right; left /= 2, right /= 2) {
            if (left & 1) key = std::min(key, minima_[left++]);
            if (right & 1) key = std::min(key, minima_[--right]);
        }
        if (key == UINT64_MAX) return;
        const int value = int(int64_t(key >> 32) - 0x80000000LL);
        if (value < frequency) {
            frequency = value;
            position = int(uint32_t(key));
        }
    }
};
