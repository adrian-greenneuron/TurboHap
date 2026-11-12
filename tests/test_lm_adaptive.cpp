#include "../src/candidate_index.hpp"
#include <climits>
#include <numeric>
#include <random>
#include <stdexcept>

struct Entry { uint64_t zero, two; };
static void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

int main() {
    std::mt19937_64 rng(71126);
    constexpr int maximum = 131072;
    std::vector<int> order(maximum + 1), frequencies(maximum);
    std::vector<Entry> entries(maximum);
    std::iota(order.begin(), order.end(), 0);
    for (auto& f : frequencies) f = int(rng() % 100) - 50;
    for (auto& e : entries) { e.zero = rng(); e.two = rng() & ~e.zero; }
    // Gaps in referenced IDs and zero order entries must remain representable.
    for (int k = 17; k <= maximum; k += 997) order[k] = 0;
    LowMemoryCandidateLookup index(64);
    for (int count : {1500, 65534, 65535, 65536, 65537, 100000, 131072}) {
        require(index.prepare(order.data(), frequencies.data(), count, entries, false), "prepare without minima");
        require(index.prepare(order.data(), frequencies.data(), count, entries, true), "enable minima after growth");
        require(index.position_bits() == (count > 65535 ? 32 : 16), "position width selection");
        require(index.resident_bytes() <= LowMemoryCandidateLookup::storage_bound(64, count), "resident allocation bound");
        for (int rep = 0; rep < 128; ++rep) {
            int h = 1 + rng() % count;
            frequencies[h - 1] = int(rng() % 21) - 10;
            index.frequency_changed(h);
            int a, b;
            do { a = 1 + rng() % count; } while (!order[a]);
            do { b = 1 + rng() % count; } while (!order[b]);
            std::swap(order[a], order[b]);
            index.changed_positions(a, b, entries);
            if (rep % 3 == 0) {
                auto old = entries[h - 1];
                entries[h - 1].zero = rng();
                entries[h - 1].two = rng() & ~entries[h - 1].zero;
                index.refresh(h, old.zero, old.two);
            }
            int first = 1 + rng() % count, last = 1 + rng() % count;
            if (first > last) std::swap(first, last);
            int frequency = INT_MAX, position = 0;
            int want_frequency = INT_MAX, want_position = 0;
            for (int k = first; k <= last; ++k)
                if (order[k] && frequencies[order[k] - 1] < want_frequency) {
                    want_frequency = frequencies[order[k] - 1]; want_position = k;
                }
            index.minimum(first, last, frequency, position);
            require(frequency == want_frequency && position == want_position, "minimum order/tie regression");
            uint64_t zero = rng() & 0xffff, two = rng() & ~zero & 0xffff;
            int want = first;
            while (want <= last && (!order[want] ||
                ((zero & entries[order[want] - 1].two) | (two & entries[order[want] - 1].zero)))) ++want;
            require(index.next(first, last, zero, two) == want, "next candidate regression");
        }
    }
    // The fixed four-MiB LM budget remains authoritative after widening.
    require(LowMemoryCandidateLookup::storage_bound(64, 131072) <= FINDHAP_LM_ORDERED_BYTES, "wide index fits unchanged budget");
    require(LowMemoryCandidateLookup::storage_bound(64, 131073) > FINDHAP_LM_ORDERED_BYTES, "wide budget boundary");
    require(LowMemoryCandidateLookup::storage_bound(64, -1) == SIZE_MAX, "negative count");
    require(LowMemoryCandidateLookup::storage_bound(64, INT_MAX) == SIZE_MAX, "tree arithmetic bound");
    std::vector<int8_t> library(size_t(131073) * 64, 1);
    std::vector<int> full_order(131074), full_frequencies(131073, 1);
    std::iota(full_order.begin(), full_order.end(), 0);
    CandidateIndex filter(64, library.data(), 64, 131073);
    require(filter.ordered(full_order.data(), full_frequencies.data(), 65535) != nullptr, "compact caller index");
    auto* wide = filter.ordered(full_order.data(), full_frequencies.data(), 65536);
    require(wide && wide->position_bits() == 32, "caller promotion");
    require(filter.ordered(full_order.data(), full_frequencies.data(), 131073) == nullptr, "caller budget fallback");
    require(filter.ordered(full_order.data(), full_frequencies.data(), 131073) == nullptr, "fallback remains disabled");
}
