#include "../src/integer_output.hpp"
#include <array>
#include <cassert>
#include <cstdio>
#include <limits>
#include <random>
#include <string>

static void check(int value) {
    for (int width : {0, 1, 2, 4, 10, 12}) {
        std::array<char, 64> actual{}, expected{};
        actual.fill('!');
        char* end = turbohap_output::append_integer(actual.data() + 1, value, width);
        const int length = std::snprintf(expected.data(), expected.size(), "%*d", width, value);
        assert(end - (actual.data() + 1) == length);
        assert(std::memcmp(actual.data() + 1, expected.data(), length) == 0);
        assert(actual.front() == '!' && *end == '!');
    }
}

int main() {
    for (int value : {std::numeric_limits<int>::min(), -1000000000, -101, -100,
                      -99, -10, -1, 0, 1, 9, 10, 99, 100, 101, 999, 1000,
                      65535, 500000, 999999999, std::numeric_limits<int>::max()})
        check(value);
    std::mt19937 random(291026);
    std::uniform_int_distribution<int> values(std::numeric_limits<int>::min(),
                                             std::numeric_limits<int>::max());
    for (int i = 0; i < 20000; ++i) check(values(random));
    std::puts("Integer formatting matches libc at boundaries and 20000 random values");
}
