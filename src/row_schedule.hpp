#pragma once
#include "cpu_policy.hpp"
#if defined(__APPLE__) && defined(__aarch64__)
#include "macos_policy.hpp"
#endif

namespace findhap::rows {
enum class Policy { automatic, fixed_static, dynamic_four };
inline const char* name(Policy p) {
    return p == Policy::automatic ? "auto" : p == Policy::fixed_static ? "static" : "dynamic";
}
inline Policy parse(const std::string& value) {
    if (value == "auto") return Policy::automatic;
    if (value == "static") return Policy::fixed_static;
    if (value == "dynamic") return Policy::dynamic_four;
    throw std::runtime_error("invalid row schedule '" + value + "'; use auto, static or dynamic");
}
struct Request {
    int threads;
    int thread_limit;
    bool dynamic_teams;
    bool explicit_placement;
    bool explicit_schedule;
};
struct Decision {
    Policy policy = Policy::automatic;
    bool dynamic = false;
    int effective_workers = 0;
    int permitted_logical_cpus = 0;
    int permitted_physical_cores = 0;
    int fastest_physical_cores = 0;
    std::string reason = "static scheduling";
};
inline Decision decide(Policy policy, const Request& request, const placement::Topology& topology) {
    Decision d;
    d.policy = policy;
    d.effective_workers = std::min(request.threads, request.thread_limit);
    d.permitted_logical_cpus = int(topology.allowed.size());
    if (request.threads < 1 || request.thread_limit < 1)
        throw std::runtime_error("row scheduling requires positive thread limits");
    if (policy != Policy::automatic) {
        d.dynamic = policy == Policy::dynamic_four;
        d.reason = "explicit row-schedule override";
        return d;
    }
    if (request.dynamic_teams) {
        d.reason = "dynamic OpenMP team size; retain static rows";
        return d;
    }
    if (request.explicit_placement || request.explicit_schedule) {
        d.reason = "explicit OpenMP settings; retain static rows";
        return d;
    }
    auto fallback = [&](const char* reason) {
        d.reason = reason;
        return d;
    };
    if (topology.allowed.empty() ||
        !std::is_sorted(topology.allowed.begin(), topology.allowed.end()) ||
        std::adjacent_find(topology.allowed.begin(), topology.allowed.end()) != topology.allowed.end())
        return fallback("CPU allocation is unavailable or inconsistent");
    struct Group {
        unsigned low = ~0u, high = 0;
        std::set<std::vector<int>> cores;
    };
    std::map<uint64_t, Group> groups;
    std::map<std::vector<int>, uint64_t> core_kinds;
    std::set<int> seen;
    for (const auto& cpu : topology.cpus) {
        if (!std::binary_search(topology.allowed.begin(), topology.allowed.end(), cpu.id)) continue;
        if (!cpu.kind || !cpu.capacity || cpu.siblings.empty() ||
            !std::is_sorted(cpu.siblings.begin(), cpu.siblings.end()) ||
            std::adjacent_find(cpu.siblings.begin(), cpu.siblings.end()) != cpu.siblings.end() ||
            !std::binary_search(cpu.siblings.begin(), cpu.siblings.end(), cpu.id) ||
            !seen.insert(cpu.id).second)
            return fallback("incomplete or inconsistent capacity/core-type topology");
        auto inserted = core_kinds.emplace(cpu.siblings, cpu.kind);
        if (!inserted.second && inserted.first->second != cpu.kind)
            return fallback("inconsistent physical-core types");
        auto& group = groups[cpu.kind];
        group.low = std::min(group.low, cpu.capacity);
        group.high = std::max(group.high, cpu.capacity);
        group.cores.insert(cpu.siblings);
    }
    if (seen.size() != topology.allowed.size())
        return fallback("capacity/core-type topology does not cover the CPU allocation");
    // A sibling must describe the same complete physical core from either end.
    for (const auto& cpu : topology.cpus) {
        if (!seen.count(cpu.id)) continue;
        for (int sibling : cpu.siblings) {
            if (!seen.count(sibling)) continue;
            auto other = std::find_if(topology.cpus.begin(), topology.cpus.end(),
                [&](const placement::Cpu& c) { return c.id == sibling; });
            if (other == topology.cpus.end() || other->siblings != cpu.siblings)
                return fallback("inconsistent SMT sibling topology");
        }
    }
    d.permitted_physical_cores = int(core_kinds.size());
    if (groups.size() < 2) {
        d.fastest_physical_cores = d.permitted_physical_cores;
        return fallback("one permitted core type; retain static rows");
    }
    const Group* fastest = nullptr;
    for (const auto& group : groups) {
        bool ranks_above_all = true;
        for (const auto& other : groups)
            if (group.first != other.first && group.second.low <= other.second.high)
                ranks_above_all = false;
        if (ranks_above_all) fastest = &group.second;
    }
    if (!fastest)
        return fallback("core-type capacity ranges overlap; retain static rows");
    d.fastest_physical_cores = int(fastest->cores.size());
    if (d.effective_workers <= d.fastest_physical_cores)
        return fallback("thread request fits fastest physical-core group");
    if (d.effective_workers > d.permitted_physical_cores)
        return fallback("request exceeds permitted physical cores; SMT/oversubscription is not auto-qualified");
    d.dynamic = true;
    d.reason = "thread request exceeds fastest group across ranked core types";
    return d;
}
inline uint64_t integer(const std::string& value, int base, uint64_t maximum) {
    size_t used = 0;
    if (value.empty() || value[0] == '-') throw std::runtime_error("invalid CPU topology value");
    uint64_t n = std::stoull(value, &used, base);
    if (used != value.size() || n > maximum) throw std::runtime_error("invalid CPU topology value");
    return n;
}
// No model-name allowlist or fixed core counts. The OS must expose both an
// architectural core-class identifier and capacities with non-overlapping ranks.
inline placement::Topology discover(const std::vector<int>& allowed,
                                     const std::string& root = "/sys/devices/system/cpu") {
    placement::Topology topology;
    topology.allowed = allowed;
    topology.online = allowed;
    for (int id : allowed) {
        const std::string cpu = root + "/cpu" + std::to_string(id);
        unsigned capacity = unsigned(integer(placement::read_line(cpu + "/cpu_capacity"), 10, 1048576));
        uint64_t kind = 0;
        try {
            kind = integer(placement::read_line(cpu + "/regs/identification/midr_el1"), 0, UINT64_MAX)
                   & UINT64_C(0xff00fff0);
        } catch (const std::exception&) {
            const uint64_t type = integer(placement::read_line(cpu + "/topology/core_type"), 10, 65535);
            if (type) kind = (UINT64_C(1) << 63) | type;
        }
        topology.cpus.push_back({id, capacity, kind,
            placement::parse_cpu_list(placement::read_line(cpu + "/topology/thread_siblings_list"))});
    }
    return topology;
}
inline Decision resolve(Policy policy, const Request& request, const std::vector<int>& allowed) {
    if (request.threads < 1 || request.thread_limit < 1)
        throw std::runtime_error("row scheduling requires positive thread limits");
    if (policy != Policy::automatic || request.dynamic_teams ||
        request.explicit_placement || request.explicit_schedule)
        return decide(policy, request, placement::Topology{{}, allowed, {}, false});
#if defined(__APPLE__) && defined(__aarch64__)
    if (findhap::macos::enabled && findhap::macos::topology.performance > 0) {
        // These are hardware counts, not a fabricated allowed-CPU mask.
        Decision d; d.policy = policy;
        d.effective_workers = std::min(request.threads, request.thread_limit);
        d.fastest_physical_cores = findhap::macos::topology.performance;
        const int physical = d.fastest_physical_cores + findhap::macos::topology.efficiency;
        d.dynamic = FINDHAP_MAC_DYNAMIC_ROWS && d.effective_workers > d.fastest_physical_cores
            && d.effective_workers <= physical;
        d.reason = d.dynamic ? "Mac hardware P/E counts (advisory); dynamic rows across core classes"
            : "Mac hardware P-core count (advisory); retain static rows";
        return d;
    }
#endif
    try {
        return decide(policy, request, discover(allowed));
    } catch (const std::exception&) {
        auto d = decide(Policy::fixed_static, request, placement::Topology{{}, allowed, {}, false});
        d.policy = policy;
        d.reason = "OS capacity/core-type metadata unavailable; retain static rows";
        return d;
    }
}
inline std::string json(const Decision& d) {
    std::ostringstream out;
    out << "\"row_scheduling\":{\"policy\":" << placement::quote(name(d.policy))
        << ",\"schedule\":" << placement::quote(d.dynamic ? "dynamic" : "static")
        << ",\"chunk_size\":" << (d.dynamic ? 4 : 0)
        << ",\"effective_workers\":" << d.effective_workers
        << ",\"permitted_logical_cpus\":" << d.permitted_logical_cpus
        << ",\"permitted_physical_cores\":" << d.permitted_physical_cores
        << ",\"fastest_physical_cores\":" << d.fastest_physical_cores
        << ",\"reason\":" << placement::quote(d.reason) << '}';
    return out.str();
}
inline void diagnostic(std::ostream& out, const Decision& d) {
    out << "TurboHap row schedule=" << (d.dynamic ? "dynamic,4" : "static")
        << " policy=" << name(d.policy) << " effective_workers=" << d.effective_workers
        << " fastest_physical_cores=" << d.fastest_physical_cores
        << " (" << d.reason << ")\n";
}
}
