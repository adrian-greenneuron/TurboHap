#pragma once

// Diagnostic builds only. No counters, timing calls or trace branches remain
// in normal builds. Phase timestamps share CLOCK_MONOTONIC with the sampler.
#ifdef FINDHAP_PROFILE
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <sys/resource.h>
#include <time.h>
#include <omp.h>

namespace findhap_profile {
inline double seconds(clockid_t clock) {
    timespec value{};
    clock_gettime(clock, &value);
    return value.tv_sec + value.tv_nsec * 1e-9;
}

struct Sink {
    FILE* file = nullptr;
    std::mutex mutex;
    Sink() {
        const char* path = std::getenv("FINDHAP_TRACE_PATH");
        if (path && *path) {
            file = std::fopen(path, "w");
            if (!file) { std::perror("profile trace"); std::exit(100); }
        }
    }
    ~Sink() { if (file) std::fclose(file); }
};

inline Sink& sink() { static Sink writer; return writer; }

class Scope {
    const char* phase_;
    int step_, iteration_, segment_, chromosome_, loci_, before_;
    const int* haplotypes_;
    double wall_, cpu_;
    bool active_;
public:
    Scope(const char* phase, int step = 0, int iteration = 0, int segment = 0,
          int chromosome = 0, int loci = 0, const int* haplotypes = nullptr)
        : phase_(phase), step_(step), iteration_(iteration), segment_(segment),
          chromosome_(chromosome), loci_(loci), before_(haplotypes ? *haplotypes : 0),
          haplotypes_(haplotypes), wall_(0), cpu_(0), active_(sink().file != nullptr) {
#ifdef FINDHAP_LIGHT_PROFILE
        // Coarse phases only: no per-segment timers or I/O-call interception.
        // Build with the same optimisation/LTO flags as the timed executable.
        active_ = active_ && segment == 0;
#endif
        if (active_) {
            wall_ = seconds(CLOCK_MONOTONIC);
            cpu_ = seconds(CLOCK_THREAD_CPUTIME_ID);
        }
    }
    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;
    void stop() {
        if (!active_) return;
        const double end = seconds(CLOCK_MONOTONIC);
        const double cpu = seconds(CLOCK_THREAD_CPUTIME_ID) - cpu_;
        rusage usage{};
        getrusage(RUSAGE_SELF, &usage);
        auto& writer = sink();
        std::lock_guard<std::mutex> lock(writer.mutex);
        std::fprintf(writer.file,
            "{\"phase\":\"%s\",\"step\":%d,\"iteration\":%d,\"segment\":%d,"
            "\"chromosome\":%d,\"loci\":%d,\"thread\":%d,"
            "\"start_monotonic_seconds\":%.9f,\"end_monotonic_seconds\":%.9f,"
            "\"wall_seconds\":%.9f,\"owner_thread_cpu_seconds\":%.9f,"
            "\"haplotypes_before\":%d,\"haplotypes_after\":%d,\"process_peak_rss_so_far_kib\":%ld}\n",
            phase_, step_, iteration_, segment_, chromosome_, loci_, omp_get_thread_num(),
            wall_, end, end - wall_, cpu, before_, haplotypes_ ? *haplotypes_ : 0, usage.ru_maxrss);
        std::fflush(writer.file);
        active_ = false;
    }
    ~Scope() { stop(); }
};
}
#define FINDHAP_PHASE(name, ...) findhap_profile::Scope name(__VA_ARGS__)
#define FINDHAP_END(name) name.stop()
#else
#define FINDHAP_PHASE(name, ...)
#define FINDHAP_END(name)
#endif
