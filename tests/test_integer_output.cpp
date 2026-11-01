#include "../src/integer_output.hpp"
#include "../src/crossover_output.hpp"
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
    {
        const int values[]={0,1,-1,9999999,10000000,999999999,1000000000,INT32_MAX,INT32_MIN};
        for(int offset=0;offset<2000;++offset) {
            int fields[8];for(int j=0;j<8;++j)fields[j]=values[(offset+j)%9];
            char expected[80],actual[82];std::memset(actual,'!',sizeof(actual));
            std::snprintf(expected,80,"%9d%9d%7d%7d%7d%7d%7d%7d",fields[0],fields[1],fields[2],fields[3],fields[4],fields[5],fields[6],fields[7]);
            turbohap_output::crossover_line(actual+1,fields[0],fields[1],fields[2],fields[3],fields[4],fields[5],fields[6],fields[7]);
            assert(std::strcmp(actual+1,expected)==0 && actual[0]=='!' && actual[81]=='!');
        }
    }
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
