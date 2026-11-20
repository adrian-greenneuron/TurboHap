#pragma once

#include <algorithm>
#include <cstdint>
#include <numeric>
#include <vector>

namespace findhap::pedigree_placement {

// Choose nonempty contiguous ranges that minimise the largest estimated
// time across the fixed worker order. The returned segment cuts are one-based.
inline std::vector<int> weighted_cuts(const int* lengths, int segments,
                                      const std::vector<double>& rates) {
    const int workers = int(rates.size());
    if (!workers || segments < workers ||
        std::any_of(rates.begin(), rates.end(), [](double rate) { return !(rate > 0); }))
        return {};
    std::vector<std::int64_t> prefix(std::size_t(segments) + 1);
    for (int i = 0; i < segments; ++i) {
        if (lengths[i] <= 0) return {};
        prefix[i + 1] = prefix[i] + lengths[i];
    }
    std::vector<unsigned char> previous(prefix.size()), next(prefix.size());
    auto feasible = [&](double limit, std::vector<int>* parents) {
        std::fill(previous.begin(), previous.end(), 0);
        previous[0] = 1;
        for (int worker = 0; worker < workers; ++worker) {
            std::fill(next.begin(), next.end(), 0);
            int last_cut = -1;
            const double capacity = limit * rates[worker];
            const int last_end = segments - (workers - worker - 1);
            for (int end = worker + 1; end <= last_end; ++end) {
                if (previous[end - 1]) last_cut = end - 1;
                // The latest reachable cut minimises the work in this range.
                if (last_cut >= 0 && double(prefix[end] - prefix[last_cut]) <= capacity) {
                    next[end] = 1;
                    if (parents) (*parents)[std::size_t(worker) * prefix.size() + end] = last_cut;
                }
            }
            previous.swap(next);
        }
        return previous[segments] != 0;
    };
    double low = double(prefix.back()) / std::accumulate(rates.begin(), rates.end(), 0.0);
    double high = (double(prefix.back()) + 1) / *std::min_element(rates.begin(), rates.end());
    for (int trial = 0; trial < 44; ++trial) {
        const double middle = low + (high - low) / 2;
        if (feasible(middle, nullptr)) high = middle;
        else low = middle;
    }
    std::vector<int> parents(std::size_t(workers) * prefix.size());
    if (!feasible(high, &parents)) return {};
    std::vector<int> cuts(std::size_t(workers) + 1);
    int end = segments;
    cuts[workers] = segments + 1;
    for (int worker = workers - 1; worker >= 0; --worker) {
        end = parents[std::size_t(worker) * prefix.size() + end];
        cuts[worker] = end + 1;
    }
    return cuts;
}

} // namespace findhap::pedigree_placement
