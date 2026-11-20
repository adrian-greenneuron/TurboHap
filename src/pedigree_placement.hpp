#pragma once

#include "cpu_policy.hpp"
#include "pedigree_segment_partition.hpp"
#include "platform_policy.hpp"
#include <cstdio>

namespace findhap::pedigree_placement {

enum class Mode { automatic, fixed, weighted };

inline bool allow_default_placement = false;
inline bool inherit_placement = false;

inline Mode requested_mode(int threads, int chromosomes) {
    if (inherit_placement) return Mode::automatic;
    const char* mode = std::getenv("FINDHAP_PEDIGREE_ASSIGNMENT");
    if (!mode)
        return allow_default_placement && threads > 1 && chromosomes < threads ? Mode::weighted : Mode::automatic;
    if (std::strcmp(mode, "auto") == 0) return Mode::automatic;
    if (std::strcmp(mode, "fixed") == 0) return Mode::fixed;
    if (std::strcmp(mode, "weighted") == 0) return Mode::weighted;
    throw std::runtime_error("invalid experimental pedigree assignment; use auto, fixed or weighted");
}

inline std::vector<placement::Cpu> fixed_cores(int workers, Mode mode) {
    if (mode == Mode::automatic) return {};
    if (placement::external_binding()) {
        std::fprintf(stderr, "TurboHap pedigree placement: caller's OpenMP binding retained\n");
        return {};
    }
    try {
        auto topology = placement::discover(true);
        std::sort(topology.cpus.begin(), topology.cpus.end(),
                  [](const placement::Cpu& a, const placement::Cpu& b) {
            if (a.capacity != b.capacity) return a.capacity > b.capacity;
            return a.id < b.id;
        });
        std::set<std::vector<int>> physical;
        std::vector<placement::Cpu> result;
        for (auto& cpu : topology.cpus) {
            if (cpu.id < 0 || cpu.id >= CPU_SETSIZE || !cpu.capacity || !cpu.kind ||
                !std::binary_search(cpu.siblings.begin(), cpu.siblings.end(), cpu.id))
                break;
            if (physical.insert(cpu.siblings).second) {
                result.push_back(std::move(cpu));
                if (int(result.size()) == workers) return result;
            }
        }
        std::fprintf(stderr, "TurboHap pedigree placement: insufficient permitted physical cores\n");
    } catch (const std::exception& error) {
        std::fprintf(stderr, "TurboHap pedigree placement: %s\n", error.what());
    }
    return {};
}

inline std::vector<double> relative_rates(const std::vector<placement::Cpu>& cores) {
    if (cores.empty()) return {};
    const char* measured = std::getenv("FINDHAP_PEDIGREE_SLOW_CORE_RATE");
    std::vector<double> rates;
    rates.reserve(cores.size());
    const auto fastest_kind = cores.front().kind;
    std::set<std::uint64_t> kinds;
    for (const auto& core : cores) kinds.insert(core.kind);
    // Measured from equal-work pedigree passes on the two GB10 core types.
    constexpr std::uint64_t cortex_x925 = UINT64_C(0x4100d850), cortex_a725 = UINT64_C(0x4100d870);
    const bool use_spark_rate = TURBOHAP_NEON_WORKLOAD_PROFILE && kinds.size() == 2 &&
        fastest_kind == cortex_x925 && kinds.count(cortex_a725);
    double slow_core_rate = 0.627;
    if (!measured) {
        if (!use_spark_rate) {
            for (const auto& core : cores) rates.push_back(double(core.capacity) / cores.front().capacity);
            return rates;
        }
    } else {
        char* end;
        slow_core_rate = std::strtod(measured, &end);
        if (end == measured || *end || !(slow_core_rate > 0 && slow_core_rate <= 1))
            throw std::runtime_error("experimental slower-core rate must be greater than zero and at most one");
        if (kinds.size() != 2)
            throw std::runtime_error("a calibrated slower-core rate requires two core types");
    }
    for (const auto& core : cores) rates.push_back(core.kind == fastest_kind ? 1.0 : slow_core_rate);
    return rates;
}

class ScopedCpu {
    cpu_set_t inherited_{};
    bool pinned_ = false;

public:
    ScopedCpu() = default;
    ScopedCpu(const ScopedCpu&) = delete;
    ScopedCpu& operator=(const ScopedCpu&) = delete;
    ~ScopedCpu() { restore(); }

    bool bind(int cpu) {
        if (sched_getaffinity(0, sizeof(inherited_), &inherited_) || !CPU_ISSET(cpu, &inherited_))
            return false;
        cpu_set_t target;
        CPU_ZERO(&target);
        CPU_SET(cpu, &target);
        pinned_ = sched_setaffinity(0, sizeof(target), &target) == 0;
        return pinned_ && sched_getcpu() == cpu;
    }

    bool restore() {
        if (pinned_) pinned_ = sched_setaffinity(0, sizeof(inherited_), &inherited_) != 0;
        return !pinned_;
    }
};

struct Worker {
    int before = -1;
    int after = -1;
    bool restored = true;
};

inline void report(const std::vector<Worker>& team, const std::vector<placement::Cpu>& cores,
                   int step, int iteration, int segments, bool pinned, const std::vector<int>& cuts) {
    if (std::any_of(team.begin(), team.end(), [](const Worker& worker) { return !worker.restored; }))
        throw std::runtime_error("could not restore the inherited pedigree worker affinity");
    if (!pinned) {
        std::fprintf(stderr, "TurboHap pedigree placement: fell back to inherited affinity at step %d iteration %d\n",
                     step, iteration);
        return;
    }
    std::fprintf(stderr, "TurboHap pedigree assignment=%s step=%d iteration=%d segments=%d cpus=",
                 cuts.empty() ? "fixed" : "weighted", step, iteration, segments);
    for (size_t i = 0; i < team.size(); ++i) {
        if (team[i].before != cores[i].id || team[i].after != cores[i].id)
            throw std::runtime_error("pedigree worker left its selected CPU");
        std::fprintf(stderr, "%s%d", i ? "," : "", cores[i].id);
    }
    if (!cuts.empty()) {
        std::fputs(" cuts=", stderr);
        for (size_t i = 0; i < cuts.size(); ++i) std::fprintf(stderr, "%s%d", i ? "," : "", cuts[i]);
    }
    std::fputc('\n', stderr);
}

} // namespace findhap::pedigree_placement
