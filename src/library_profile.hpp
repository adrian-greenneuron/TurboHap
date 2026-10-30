#pragma once

// Optional serial-library diagnostic only. Ordinary builds contain no sampler.
// Deterministically hashed call indices avoid selecting the same periodic row.
#ifdef FINDHAP_LIBRARY_PROFILE
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <time.h>

namespace turbohap_library_profile {
struct Counter {
    const char* name;
    uint64_t calls = 0, samples = 0;
    double wall = 0;
};
inline Counter counters[] = {{"found_format"}, {"found_submit"}, {"frequency_count"},
                             {"list_format"}, {"list_submit"}, {"empty_timer"}};
inline bool enabled = false;
inline double seconds(clockid_t id) {
    timespec t{}; clock_gettime(id, &t);
    return t.tv_sec + t.tv_nsec * 1e-9;
}
class Sample {
    Counter* counter_ = nullptr;
    double wall_ = 0;
public:
    explicit Sample(int category) {
        if (!enabled) return;
        auto& c = counters[category];
        uint64_t h = ++c.calls + UINT64_C(0x9e3779b97f4a7c15);
        h = (h ^ (h >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
        h = (h ^ (h >> 27)) * UINT64_C(0x94d049bb133111eb);
        if (((h ^ (h >> 31)) & 1023) != 0) return;
        counter_ = &c; ++c.samples;
        wall_ = seconds(CLOCK_MONOTONIC);
    }
    void stop() {
        if (!counter_) return;
        const double end = seconds(CLOCK_MONOTONIC);
        counter_->wall += end - wall_;
        counter_ = nullptr;
    }
    ~Sample() { stop(); }
};
inline void calibrate() {
    enabled = true;
    for (int i = 0; i < 1000000; ++i) { Sample empty(5); }
    enabled = false;
}
inline void save() {
    const char* path = std::getenv("FINDHAP_LIBRARY_PROFILE_PATH");
    if (!path) return;
    FILE* f = std::fopen(path, "w");
    if (!f) { std::perror("library profile"); std::exit(100); }
    std::fprintf(f, "[\n");
    for (unsigned i = 0; i < sizeof(counters)/sizeof(*counters); ++i) {
        const auto& c = counters[i];
        std::fprintf(f, "%s{\"category\":\"%s\",\"calls\":%llu,\"samples\":%llu,"
                     "\"sample_wall_seconds\":%.9f}",
                     i ? ",\n" : "", c.name, (unsigned long long)c.calls,
                     (unsigned long long)c.samples, c.wall);
    }
    std::fprintf(f, "\n]\n"); std::fclose(f);
}
}
#define FINDHAP_LIBRARY_SAMPLE(name, category) turbohap_library_profile::Sample name(category)
#define FINDHAP_LIBRARY_SAMPLE_END(name) name.stop()
#define FINDHAP_LIBRARY_ENABLE(value) turbohap_library_profile::enabled = value
#define FINDHAP_LIBRARY_SAVE() turbohap_library_profile::save()
#define FINDHAP_LIBRARY_CALIBRATE() turbohap_library_profile::calibrate()
#else
#define FINDHAP_LIBRARY_SAMPLE(name, category)
#define FINDHAP_LIBRARY_SAMPLE_END(name)
#define FINDHAP_LIBRARY_ENABLE(value)
#define FINDHAP_LIBRARY_SAVE()
#define FINDHAP_LIBRARY_CALIBRATE()
#endif
