#include "../src/external_genotype_cache.hpp"
#include "../src/bounded_genotype_workspace.hpp"
#include "../src/genotype_cache.hpp"
#include <cstdlib>
#include <iostream>
#include <numeric>
#include <random>
#include <omp.h>

static void require(bool condition, const char* message) {
    if (!condition) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}

static void check_raw(size_t budget) {
    std::vector<std::vector<int8_t>> rows;
    const int8_t states[] = {0, 1, 2, 5};
    for (int byte = 0; byte < 256; ++byte) {
        std::vector<int8_t> row(4);
        for (int l = 0; l < 4; ++l) row[l] = states[(byte >> (l * 2)) & 3];
        rows.push_back(row);
    }
    for (int value = -128; value <= 127; ++value)
        rows.push_back({0, int8_t(value), 5, 1, 2});
    for (size_t length : {size_t(0), size_t(1), size_t(3), size_t(7), size_t(128), size_t(4097)}) {
        std::vector<int8_t> row(length);
        for (size_t l = 0; l < length; ++l) row[l] = states[l % 4];
        rows.push_back(row);
    }
    size_t bytes = 0, widest = 0;
    for (const auto& row : rows) {
        const size_t encoded = GenotypeCache::encoded_bytes(row.data(), row.size());
        bytes += encoded;
        widest = std::max(widest, encoded);
    }
    ExternalGenotypeCache cache(rows.size(), bytes, budget);
    for (const auto& row : rows) cache.append(row.data(), row.size());
    require(cache.complete(), "raw records incomplete");
    require(cache.resident_bytes() <= std::max(budget, widest), "append exceeded bounded payload");
    cache.finish_append();
    require(cache.write_bytes() == bytes && cache.resident_bytes() == 0, "raw file not flushed/released");

    auto check = [&](const size_t* records, size_t count) {
        int failures = 0;
        #pragma omp parallel for num_threads(4) reduction(+:failures)
        for (size_t j = 0; j < count; ++j) {
            const size_t record = records[j];
            if (record == ExternalGenotypeCache::absent) continue;
            const auto& expected = rows[record];
            std::vector<int8_t> scratch(expected.size() + 2, -97);
            const auto* actual = cache.row(record, expected.size(), scratch.data() + 1);
            if (!std::equal(expected.begin(), expected.end(), actual) ||
                scratch.front() != -97 || scratch.back() != -97) ++failures;
        }
        require(failures == 0, "raw bytes or scratch guards changed");
        require(cache.resident_bytes() <= std::max(budget, widest), "read exceeded bounded payload");
    };
    std::vector<size_t> order(rows.size());
    std::iota(order.begin(), order.end(), 0);
    for (size_t first = 0; first < rows.size();) {
        const size_t count = cache.prepare_sequential(first, rows.size() - first);
        require(count > 0, "sequential batch made no progress");
        check(order.data() + first, count);
        first += count;
    }
    require(cache.read_bytes() == bytes, "sequential raw pass skipped/repeated bytes");
    cache.release_batch();
    std::mt19937 rng(20261020);
    std::shuffle(order.begin(), order.end(), rng);
    order.insert(order.begin(), ExternalGenotypeCache::absent);
    order.push_back(order[17]); // repeated raw record in an output selection
    order.push_back(ExternalGenotypeCache::absent);
    for (size_t first = 0; first < order.size();) {
        const size_t count = cache.prepare_gather(order.data() + first, order.size() - first);
        require(count > 0, "gather batch made no progress");
        check(order.data() + first, count);
        first += count;
    }
    cache.release_batch();
    require(cache.resident_bytes() == 0, "raw read buffer retained after release");
    bool rejected = false;
    try { cache.append(states, 4); } catch (const std::length_error&) { rejected = true; }
    require(rejected, "sealed raw cache accepted a write");
    ExternalGenotypeCache too_small(1, 0, budget);
    rejected = false;
    try { too_small.append(states, 4); } catch (const std::length_error&) { rejected = true; }
    require(rejected, "raw capacity overflow was not rejected");
    ExternalGenotypeCache empty(0, 0, budget);
    empty.finish_append();
    require(empty.prepare_sequential(0, 0) == 0, "empty cache changed record count");
}

