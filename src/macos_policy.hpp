#pragma once
// Darwin scheduling intent, independent of Linux affinity or ARM server profiles.
#include "platform_policy.hpp"
#include <algorithm>
#include <cstdint>
#include <limits>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <sstream>
#include <string>
#ifndef FINDHAP_MAC_SCHEDULING
#define FINDHAP_MAC_SCHEDULING 1
#endif
#if defined(__APPLE__) && defined(__aarch64__)
#include <sys/sysctl.h>
#include <pthread/qos.h>
#endif
#include <omp.h>
#ifndef FINDHAP_HAVE_KMP_BLOCKTIME
#define FINDHAP_HAVE_KMP_BLOCKTIME 0
#endif
#if defined(__APPLE__) && defined(__aarch64__) && FINDHAP_HAVE_KMP_BLOCKTIME
extern "C" void kmp_set_blocktime(int);
extern "C" int kmp_get_blocktime();
#endif

namespace findhap::macos {
struct Topology { int performance = 0, efficiency = 0, cache_line = 0; };
inline int query(const char* key) {
#if defined(__APPLE__) && defined(__aarch64__)
    uint64_t value = 0; size_t size = sizeof(value);
    if (sysctlbyname(key, &value, &size, nullptr, 0) == 0 && (size == sizeof(uint32_t) || size == sizeof(uint64_t)) && value > 0 && value <= uint64_t(std::numeric_limits<int>::max()))
        return value;
#endif
    return 0;
}
inline Topology discover() {
    Topology t;
    const int levels = query("hw.nperflevels");
    if (levels > 0 && levels <= 16) {
        t.performance = query("hw.perflevel0.physicalcpu");
        for (int level = 1; level < levels; ++level)
            t.efficiency += query(("hw.perflevel" + std::to_string(level) + ".physicalcpu").c_str());
    }
    t.cache_line = query("hw.cachelinesize");
    return t;
}
inline Topology topology;
inline bool enabled = false;
inline int proposed_blocktime = -1;
inline bool wait_override() {
    return std::getenv("KMP_BLOCKTIME") || std::getenv("OMP_WAIT_POLICY") ||
           std::getenv("KMP_LIBRARY") || std::getenv("GOMP_SPINCOUNT");
}
inline int worker_blocktime() {
#if defined(__APPLE__) && defined(__aarch64__) && FINDHAP_HAVE_KMP_BLOCKTIME
    return kmp_get_blocktime();
#else
    return -1;
#endif
}
inline bool placement_override() {
    return std::getenv("OMP_PLACES") || std::getenv("OMP_PROC_BIND") ||
           std::getenv("GOMP_CPU_AFFINITY") || std::getenv("KMP_AFFINITY") ||
           std::getenv("KMP_HW_SUBSET");
}
inline void configure(bool automatic) {
    topology = discover();
    enabled = FINDHAP_MAC_SCHEDULING && automatic && !placement_override() && topology.performance > 0;
    proposed_blocktime = enabled && FINDHAP_HAVE_KMP_BLOCKTIME && !wait_override()
        ? FINDHAP_MAC_BLOCKTIME_MS : -1;
}
inline int default_threads(int runtime_max, bool explicit_request) {
    return enabled && !explicit_request && !std::getenv("OMP_NUM_THREADS")
        ? std::min(runtime_max, topology.performance) : runtime_max;
}
inline std::string json() {
#if defined(__APPLE__) && defined(__aarch64__)
    std::ostringstream out;
    out << "\"macos\":{\"performance_physical_cores\":" << topology.performance
        << ",\"efficiency_physical_cores\":" << topology.efficiency
        << ",\"cache_line_bytes\":" << topology.cache_line
        << ",\"native_optimisations\":" << (TURBOHAP_APPLE_M4_PROFILE ? "true" : "false")
        << ",\"build_profile\":\"" << TURBOHAP_PLATFORM_POLICY << "\""
        << ",\"blocktime_api_available\":" << (FINDHAP_HAVE_KMP_BLOCKTIME ? "true" : "false")
        << ",\"blocktime_proposed_ms\":" << (proposed_blocktime < 0 ? "null" : std::to_string(proposed_blocktime))
        << ",\"qos_proposed\":\"" << (enabled ? "user-initiated" : "inherit")
        << "\",\"placement\":\"OS-controlled; QoS is advisory\",\"hard_affinity\":false}";
    return out.str();
#else
    return "";
#endif
}
// Called by the main thread and at every parallel-region entry. A thread-local
// guard covers reused and newly created workers without inner-loop overhead.
inline void worker_enter() {
#if defined(__APPLE__) && defined(__aarch64__)
    if (!enabled) return;
    static thread_local bool attempted = false;
    if (!attempted) {
        attempted = true;
#if FINDHAP_HAVE_KMP_BLOCKTIME
        if (proposed_blocktime >= 0) kmp_set_blocktime(proposed_blocktime);
#endif
        const int error = pthread_set_qos_class_self_np(QOS_CLASS_USER_INITIATED, 0);
        if (error) std::fprintf(stderr, "TurboHap: worker QoS request failed: errno=%d; OS scheduling retained\n", error);
    }
    // Optional diagnostics only; never creates genetic output files.
    const char* path = std::getenv("FINDHAP_WORKER_TRACE_PATH");
    if (path && *path) {
        static std::mutex mutex;
        qos_class_t qos = QOS_CLASS_UNSPECIFIED; int relative = 0;
        const int error = pthread_get_qos_class_np(pthread_self(), &qos, &relative);
        std::lock_guard<std::mutex> lock(mutex);
        if (FILE* f = std::fopen(path, "a")) {
            std::fprintf(f, "{\"worker\":%d,\"team\":%d,\"qos\":%u,\"qos_error\":%d,\"blocktime_ms\":%d}\n",
                         omp_get_thread_num(), omp_get_num_threads(), unsigned(qos), error, worker_blocktime());
            std::fclose(f);
        }
    }
#endif
}
} // namespace findhap::macos
