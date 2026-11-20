#pragma once

// Diagnostic builds only. No counters, timing calls or trace branches remain
// in normal builds. Phase timestamps share CLOCK_MONOTONIC with the sampler.
#ifdef FINDHAP_PROFILE
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <array>
#include <atomic>
#include <cstring>
#include <sys/resource.h>
#include <time.h>
#include <omp.h>
#if defined(__linux__)
#include <sched.h>
#endif

namespace findhap_profile {
inline int logical_cpu() {
#if defined(__linux__)
    return sched_getcpu();
#else
    return -1; // macOS exposes no supported current-CPU identifier.
#endif
}
inline long peak_kib(const rusage& usage) {
#if defined(__APPLE__)
    return usage.ru_maxrss / 1024;
#else
    return usage.ru_maxrss;
#endif
}
inline double seconds(clockid_t clock) {
    timespec value{};
    clock_gettime(clock, &value);
    return value.tv_sec + value.tv_nsec * 1e-9;
}

struct Sink {
    struct Lane {
        std::array<char, 65536> bytes{};
        size_t used = 0;
        std::array<unsigned long long, 4> counts{};
    };
    FILE* file = nullptr;
    std::mutex mutex;
    std::array<Lane, 128> lanes;
    std::atomic<unsigned> next{0};
    Sink() {
        const char* path = std::getenv("FINDHAP_TRACE_PATH");
        if (path && *path) {
            file = std::fopen(path, "w");
            if (!file) { std::perror("profile trace"); std::exit(100); }
        }
    }
    unsigned lane() {
        thread_local const unsigned index = next.fetch_add(1);
        if (index >= lanes.size()) std::abort();
        return index;
    }
    void flush(Lane& buffer) {
        std::lock_guard<std::mutex> lock(mutex);
        if (buffer.used) std::fwrite(buffer.bytes.data(), 1, buffer.used, file);
        buffer.used = 0;
    }
    void append(const char* record, size_t length) {
        auto& buffer = lanes[lane()];
        if (length > buffer.bytes.size()) std::abort();
        if (length > buffer.bytes.size() - buffer.used) flush(buffer);
        std::memcpy(buffer.bytes.data() + buffer.used, record, length);
        buffer.used += length;
    }
    ~Sink() {
        if (!file) return;
        // Inference teams and the asynchronous writer have finished before exit.
        for (unsigned i = 0; i < next; ++i) {
            flush(lanes[i]);
            const auto& c = lanes[i].counts;
            std::fprintf(file, "{\"phase\":\"work_counts\",\"lane\":%u,\"population_rows\":%llu,\"eligible_rows\":%llu,\"candidate_visits\":%llu,\"exact_scans\":%llu}\n", i,c[0],c[1],c[2],c[3]);
        }
        std::fclose(file);
    }
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
        char record[1024];
        const int length = std::snprintf(record, sizeof record,
            "{\"phase\":\"%s\",\"step\":%d,\"iteration\":%d,\"segment\":%d,"
            "\"chromosome\":%d,\"loci\":%d,\"thread\":%d,\"logical_cpu\":%d,"
            "\"start_monotonic_seconds\":%.9f,\"end_monotonic_seconds\":%.9f,"
            "\"wall_seconds\":%.9f,\"owner_thread_cpu_seconds\":%.9f,"
            "\"haplotypes_before\":%d,\"haplotypes_after\":%d,\"process_peak_rss_so_far_kib\":%ld}\n",
            phase_, step_, iteration_, segment_, chromosome_, loci_, omp_get_thread_num(), logical_cpu(),
            wall_, end, end - wall_, cpu, before_, haplotypes_ ? *haplotypes_ : 0, peak_kib(usage));
        if (length < 0 || size_t(length) >= sizeof record) std::abort();
        writer.append(record, size_t(length));
        active_ = false;
    }
    ~Scope() { stop(); }
};
}
#define FINDHAP_PHASE(name, ...) findhap_profile::Scope name(__VA_ARGS__)
#define FINDHAP_END(name) name.stop()
#define FINDHAP_COUNT(category) (++findhap_profile::sink().lanes[findhap_profile::sink().lane()].counts[category])
#else
#define FINDHAP_PHASE(name, ...)
#define FINDHAP_END(name)
#define FINDHAP_COUNT(category)
#endif
