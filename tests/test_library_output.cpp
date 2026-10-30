#include "../src/library_output.hpp"
#include <array>
#include <cassert>
#include <climits>
#include <cmath>
#include <limits>
#include <random>

static void check(float first, double second, const std::array<int, 6>& fields) {
    char expected[64], actual[64];
    std::memset(expected, 0x5a, sizeof(expected));
    std::memset(actual, 0x5a, sizeof(actual));
    std::snprintf(expected, 59, "%10.5f%10.5f%10d%3d%3d%7d%8d%6d ", first, second,
                  fields[0], fields[1], fields[2], fields[3], fields[4], fields[5]);
    turbohap_output::haplist_header<false>(actual, first, second, fields[0], fields[1],
                                   fields[2], fields[3], fields[4], fields[5]);
    assert(std::memcmp(expected, actual, sizeof(expected)) == 0);
    turbohap_output::haplist_header<true>(actual, first, second, fields[0], fields[1],
                                   fields[2], fields[3], fields[4], fields[5]);
    assert(std::memcmp(expected, actual, sizeof(expected)) == 0);
}

int main() {
    const std::array<int, 6> ordinary{1, 29, 3, 999, 99999, 100};
    const double special[] = {0., -0., 0.000005, 0.999995, 99.999995, 100., -100.,
        9999.99999, -9999.99999, 100000., -100000., 1e300, -1e300,
        std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity(),
        std::numeric_limits<double>::quiet_NaN()};
    const int boundaries[] = {INT_MIN, -1000000000, -100000, -1000, -100, -10, -1,
                              0, 1, 9, 10, 99, 100, 999, 1000, 100000, 1000000000, INT_MAX};
    for (double a : special) for (double b : special) check(static_cast<float>(a), b, ordinary);
    for (int field = 0; field < 6; ++field) for (int v : boundaries) {
        auto values = ordinary; values[field] = v; check(25.f, 100., values);
    }
    std::mt19937 random(291026);
    for (int i = 0; i < 100000; ++i) {
        std::array<int, 6> values;
        for (int& v : values) v = static_cast<int>(random());
        check((random() % 10000001) / 100000.f, (random() % 10000001) / 100000., values);
    }
    // Half-decimal boundaries and adjacent doubles; the float is promoted to
    // double exactly as in variadic printf. Exercise every count, not a sample.
    for (int i = -100000; i <= 100000; ++i) {
        const double tie = (i + .5) / 100000.;
        for (double v : {std::nextafter(tie, -INFINITY), tie, std::nextafter(tie, INFINITY)})
            check(static_cast<float>(v), v, ordinary);
    }
    for (int denominator : {17, 1000, 99991, 100000}) {
        double cumulative = 0;
        for (int count = 0; count <= denominator; ++count) {
            cumulative += static_cast<double>(1.f * count / denominator);
            check(100.f * count / denominator, 100. * cumulative, ordinary);
        }
    }
    for (int rounding : {FE_DOWNWARD, FE_UPWARD, FE_TOWARDZERO, FE_TONEAREST}) {
        assert(std::fesetround(rounding) == 0);
        for (double a : special) for (double b : special) check(static_cast<float>(a), b, ordinary);
    }
}
