#include "../src/genotype_cache.hpp"

#include <algorithm>
#include <cassert>
#include <iostream>
#include <vector>

int main() {
    std::vector<std::vector<int8_t>> records;
    const int8_t states[] = {0, 1, 2, 5};
    for (int pattern = 0; pattern < 256; ++pattern) {
        std::vector<int8_t> row(4);
        for (int l = 0; l < 4; ++l) row[l] = states[(pattern >> (2 * l)) & 3];
        records.push_back(row);
    }
    for (int length : {0, 1, 2, 3, 5, 7, 15, 16, 17, 127, 128, 129, 65537, 1048576}) {
        std::vector<int8_t> row(length);
        for (int l = 0; l < length; ++l) row[l] = states[l % 4];
        records.push_back(row);
    }
    // Preserve every possible parsed byte, including noncanonical codes.
    for (int value = -128; value <= 127; ++value)
        records.push_back({0, static_cast<int8_t>(value), 5, 1, 2});

    size_t bytes = 0;
    for (const auto& row : records) bytes += GenotypeCache::encoded_bytes(row.data(), row.size());
    GenotypeCache cache(records.size(), bytes);
    assert(!cache.complete());
    for (const auto& row : records) cache.append(row.data(), row.size());
    assert(cache.complete() && cache.bytes() == bytes);

    int failures = 0;
    #pragma omp parallel for num_threads(10) reduction(+:failures)
    for (size_t record = 0; record < records.size(); ++record) {
        const auto& expected = records[record];
        std::vector<int8_t> scratch(expected.size() + 2, -97);
        for (int repeat = 0; repeat < 3; ++repeat) {
            const int8_t* actual = cache.row(record, expected.size(), scratch.data() + 1);
            if (!std::equal(expected.begin(), expected.end(), actual) ||
                scratch.front() != -97 || scratch.back() != -97) ++failures;
            std::fill(scratch.begin(), scratch.end(), -97);
        }
    }
    assert(failures == 0);
    // A changed input must never write beyond the allocated cache.
    bool rejected = false;
    try { cache.append(states, 4); } catch (const std::length_error&) { rejected = true; }
    assert(rejected);
    GenotypeCache too_small(1, 0);
    rejected = false;
    try { too_small.append(states, 4); } catch (const std::length_error&) { rejected = true; }
    assert(rejected);
    GenotypeCache empty(0, 0);
    assert(empty.complete());
    std::cout << "PASS: " << records.size()
              << " lossless records, all int8 values, boundary guards, concurrent repeated reads and capacity checks\n";
}
