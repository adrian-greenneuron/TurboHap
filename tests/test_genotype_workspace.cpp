#include "../src/genotype_workspace.hpp"
#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <random>
#include <omp.h>

static void require(bool condition) {
    if (!condition) {
        std::cerr << "FAIL: genotype workspace changed a value or segment boundary\n";
        std::exit(1);
    }
}

// Read every value back against a separate dense oracle, including after the
// chromosome buffer has been discarded. Input batches deliberately end inside
// the animal list, and segments end inside packed bytes.
static void check_chromosomes(int loci, int width, int chromosome_count, size_t budget,
                              size_t workers = 1, size_t tile_budget = 128 * 1024 * 1024) {
    const size_t records = 17;
    std::vector<int> starts(1, 0), lengths(1, 0), chromosomes;
    for (int first = 0; first < loci; first += width) {
        starts.push_back(first + 1);
        lengths.push_back(std::min(width, loci - first));
    }
    const size_t segments = lengths.size() - 1;
    for (size_t s = 0; s < segments; ++s)
        chromosomes.push_back(1 + s * chromosome_count / segments);
    ChromosomeWorkspace workspace(records, loci, starts.data(), lengths.data(),
                                  segments, chromosomes.data(), budget);
    require(workspace.packed());
    require(workspace.input_batch_size() > 0);
    require(workspace.input_batch_size() < records);
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
    require(workspace.write_bytes() == workspace.bytes());
    for (int iteration = 0; iteration < 4; ++iteration) {
        const auto written_before = workspace.write_bytes();
        for (size_t first = 0; first < segments;) {
            const size_t end = workspace.tile_end(first, workers, tile_budget);
            require(end > first && end <= segments);
            if (end < segments) require(chromosomes[end] != chromosomes[end - 1]);
            size_t tile_bytes = 0;
            for (size_t s = first; s < end; ++s)
                tile_bytes += records * ((lengths[s + 1] + 3) / 4);
            require(tile_bytes <= tile_budget || chromosomes[first] == chromosomes[end - 1]);
            workspace.begin_tile(first, end);
            #pragma omp parallel for num_threads(4) schedule(dynamic, 1)
            for (size_t s = first; s < end; ++s) {
                std::vector<int8_t> scratch(lengths[s + 1] + 2, 99);
                for (size_t n = 0; n < records; ++n) {
                    int8_t* actual = workspace.segment(n, s, scratch.data() + 1);
                    int8_t* reference = expected.data() + n * loci + starts[s + 1] - 1;
                    require(std::equal(actual, actual + lengths[s + 1], reference));
                    require(scratch.front() == 99 && scratch.back() == 99);
                    // Leave holes between dirty records and entire segments
                    // untouched. The last visit checks without writing.
                    if (iteration < 3 && (n + s + iteration) % 3 == 0) {
                        actual[(n + iteration) % lengths[s + 1]] = 5;
                        reference[(n + iteration) % lengths[s + 1]] = 5;
                        workspace.save_segment(n, s, actual);
                    }
                }
            }
            workspace.end_tile(end);
            first = end;
        }
        workspace.finish_population();
        if (iteration == 3) require(workspace.write_bytes() == written_before);
    }
    require(workspace.read_bytes() == 4 * workspace.bytes());
}

static void check_batch_boundaries() {
    // Three uneven chromosomes (1, 2, 1 segments), ten bytes per segment.
    const int starts[] = {0, 1, 5, 9, 13}, lengths[] = {0, 4, 4, 4, 4};
    const int chromosomes[] = {1, 2, 2, 3};
    ChromosomeWorkspace workspace(10, 16, starts, lengths, 4, chromosomes);
    require(workspace.tile_end(0, 1, 40) == 1);
    require(workspace.tile_end(0, 2, 40) == 3); // complete second chromosome
    require(workspace.tile_end(0, 4, 40) == 4);
    require(workspace.tile_end(0, 4, 30) == 3); // inclusive byte limit
    require(workspace.tile_end(0, 4, 29) == 1);
    require(workspace.tile_end(1, 4, 19) == 3); // oversized chromosome stays whole
    require(workspace.tile_end(3, 10, 40) == 4); // short final batch
    GenotypeWorkspace resident(10, 16, starts, lengths, 4);
    require(resident.tile_end(0, 1, 1) == 4); // Turbo still exposes all segments
}

