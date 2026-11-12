#pragma once
// Optional, coarse reporting events. Never called inside inference hot loops.
// Console presentation is independent of result files and optional JSON events.
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include "stdout_policy.hpp"

namespace turbohap_reporting {
class Events {
    FILE* file_ = nullptr;
    struct Peak {
        int count = 0, segment = 0, chromosome = 0, step = 0, iteration = 0;
    } step_peak_, run_peak_;
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
    void phase(const char* name, const char* state, int step = 0, int iteration = 0) const {
        if (modern_stdout()) stdout_clock().print("%s %s; step %d iteration %d\n",
                                                  name, state, step, iteration);
    }
    void emit(const char* kind, int step = 0, int iteration = 0,
              int segments = 0, const int* counts = nullptr, int stride = 1,
              int threads = 0, int genotyped = -1, int pedigree = -1, int loci = -1,
              int high = -1, int medium = -1,
              const int* chromosomes = nullptr, int configured_maxhap = 0) {
        const bool live = std::strcmp(kind, "iteration_end") == 0;
        const bool retained = std::strcmp(kind, "step_end") == 0;
        const bool complete = std::strcmp(kind, "run_complete") == 0;
        if (std::strcmp(kind, "step_start") == 0) step_peak_ = {};
        if (!file_ && !modern_stdout()) return;
        if (modern_stdout()) {
            if (std::strcmp(kind, "run_start") == 0)
                stdout_clock().print("Run configured: %d CPU threads\n", threads);
            else if (std::strcmp(kind, "step_start") == 0)
                stdout_clock().print("Step %d started\n", step);
            else if (std::strcmp(kind, "iteration_start") == 0)
                stdout_clock().print("Step %d iteration %d started\n", step, iteration);
        }
        int64_t total = 0;
        int maximum = 0;
        int maximum_segment = 0;
        // This is O(segments), only when reporting is requested. No animal or
        // genotype scan, counter updates, atomics or extra OpenMP barriers.
        if (counts) for (int s = 0; s < segments; ++s) {
            int value = counts[size_t(s) * stride];
            total += value;
            if (value > maximum) {
                maximum = value;
                maximum_segment = s + 1;
            }
        }
        // The live counters only increase within a step. The engine reduces
        // them once, in library output, after the final iteration_end hook.
        // Therefore these boundary observations capture the exact successful
        // run high-water mark without modifying any per-haplotype hot path.
        if (modern_stdout() && counts && live) {
            const Peak observed{maximum, maximum_segment,
                chromosomes && maximum_segment ? chromosomes[maximum_segment - 1] : 0,
                step, iteration};
            if (observed.count > step_peak_.count) step_peak_ = observed;
            if (observed.count > run_peak_.count) run_peak_ = observed;
            stdout_clock().print("TurboHap capacity: step %d iteration %d completed; live peak %d; segment %d chromosome %d; maxhap %d\n",
                step, iteration, step_peak_.count, step_peak_.segment,
                step_peak_.chromosome, configured_maxhap);
        } else if (modern_stdout() && counts && retained) {
            stdout_clock().print("TurboHap capacity: step %d live peak %d before pruning; segment %d chromosome %d; retained maximum %d\n",
                step, step_peak_.count, step_peak_.segment, step_peak_.chromosome, maximum);
        } else if (modern_stdout() && counts && complete) {
            stdout_clock().print("TurboHap capacity: run peak %d; step %d iteration %d segment %d chromosome %d; maxhap %d; headroom %lld slots\n",
                run_peak_.count, run_peak_.step, run_peak_.iteration,
                run_peak_.segment, run_peak_.chromosome, configured_maxhap,
                static_cast<long long>(configured_maxhap) - run_peak_.count);
        }
        if (!file_) return;
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
