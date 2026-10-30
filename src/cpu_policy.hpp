#pragma once
#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <new>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
#if defined(__linux__)
#include <sched.h>
#endif

namespace findhap::placement {
enum class Policy { automatic, inherit, performance };
inline const char* name(Policy p) {
    return p == Policy::automatic ? "auto" : p == Policy::inherit ? "inherit" : "performance";
}
inline Policy parse(const std::string& value) {
    if (value == "auto") return Policy::automatic;
    if (value == "inherit") return Policy::inherit;
    if (value == "performance") return Policy::performance;
    throw std::runtime_error("invalid CPU policy '" + value + "'; use auto, inherit or performance");
}
inline bool external_binding() {
    return std::getenv("OMP_PLACES") || std::getenv("OMP_PROC_BIND") || std::getenv("GOMP_CPU_AFFINITY");
}
inline std::string cpu_list(const std::vector<int>& cpus) {
    std::string result;
    for (size_t i = 0; i < cpus.size(); ++i) {
        int start = cpus[i], end = start;
        while (i + 1 < cpus.size() && cpus[i + 1] == end + 1) end = cpus[++i];
        if (!result.empty()) result += ',';
        result += std::to_string(start);
        if (end != start) result += '-' + std::to_string(end);
    }
    return result;
}
inline std::vector<int> parse_cpu_list(const std::string& text) {
    if (text.empty() || text.back() == ',') throw std::runtime_error("invalid CPU list");
    std::vector<int> result;
    std::istringstream stream(text);
    std::string field;
    auto number = [](const std::string& value) {
        if (value.empty() || value.find_first_not_of("0123456789") != std::string::npos)
            throw std::runtime_error("invalid CPU list");
        const unsigned long n = std::stoul(value);
        if (n >= (1u << 20)) throw std::runtime_error("CPU ID exceeds supported mask bound");
        return int(n);
    };
    while (std::getline(stream, field, ',')) {
        const size_t dash = field.find('-');
        int first = number(field.substr(0, dash));
        int last = dash == std::string::npos ? first : number(field.substr(dash + 1));
        if (last < first) throw std::runtime_error("reversed CPU range");
        for (int n = first; n <= last; ++n) result.push_back(n);
    }
    if (result.empty()) throw std::runtime_error("empty CPU list");
    std::sort(result.begin(), result.end());
    result.erase(std::unique(result.begin(), result.end()), result.end());
    return result;
}
struct Cpu {
    int id;
    unsigned capacity;
    uint64_t kind; // MIDR implementor + part, excluding revision/variant.
    std::vector<int> siblings;
};
struct Topology {
    std::vector<int> online, allowed;
    std::vector<Cpu> cpus;
    bool qualified = false;
};
struct Decision {
    std::vector<int> inherited, selected;
    std::string reason;
    bool changed = false;
};
inline Decision decide(Policy policy, int threads, bool external, const Topology& t) {
    if (threads < 1) throw std::runtime_error("thread count must be positive");
    Decision d{t.allowed, t.allowed, "inherited placement", false};
    auto fallback = [&](const char* reason) {
        if (policy == Policy::performance) throw std::runtime_error(reason);
        d.reason = reason;
        return d;
    };
    if (policy == Policy::inherit) return d;
    if (external) return fallback("explicit OpenMP affinity settings take precedence");
    if (!t.qualified) return fallback("automatic core selection is not qualified on this platform");
    if (t.allowed.empty()) return fallback("CPU allocation is unavailable");
    if (policy == Policy::automatic && t.allowed != t.online)
        return fallback("inherited CPU restriction retained");
    struct Group { std::vector<int> ids; std::set<std::vector<int>> cores; unsigned low = ~0u, high = 0; };
    std::map<uint64_t, Group> groups;
    std::set<int> seen;
    for (const auto& c : t.cpus) {
        if (!std::binary_search(t.allowed.begin(), t.allowed.end(), c.id)) continue;
        if (!c.kind || !c.capacity || c.siblings.empty() ||
            !std::binary_search(c.siblings.begin(), c.siblings.end(), c.id) || !seen.insert(c.id).second)
            return fallback("incomplete or inconsistent CPU topology");
        auto& g = groups[c.kind];
        g.ids.push_back(c.id); g.cores.insert(c.siblings);
        g.low = std::min(g.low, c.capacity); g.high = std::max(g.high, c.capacity);
    }
    if (seen.size() != t.allowed.size()) return fallback("CPU capacity or core-type information is unavailable");
    if (groups.size() == 1 && policy == Policy::automatic) return fallback("one permitted core type; inherited placement retained");
    Group* best = nullptr;
    for (auto& item : groups) {
        bool greater = true;
        for (const auto& other : groups)
            if (item.first != other.first && item.second.low <= other.second.high) greater = false;
        if (greater) best = &item.second;
    }
    if (!best) return fallback("core types do not have a reliable capacity ranking");
    if (threads > int(best->cores.size())) return fallback("thread request exceeds fastest permitted physical-core group; use auto or inherit");
    d.selected = best->ids;
    std::sort(d.selected.begin(), d.selected.end());
    d.changed = d.selected != d.inherited;
    d.reason = "fastest permitted core type; no added thread binding";
    return d;
}

inline constexpr bool qualified_platform() {
#if defined(__linux__) && defined(FINDHAP_NATIVE_GB10)
    return true;
#else
    return false;
#endif
}

#if defined(__linux__)
struct Mask {
    cpu_set_t* data;
    size_t bytes;
    explicit Mask(int bits) : data(CPU_ALLOC(bits)), bytes(CPU_ALLOC_SIZE(bits)) {
        if (!data) throw std::bad_alloc();
        CPU_ZERO_S(bytes, data);
    }
    ~Mask() { CPU_FREE(data); }
    Mask(const Mask&) = delete;
};
inline std::vector<int> affinity() {
    for (int bits = 128; bits <= (1 << 20); bits *= 2) {
        Mask mask(bits);
        if (!sched_getaffinity(0, mask.bytes, mask.data)) {
            std::vector<int> cpus;
            for (int i = 0; i < bits; ++i) if (CPU_ISSET_S(i, mask.bytes, mask.data)) cpus.push_back(i);
            return cpus;
        }
        if (errno != EINVAL) throw std::runtime_error(std::string("cannot read CPU affinity: ") + std::strerror(errno));
    }
    throw std::runtime_error("CPU affinity exceeds supported mask bound");
}
inline void set_affinity(const std::vector<int>& cpus) {
    if (cpus.empty()) throw std::runtime_error("refusing an empty CPU allocation");
    Mask mask(cpus.back() + 1);
    for (int id : cpus) CPU_SET_S(id, mask.bytes, mask.data);
    if (sched_setaffinity(0, mask.bytes, mask.data))
        throw std::runtime_error(std::string("cannot set CPU affinity: ") + std::strerror(errno));
}
inline std::string read_line(const std::string& path) {
    std::ifstream input(path);
    std::string value;
    if (!std::getline(input, value) || value.size() > 65536) throw std::runtime_error("CPU topology information is unavailable");
    return value;
}
inline Topology discover(bool details) {
    Topology t;
    t.qualified = qualified_platform(); t.allowed = affinity();
    if (!details) return t;
    t.online = parse_cpu_list(read_line("/sys/devices/system/cpu/online"));
    for (int id : t.allowed) {
        std::string root = "/sys/devices/system/cpu/cpu" + std::to_string(id);
        unsigned long capacity = std::stoul(read_line(root + "/cpu_capacity"));
        if (!capacity || capacity > 1048576) throw std::runtime_error("invalid CPU capacity");
        uint64_t midr = std::stoull(read_line(root + "/regs/identification/midr_el1"), nullptr, 0);
        t.cpus.push_back({id, unsigned(capacity), midr & UINT64_C(0xff00fff0),
                         parse_cpu_list(read_line(root + "/topology/thread_siblings_list"))});
    }
    return t;
}
#else
inline Topology discover(bool) { return {}; }
#endif

// Keep the application/rollback path testable without changing real affinity.
// Automatic mode must also survive a transient syscall failure after discovery.
template<class Read, class Write>
inline Decision apply_decision(Policy policy, const Decision& d, Read read, Write write) {
    bool attempted = false;
    try {
        auto current = read();
        if (current != d.inherited) {
            if (policy == Policy::performance) throw std::runtime_error("CPU allocation changed during discovery");
            return {current, current, "CPU allocation changed; inherited placement retained", false};
        }
        attempted = true;
        write(d.selected);
        if (read() != d.selected) {
            write(d.inherited);
            attempted = false;
            if (policy == Policy::performance) throw std::runtime_error("CPU allocation changed while applying placement");
            current = read();
            return {current, current, "allocation changed; original restriction restored", false};
        }
        return d;
    } catch (const std::exception& error) {
        if (policy == Policy::performance) throw;
        std::string reason = error.what();
        if (attempted) {
            // Restore only the inherited allocation. The kernel continues to
            // enforce administrative restrictions, including concurrent changes.
            try { write(d.inherited); }
            catch (const std::exception&) { reason += "; could not restore inherited mask"; }
        }
        std::vector<int> current;
        try { current = read(); }
        catch (const std::exception&) { reason += "; effective mask unavailable"; }
        return {current, current, reason, false};
    }
}

inline Decision start(Policy policy, int threads, bool dry_run, Topology& topology) {
    const bool external = external_binding();
    try {
        topology = discover(qualified_platform() && policy != Policy::inherit && !external);
    } catch (const std::exception& error) {
        if (policy == Policy::performance) throw;
#if defined(__linux__)
        // Discovery failure must not turn automatic mode into a hard failure.
        // Leave scheduling untouched even if the affinity query itself is denied.
        try { topology.allowed = affinity(); } catch (const std::exception&) {}
#endif
        return {topology.allowed, topology.allowed, error.what(), false};
    }
    Decision d = decide(policy, threads, external, topology);
#if defined(__linux__)
    if (d.changed && !dry_run) return apply_decision(policy, d, affinity, set_affinity);
#endif
    return d;
}
inline void diagnostic(std::ostream& out, Policy policy, int threads, const Decision& d) {
    out << "TurboHap CPU policy=" << name(policy) << " effective_threads=" << threads
        << " calling_thread_cpus=" << cpu_list(d.selected) << " (" << d.reason << ")\n";
}
inline std::string quote(const std::string& text) {
    std::string result = "\"";
    for (char c : text) {
        if (c == '\\' || c == '"') result += '\\';
        if (c == '\n') result += "\\n"; else result += c;
    }
    return result + '"';
}
inline void information(std::ostream& out, Policy policy, int threads, int limit, const Topology& t, const Decision& d) {
    out << "{\"policy\":" << quote(name(policy)) << ",\"effective_threads\":" << threads
        << ",\"openmp_limit\":" << limit << ",\"dry_run\":true,\"qualified\":" << (t.qualified ? "true" : "false")
        << ",\"backend\":" << quote(t.qualified ? "Linux capacity/MIDR" : "inherited scheduling")
        << ",\"calling_thread_cpus\":" << quote(cpu_list(d.inherited))
        << ",\"proposed_cpus\":" << quote(cpu_list(d.selected)) << ",\"reason\":" << quote(d.reason) << ",\"cpus\":[";
    for (size_t i = 0; i < t.cpus.size(); ++i) {
        const auto& c = t.cpus[i];
        if (i) out << ',';
        out << "{\"id\":" << c.id << ",\"capacity\":" << c.capacity << ",\"kind\":" << c.kind
            << ",\"siblings\":" << quote(cpu_list(c.siblings)) << '}';
    }
    out << "]}\n";
}
}
