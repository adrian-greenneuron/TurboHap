#include "../src/pedigree_frequency.hpp"
#include "../src/population_segment.hpp"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <vector>
#if defined(__linux__)
#include <sys/mman.h>
#include <unistd.h>
#endif

static void require(bool value) {
    if (!value) { std::fputs("workload-vector oracle mismatch\n", stderr); std::exit(1); }
}

static void check(const int8_t* raw, const int8_t* hap, int8_t* result,
                  int* sums, int* counts, int length) {
    std::vector<int8_t> expected(length);
    std::vector<int> expected_sums(sums, sums + length), expected_counts(counts, counts + length);
    int known = 0;
    for (int i = 0; i < length; ++i) {
        expected[i] = raw[i] == 5 ? 1 : raw[i];
        known += expected[i] != 1;
        if (hap[i] != 1) { expected_sums[i] += hap[i]; ++expected_counts[i]; }
    }
    findhap_pedigree::add_base_frequency(hap, sums, counts, length);
    require(std::equal(expected_sums.begin(), expected_sums.end(), sums));
    require(std::equal(expected_counts.begin(), expected_counts.end(), counts));
    require(findhap_population::normalise(raw, result, length) == known);
    require(std::equal(expected.begin(), expected.end(), result));
    known = 0;
    for (int i = 0; i < length; ++i) {
        expected[i] = raw[i] == 5 ? 1 : int8_t(2 * int(expected[i]) - int(hap[i]));
        known += expected[i] != 1;
    }
    require(findhap_population::backsolve(result, hap, raw, length) == known);
    require(std::equal(expected.begin(), expected.end(), result));
}

int main() {
    std::mt19937 random(81126);
    for (int n = 0; n <= 257; ++n) for (int repeat = 0; repeat < 16; ++repeat) {
        std::vector<int8_t> raw(n + 2), hap(n + 2), result(n + 2, -97);
        std::vector<int> sums(n + 2, 17), counts(n + 2, 7);
        for (int i = 1; i <= n; ++i) {
            raw[i] = repeat < 8 ? int8_t(random() % 6) : int8_t(random() % 256);
            hap[i] = int8_t(random() % 256);
        }
        check(raw.data() + 1, hap.data() + 1, result.data() + 1,
              sums.data() + 1, counts.data() + 1, n);
        require(result.front() == -97 && result.back() == -97);
        require(sums.front() == 17 && sums.back() == 17);
        require(counts.front() == 7 && counts.back() == 7);
    }
    // Exercise every signed-byte arithmetic pair in actual vector lanes.
    for (int width : {16,32,64}) for (int a = -128; a <= 127; ++a) for (int b = -128; b <= 127; ++b) {
        int8_t raw[64], hap[64], result[64];
        int sums[64] = {}, counts[64] = {};
        std::fill_n(raw, width, int8_t(a)); std::fill_n(hap, width, int8_t(b));
        check(raw, hap, result, sums, counts, width);
    }
#if defined(__linux__)
    // Guard pages catch vector reads/writes past a partial final block.
    const size_t page = size_t(sysconf(_SC_PAGESIZE));
    for (int n = 0; n <= 129; ++n) {
        void* mappings[5];
        for (void*& address : mappings) {
            address = mmap(nullptr, page * 2, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
            require(address != MAP_FAILED);
            require(mprotect(static_cast<char*>(address) + page, page, PROT_NONE) == 0);
        }
        auto bytes = [&](int k) { return reinterpret_cast<int8_t*>(mappings[k]) + page - n; };
        auto integers = [&](int k) { return reinterpret_cast<int*>(static_cast<char*>(mappings[k]) + page) - n; };
        for (int i = 0; i < n; ++i) {
            bytes(0)[i] = int8_t(random() % 256); bytes(1)[i] = int8_t(random() % 256);
            integers(3)[i] = 17; integers(4)[i] = 7;
        }
        check(bytes(0), bytes(1), bytes(2), integers(3), integers(4), n);
        for (void* address : mappings) require(munmap(address, page * 2) == 0);
    }
#endif
}
