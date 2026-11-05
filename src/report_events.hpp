#pragma once
// Optional, coarse reporting events. Never called inside inference hot loops.
// Legacy output files and stdout are deliberately untouched.
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstdint>

namespace turbohap_reporting {
class Events {
    FILE* file_ = nullptr;
public:
    Events() {
        const char* path = std::getenv("TURBOHAP_REPORT_EVENTS");
        if (path && *path) {
            file_ = std::fopen(path, "wx");
            if (file_) std::setvbuf(file_, nullptr, _IOFBF, 65536);
            else std::fprintf(stderr, "TurboHap report: cannot create optional event file; inference continues\n");
        }
    }
    Events(const Events&) = delete;
    Events& operator=(const Events&) = delete;
    ~Events() {
        if (file_ && std::fclose(file_) != 0)
            std::fprintf(stderr, "TurboHap report: optional event file close failed\n");
    }
    void emit(const char* kind, int step = 0, int iteration = 0,
              int segments = 0, const int* counts = nullptr, int stride = 1,
              int threads = 0, int genotyped = -1, int pedigree = -1, int loci = -1,
              int high = -1, int medium = -1) {
        if (!file_) return;
        int64_t total = 0;
        int maximum = 0;
        // This is O(segments), only when reporting is requested. No animal or
        // genotype scan, counter updates, atomics or extra OpenMP barriers.
        if (counts) for (int s = 0; s < segments; ++s) {
            int value = counts[size_t(s) * stride];
            total += value;
            if (value > maximum) maximum = value;
        }
        const double when = std::chrono::duration<double>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        if (std::fprintf(file_, "{\"schema_version\":1,\"event\":\"%s\",\"monotonic_seconds\":%.9f,"
            "\"step\":%d,\"iteration\":%d,\"segments\":%d,\"library_counts_available\":%s,"
            "\"segment_haplotypes_total\":%lld,\"maximum_segment_haplotypes\":%d,"
            "\"threads\":%d,\"genotype_records\":%d,\"pedigree_animals\":%d,\"loci\":%d,"
            "\"high_density_counter\":%d,\"medium_density_counter\":%d}\n",
            kind, when, step, iteration, segments, counts ? "true" : "false",
            static_cast<long long>(total), maximum, threads, genotyped, pedigree, loci, high, medium) < 0) {
            std::fprintf(stderr, "TurboHap report: optional event write failed; inference continues\n");
            std::fclose(file_); file_ = nullptr;
        }
    }
};
}
