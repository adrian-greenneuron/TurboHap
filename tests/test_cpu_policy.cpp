#include "../src/cpu_policy.hpp"
#include <cassert>
#include <functional>
#include <iostream>

using namespace findhap::placement;
static void fails(const std::function<void()>& f) {
    bool caught = false;
    try { f(); } catch (const std::exception&) { caught = true; }
    assert(caught);
}
int main() {
    // Sparse IDs and unequal capacities within a core type must still select
    // the complete faster type. CPU numbers are deliberately unlike the Spark.
    Topology t{{1, 4, 80, 81, 8193}, {1, 4, 80, 81, 8193}, {
        {1, 997, 100, {1}}, {4, 1017, 100, {4}}, {8193, 1024, 100, {8193}},
        {80, 718, 200, {80}}, {81, 731, 200, {81}}}, true};
    for (int n : {1, 2, 3}) {
        auto d = decide(Policy::automatic, n, false, t);
        assert(d.changed && d.selected == std::vector<int>({1, 4, 8193}));
    }
    assert(decide(Policy::automatic, 4, false, t).selected == t.allowed);
    fails([&] { decide(Policy::performance, 4, false, t); });
    fails([&] { decide(Policy::automatic, 0, false, t); });
    assert(decide(Policy::inherit, 2, false, t).selected == t.allowed);
    assert(decide(Policy::automatic, 2, true, t).selected == t.allowed);
    fails([&] { decide(Policy::performance, 2, true, t); });
    t.allowed = {1, 4, 80};
    assert(decide(Policy::automatic, 2, false, t).selected == t.allowed);
    assert(decide(Policy::performance, 2, false, t).selected == std::vector<int>({1, 4}));
    t.allowed = t.online;
    t.qualified = false;
    assert(decide(Policy::automatic, 2, false, t).selected == t.allowed);
    fails([&] { decide(Policy::performance, 2, false, t); });
    t.qualified = true;
    t.cpus[0].capacity = 0;
    assert(!decide(Policy::automatic, 2, false, t).changed);
    fails([&] { decide(Policy::performance, 2, false, t); });
    t.cpus[0].capacity = 700; // Overlapping core-class ranges: do not guess.
    assert(!decide(Policy::automatic, 2, false, t).changed);
    t.cpus[0].capacity = 997;
    auto missing = t; missing.cpus.pop_back();
    assert(!decide(Policy::automatic, 2, false, missing).changed);
    auto malformed = t; malformed.cpus[0].siblings = {500};
    assert(!decide(Policy::automatic, 2, false, malformed).changed);
    // Two SMT siblings are one physical core, not capacity for two workers.
    Topology smt{{0, 1, 2}, {0, 1, 2}, {{0, 1024, 10, {0, 1}},
        {1, 1024, 10, {0, 1}}, {2, 700, 20, {2}}}, true};
    assert(decide(Policy::automatic, 1, false, smt).selected == std::vector<int>({0, 1}));
    assert(!decide(Policy::automatic, 2, false, smt).changed);
    Topology homogeneous{{7, 8}, {7, 8}, {{7, 1000, 10, {7}}, {8, 1024, 10, {8}}}, true};
    assert(!decide(Policy::automatic, 1, false, homogeneous).changed);
    assert(decide(Policy::performance, 2, false, homogeneous).selected == homogeneous.allowed);
    assert(cpu_list(parse_cpu_list("8193,4,2-3,4")) == "2-4,8193");
    for (const auto& bad : {"", "1,", "1,,2", "4-2", "-1", "1048576", "a"})
        fails([&] { parse_cpu_list(bad); });
    assert(parse("auto") == Policy::automatic && parse("inherit") == Policy::inherit);
    fails([] { parse("fast"); });
    const Decision target{{0, 1}, {1}, "performance", true};
    for (int failure_at : {0, 1, 2, 3, 4}) {
        // 0: success, 1: pre-apply read, 2: write, 3: verification read,
        // 4: all syscalls denied. These failures must not abort auto mode.
        int calls = 0;
        std::vector<int> mask = target.inherited;
        auto fault = [&] {
            if (++calls == failure_at || failure_at == 4) throw std::runtime_error("injected affinity failure");
        };
        auto read = [&] { fault(); return mask; };
        auto write = [&](const std::vector<int>& value) { fault(); mask = value; };
        const auto d = apply_decision(Policy::automatic, target, read, write);
        assert(d.changed == (failure_at == 0));
        assert(mask == (failure_at == 0 ? target.selected : target.inherited));
        if (failure_at == 4) assert(d.selected.empty());
    }
    fails([&] {
        apply_decision(Policy::performance, target,
            []() -> std::vector<int> { throw std::runtime_error("denied"); },
            [](const std::vector<int>&) {});
    });
    {
        // Allocation changes before applying: preserve the new restriction.
        bool wrote = false;
        auto d = apply_decision(Policy::automatic, target,
            [] { return std::vector<int>{0}; }, [&](const auto&) { wrote = true; });
        assert(!d.changed && d.selected == std::vector<int>{0} && !wrote);
    }
    {
        // A successful set may be intersected by a concurrent cpuset change.
        int reads = 0;
        std::vector<std::vector<int>> writes;
        auto d = apply_decision(Policy::automatic, target,
            [&] { return ++reads == 2 ? std::vector<int>{} : target.inherited; },
            [&](const auto& value) { writes.push_back(value); });
        assert(!d.changed && d.selected == target.inherited);
        assert(writes == std::vector<std::vector<int>>({target.selected, target.inherited}));
    }
    std::cout << "PASS: topology/policy precedence and affinity failure/rollback fixtures\n";
}