static void check_working(size_t budget, size_t requested) {
    constexpr size_t records = 37, segments = 7;
    const int lengths[] = {0, 1, 3, 7, 16, 31, 65, 101};
    const int chromosomes[] = {1, 1, 1, 2, 2, 3, 4};
    int starts[segments + 1] = {0, 1};
    for (size_t s = 1; s < segments; ++s) starts[s + 1] = starts[s] + lengths[s];
    const int loci = starts[segments] - 1 + lengths[segments];
    BoundedGenotypeWorkspace workspace(records, loci, starts, lengths, segments, chromosomes, 151, budget);
    require(workspace.packed(), "partitioned layout fell back to dense storage");
    const int8_t states[] = {0, 1, 2, 5};
    std::vector<int8_t> expected(records * loci);
    for (size_t i = 0; i < expected.size(); ++i) expected[i] = states[(i * 11 + i / 7) % 4];
    for (size_t first = 0; first < records;) {
        const size_t count = std::min(workspace.input_batch_size(), records - first);
        workspace.begin_input(first, count);
        #pragma omp parallel for num_threads(4)
        for (size_t n = first; n < first + count; ++n)
            workspace.store_row(n, expected.data() + n * loci);
        workspace.end_input();
        first += count;
    }
    workspace.finish_input();
    require(workspace.write_bytes() == workspace.bytes(), "working input bytes not written exactly once");
    const size_t threads = workspace.population_workers(requested);
    require(threads > 0 && threads <= requested, "invalid worker count");
    const size_t capacity = workspace.population_buffer_bytes(threads);
    require(threads * capacity <= budget || (threads == 1 && capacity == 26), "worker payload exceeded budget");
    for (int pass = 0; pass < 4; ++pass) {
        const auto written = workspace.write_bytes();
        auto workers = workspace.make_population_workers(threads);
        #pragma omp parallel num_threads(threads)
        {
            auto& worker = *workers[omp_get_thread_num()];
            #pragma omp for schedule(dynamic, 1)
            for (size_t s = 0; s < segments; ++s) {
                worker.begin_segment(s);
                std::vector<int8_t> scratch(lengths[s + 1] + 2, -97);
                // Skip records in early passes, revisit every record later,
                // then read backwards to force eviction and reload of edits.
                for (size_t j = 0; j < records; ++j) {
                    const size_t n = pass == 3 ? records - 1 - j : j;
                    if (pass < 2 && (n + s) % 3 == 0) continue;
                    auto* actual = worker.segment(n, scratch.data() + 1);
                    auto* reference = expected.data() + n * loci + starts[s + 1] - 1;
                    require(std::equal(actual, actual + lengths[s + 1], reference), "working edit lost across blocks/passes");
                    require(scratch.front() == -97 && scratch.back() == -97, "working unpack overwrote scratch guards");
                    if (pass < 3 && (n + s + pass) % 4 == 0) {
                        const size_t locus = (n + pass) % lengths[s + 1];
                        actual[locus] = reference[locus] = states[(n + pass) % 4];
                        worker.save_segment(n, actual);
                    }
                }
                worker.end_segment();
            }
        }
        workspace.finish_population_workers(workers);
        workspace.finish_population();
        require(workers.empty(), "worker buffers retained after pass");
        if (pass == 3) require(workspace.write_bytes() == written, "read-only pass wrote data");
    }
}

static void check_overlap() {
    const int starts[] = {0, 1, 2}, lengths[] = {0, 4, 4}, chromosomes[] = {1, 1};
    BoundedGenotypeWorkspace workspace(1, 5, starts, lengths, 2, chromosomes, 1, 1);
    require(!workspace.packed(), "overlapping priors lost shared-locus fallback");
    const int8_t row[] = {0, 1, 2, 5, 0};
    int8_t scratch[4];
    workspace.begin_input(0, 1);
    workspace.store_row(0, row);
    workspace.end_input();
    workspace.finish_input();
    auto workers = workspace.make_population_workers(1);
    auto& worker = *workers[0];
    worker.begin_segment(0);
    auto* first = worker.segment(0, scratch);
    first[1] = 5;
    worker.save_segment(0, first);
    worker.end_segment();
    worker.begin_segment(1);
    require(worker.segment(0, scratch)[0] == 5, "overlapping segment did not see shared edit");
    worker.end_segment();
    workspace.finish_population_workers(workers);
}

int main() {
    for (size_t budget : {size_t(17), size_t(4096)}) check_raw(budget);
    for (size_t budget : {size_t(1), size_t(135), size_t(1024), size_t(8192)})
        for (size_t workers : {size_t(1), size_t(4)}) check_working(budget, workers);
    check_overlap();
    std::cout << "PASS: TurboHap LM lossless raw batches, unordered output gathers, bounded parallel workers, persistent edits and overlap fallback\n";
}
