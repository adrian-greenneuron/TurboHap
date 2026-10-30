#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace pedigree_rematch {

struct Overlap {
    int previous_segment; // one based, in the original traversal order
    int previous_offset;
    int destination_offset;
    int length;
};

struct Plan {
    bool disjoint = true;
    std::vector<std::vector<Overlap>> segments;
};

// Only disjoint destination slices may own mutable library/genotype work
// independently. Previous slices may overlap: retain their original overwrite
// order, including the rule that an unknown allele does not erase a known one.
inline Plan make_plan(const int* starts, const int* lengths, int count,
                      const int* previous_starts, const int* previous_lengths,
                      int previous_count) {
    Plan plan;
    plan.segments.resize(count);
    int end = 0;
    for (int s = 1; s <= count; ++s) {
        if (starts[s] < end || lengths[s] < 0) plan.disjoint = false;
        end = starts[s] + lengths[s];
    }
    if (!plan.disjoint) return plan;
    bool previous_disjoint = true;
    end = 0;
    for (int p = 1; p <= previous_count; ++p) {
        if (previous_starts[p] < end || previous_lengths[p] < 0)
            previous_disjoint = false;
        end = previous_starts[p] + previous_lengths[p];
    }
    int first_previous = 1;
    for (int s = 1; s <= count; ++s) {
        const int begin = starts[s], finish = begin + lengths[s];
        if (previous_disjoint)
            while (first_previous <= previous_count &&
                   previous_starts[first_previous] + previous_lengths[first_previous] <= begin)
                ++first_previous;
        for (int p = previous_disjoint ? first_previous : 1; p <= previous_count; ++p) {
            if (previous_disjoint && previous_starts[p] >= finish) break;
            const int lo = std::max(begin, previous_starts[p]);
            const int hi = std::min(finish, previous_starts[p] + previous_lengths[p]);
            if (lo < hi)
                plan.segments[s - 1].push_back({p, lo - previous_starts[p], lo - begin, hi - lo});
        }
    }
    return plan;
}

template<class HapId>
inline int reconstruct(int8_t* destination, int length, int animal, int maxped,
                       const std::vector<Overlap>& overlaps, const HapId* previous_haps,
                       const int8_t* previous_library, const size_t* previous_offsets,
                       int previous_maxlen, int missing) {
    std::fill_n(destination, length, int8_t(5));
    for (const auto& overlap : overlaps) {
        const size_t entry = size_t(overlap.previous_segment - 1) * 2 * maxped
                           + size_t(animal - 1) * 2;
        const int h1 = previous_haps[entry], h2 = previous_haps[entry + 1];
        if (h1 <= 0 || h2 <= 0) continue;
        const int8_t* first = previous_library + previous_offsets[overlap.previous_segment - 1]
                            + size_t(h1 - 1) * previous_maxlen + overlap.previous_offset;
        const int8_t* second = previous_library + previous_offsets[overlap.previous_segment - 1]
                             + size_t(h2 - 1) * previous_maxlen + overlap.previous_offset;
        int8_t* output = destination + overlap.destination_offset;
        for (int l = 0; l < overlap.length; ++l)
            if (first[l] != 1 && second[l] != 1)
                output[l] = (first[l] + second[l]) / 2;
    }
    int known = 0;
    for (int l = 0; l < length; ++l) known += destination[l] != missing;
    return known;
}

} // namespace pedigree_rematch