static void check_rolling(size_t requested, size_t budget) {
    const int records = 17;
    const int lengths[] = {0, 1, 3, 7, 16, 24, 36, 44, 125};
    const int chromosomes[] = {1, 1, 1, 2, 2, 3, 4, 4};
    const size_t segments = 8;
    int starts[9] = {0, 1};
    for (size_t s = 1; s < segments; ++s) starts[s + 1] = starts[s] + lengths[s];
    const int loci = starts[8] - 1 + lengths[8];
    ChromosomeWorkspace workspace(records, loci, starts, lengths, segments, chromosomes, 150);
    const int8_t states[] = {0, 1, 2, 5};
    std::vector<int8_t> expected(records * loci);
    for (size_t i = 0; i < expected.size(); ++i) expected[i] = states[(i * 11 + i / 7) % 4];
    for (size_t first = 0; first < records;) {
        const size_t count = std::min(workspace.input_batch_size(), records - first);
        workspace.begin_input(first, count);
        for (size_t n = first; n < first + count; ++n)
            workspace.store_row(n, expected.data() + n * loci);
        workspace.end_input();
        first += count;
    }
    workspace.finish_input();
    const int threads = workspace.population_workers(requested, budget);
    require(threads >= 1 && threads <= (int)requested && threads <= (int)segments);
    require(threads * workspace.population_buffer_bytes() <= budget || threads == 1);
    for (int pass = 0; pass < 4; ++pass) {
        const auto written = workspace.write_bytes();
        auto workers = workspace.make_population_workers(threads);
        #pragma omp parallel num_threads(threads)
        {
            auto& worker = *workers[omp_get_thread_num()];
            #pragma omp for schedule(dynamic, 1)
            for (size_t s = 0; s < segments; ++s) {
                worker.begin_segment(s);
                std::vector<int8_t> scratch(lengths[s + 1] + 2, 99);
                for (size_t n = 0; n < records; ++n) {
                    auto* actual = worker.segment(n, scratch.data() + 1);
                    auto* reference = expected.data() + n * loci + starts[s + 1] - 1;
                    require(std::equal(actual, actual + lengths[s + 1], reference));
                    require(scratch.front() == 99 && scratch.back() == 99);
                    if (pass < 3 && (n + s + pass) % 3 == 0) {
                        actual[(n + pass) % lengths[s + 1]] = 5;
                        reference[(n + pass) % lengths[s + 1]] = 5;
                        worker.save_segment(n, actual);
                    }
                }
                worker.end_segment();
            }
        }
        workspace.finish_population_workers(workers);
        require(workers.empty());
        if (pass == 3) require(workspace.write_bytes() == written);
    }
    require(workspace.read_bytes() == 4 * workspace.bytes());
    // The independent whole-chromosome reader must see every rolling mutation.
    for (size_t first = 0; first < segments;) {
        const size_t end = workspace.tile_end(first);
        workspace.begin_tile(first, end);
        for (size_t s = first; s < end; ++s) {
            std::vector<int8_t> scratch(lengths[s + 1]);
            for (size_t n = 0; n < records; ++n) {
                auto* actual = workspace.segment(n, s, scratch.data());
                require(std::equal(actual, actual + lengths[s + 1],
                                   expected.data() + n * loci + starts[s + 1] - 1));
            }
        }
        workspace.end_tile(end);
        first = end;
    }
}

