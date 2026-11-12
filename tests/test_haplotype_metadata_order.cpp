// Check sparse metadata against the original eager order at library output.
#include "../src/haplotype_output_order.hpp"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

static void require(bool ok) {
    if (!ok) {
        std::fputs("haplotype metadata output-order check failed\n", stderr);
        std::exit(1);
    }
}

struct Result {
    int emitted = 0;
    double cumulative = 0.0;
    std::string bytes;
};

// Preserve the actual frequency denominator, ordered traversal and REAL(4)
// rounding. Bounds checks detect both first-segment underflow and accidental
// access to an adjacent segment, even when such an access stays in the array.
static Result output(const std::vector<int>& frequencies, const std::vector<int>& order,
                     int segment, int maxhap, int published, bool active_bound,
                     bool demand_zero) {
    const size_t freq_base = size_t(segment) * maxhap;
    const size_t order_base = size_t(segment) * (size_t(maxhap) + 1);
    const int limit = active_bound && published > 0 ? published : maxhap;
    int total = 0;
    for (int i = 1; i <= limit; ++i)
        total += std::max(frequencies[freq_base + i - 1], 0);
    Result result;
    for (int i = 1; i <= limit; ++i) {
        const int stored = order[order_base + i];
        const int id = demand_zero
            ? findhap_haplotype_metadata::output_order_id(stored, i, published) : stored;
        require(id >= 1 && id <= maxhap);
        const size_t at = freq_base + size_t(id) - 1;
        require(at >= freq_base && at < freq_base + maxhap);
        const int frequency = frequencies[at];
        result.cumulative += static_cast<double>(1.0f * frequency / total);
        if (frequency > 0) {
            ++result.emitted;
            char record[128];
            const int count = std::snprintf(record, sizeof(record), "%10.5f%10.5f%10d\n",
                100.0f * frequency / total, 100.0 * result.cumulative, id);
            require(count > 0 && size_t(count) < sizeof(record));
            result.bytes.append(record, size_t(count));
        }
    }
    return result;
}

static void check_capacity(int maxhap) {
    constexpr int segments = 3;
    const size_t stride = size_t(maxhap) + 1;
    std::vector<int> frequencies(size_t(maxhap) * segments, 0);
    std::vector<int> dense(stride * segments, 0), sparse(stride * segments, 0);
    const int partial = std::min(maxhap, 3);
    const int published[segments] = {0, partial, maxhap};
    for (int segment = 0; segment < segments; ++segment) {
        for (int i = 1; i <= maxhap; ++i) {
            dense[size_t(segment) * stride + i] = i;
            if (i <= published[segment]) sparse[size_t(segment) * stride + i] = i;
        }
    }
    // The empty first segment exercises the original underflow. The partial
    // second segment has reordered live entries, a zero-frequency published
    // entry, and zero unpublished tails. A fully occupied final segment with
    // a nonzero last slot also checks the last valid output address.
    frequencies[size_t(maxhap)] = 2;
    if (partial >= 3) {
        frequencies[size_t(maxhap) + 1] = 5;
        const int permutation[] = {2, 3, 1};
        for (int i = 1; i <= partial; ++i)
            dense[stride + i] = sparse[stride + i] = permutation[i - 1];
    }
    frequencies[size_t(maxhap) * 3 - 1] = 17;
    for (bool stale_prior_tail : {false, true}) {
        if (stale_prior_tail && maxhap > partial) {
            // Prior loading can leave original-ID -> loaded-slot mappings
            // beyond the published prefix. Such nonzero tails must also have
            // logical identity during output, without rewriting the remap.
            sparse[stride + partial + 1] = 1;
            sparse[stride + maxhap] = partial;
        }
        const auto before = sparse;
        for (bool active_bound : {false, true}) {
            for (int segment = 0; segment < segments; ++segment) {
                const auto expected = output(frequencies, dense, segment, maxhap,
                                             published[segment], active_bound, false);
                const auto actual = output(frequencies, sparse, segment, maxhap,
                                           published[segment], active_bound, true);
                require(actual.emitted == expected.emitted && actual.bytes == expected.bytes);
                require(actual.cumulative == expected.cumulative ||
                        (std::isnan(actual.cumulative) && std::isnan(expected.cumulative)));
            }
        }
        // Rendering must not publish tails or rewrite prior-ID zero sentinels
        // or nonzero remapping entries left outside the current live prefix.
        require(sparse == before);
        for (int segment = 0; segment < segments; ++segment) {
            require(sparse[size_t(segment) * stride] == 0);
            if (!stale_prior_tail)
                for (int i = published[segment] + 1; i <= maxhap; ++i)
                    require(sparse[size_t(segment) * stride + i] == 0);
        }
    }
}

int main() {
    // Cover a one-slot library, mixed small segments, the uint16 boundary and
    // wide positions without large memory allocations or inference work.
    for (int capacity : {1, 7, 65535, 65536}) check_capacity(capacity);
    std::puts("PASS sparse metadata: empty/live/permuted/zero-tail/prior-tail/stride/bounds; both output limits");
}