int main() {
    check_batch_boundaries();
    for (size_t workers : {size_t(1), size_t(4), size_t(10)})
        for (size_t budget : {size_t(1), size_t(544), size_t(2175), size_t(2176), size_t(5440)})
            check_rolling(workers, budget);
    std::mt19937 rng(20261019);
    const int8_t states[] = {0, 1, 2, 5};
    int cases = 0;
    for (int loci : {1, 2, 3, 4, 5, 15, 16, 17, 31, 100, 257, 4097}) {
        for (int width : {1, 3, 4, 17, 100, 600}) {
            const int records = 17;
            std::vector<int> starts(1, 0), lengths(1, 0);
            for (int first = 0; first < loci; first += width) {
                starts.push_back(first + 1);
                lengths.push_back(std::min(width, loci - first));
            }
            const int segments = lengths.size() - 1;
            GenotypeWorkspace workspace(records, loci, starts.data(), lengths.data(), segments);
            require(workspace.packed());
            std::vector<int8_t> expected((size_t)records * loci);
            for (auto& value : expected) value = states[rng() % 4];
            for (int n = 0; n < records; ++n)
                workspace.store_row(n, expected.data() + (size_t)n * loci);

            // Concurrent neighbouring segment owners must not share packed
            // bytes. Persist conflict edits and read them back in later passes.
            for (int iteration = 0; iteration < 3; ++iteration) {
                #pragma omp parallel for num_threads(4) schedule(dynamic, 1)
                for (int s = 0; s < segments; ++s) {
                    std::vector<int8_t> scratch(lengths[s + 1] + 2, 99);
                    for (int n = 0; n < records; ++n) {
                        int8_t* actual = workspace.segment(n, s, scratch.data() + 1);
                        int8_t* reference = expected.data() + (size_t)n * loci + starts[s + 1] - 1;
                        require(std::equal(actual, actual + lengths[s + 1], reference));
                        require(scratch.front() == 99 && scratch.back() == 99);
                        for (int l = 0; l < lengths[s + 1]; ++l) {
                            if ((n + s + l + iteration) % 7 == 0)
                                actual[l] = reference[l] = 5;
                        }
                        workspace.save_segment(n, s, actual);
                    }
                }
            }
            ++cases;
        }
    }
    // Non-partitioned prior layouts keep shared-locus writes in dense storage.
    const int starts[] = {0, 1, 2}, lengths[] = {0, 4, 4};
    GenotypeWorkspace overlapping(1, 5, starts, lengths, 2);
    require(!overlapping.packed());
    int8_t row[] = {0, 1, 2, 5, 0}, scratch[4];
    overlapping.store_row(0, row);
    int8_t* first = overlapping.segment(0, 0, scratch);
    first[1] = 5;
    overlapping.save_segment(0, 0, first);
    require(overlapping.segment(0, 1, scratch)[0] == 5);
    const int chromosomes[] = {1, 1};
    ChromosomeWorkspace overlapping_external(1, 5, starts, lengths, 2, chromosomes);
    require(!overlapping_external.packed());
    overlapping_external.begin_input(0, 1);
    overlapping_external.store_row(0, row);
    overlapping_external.end_input();
    overlapping_external.finish_input();
    for (int pass = 0; pass < 2; ++pass) {
        require(overlapping_external.tile_end(0) == 2);
        overlapping_external.begin_tile(0, 2);
        auto* first = overlapping_external.segment(0, 0, scratch);
        first[1] = 5;
        overlapping_external.save_segment(0, 0, first);
        require(overlapping_external.segment(0, 1, scratch)[0] == 5);
        overlapping_external.end_tile(2);
        overlapping_external.finish_population();
    }
    require(overlapping_external.read_bytes() == 0 && overlapping_external.write_bytes() == 0);
    require(overlapping_external.population_buffer_bytes() == 0);
    auto dense_workers = overlapping_external.make_population_workers(1);
    dense_workers[0]->begin_segment(0);
    auto* dense_first = dense_workers[0]->segment(0, scratch);
    dense_first[1] = 2;
    dense_workers[0]->save_segment(0, dense_first);
    dense_workers[0]->end_segment();
    dense_workers[0]->begin_segment(1);
    require(dense_workers[0]->segment(0, scratch)[0] == 2);
    dense_workers[0]->end_segment();
    overlapping_external.finish_population_workers(dense_workers);
    require(overlapping_external.read_bytes() == 0 && overlapping_external.write_bytes() == 0);
    int chromosome_cases = 0;
    for (int loci : {1, 17, 257, 4097})
        for (int width : {1, 3, 17, 600})
            for (int chromosome_count : {1, 3, 30})
                for (size_t workers : {size_t(1), size_t(4), size_t(10)})
                    for (size_t tile_budget : {size_t(64), size_t(65536)}) {
                        check_chromosomes(loci, width, chromosome_count, loci / 2 + 1,
                                          workers, tile_budget);
                        ++chromosome_cases;
                    }
    std::cout << "PASS: " << cases << " resident layouts and " << chromosome_cases
              << " chromosome layouts and 15 rolling budgets with persistent edits; dense overlap fallbacks\n";
}
