#include "../src/pedigree_conflict.hpp"
#include <algorithm>
#include <cassert>
#include <random>
#include <vector>

static void check(const std::vector<int8_t>& a, const std::vector<int8_t>& b) {
    int full_count = 0;
    for (size_t i=0; i<a.size(); ++i)
        if (std::abs(int(a[i])-int(b[i])) > 1) ++full_count;
    assert(pedigree_single_conflict(a.data(), b.data(), int(a.size())) == (full_count == 1));
}
int main() {
    // Includes signed-byte extremes as well as all valid missing/allele codes.
    for (int a=-128; a<=127; ++a)
        for (int b=-128; b<=127; ++b) check({int8_t(a)}, {int8_t(b)});
    std::mt19937 rng(271026);
    for (int n=0; n<=256; ++n) {
        if (n>65 && n!=127 && n!=128 && n!=129 && n!=255 && n!=256) continue;
        std::vector<int8_t> a(n,0), b(n,1); check(a,b);
        for (int i=0; i<n; ++i) {
            b[i]=2; check(a,b); // sole conflict at each possible block/tail position
            for (int j=0; j<n; ++j) if(j!=i) {
                b[j]=2; check(a,b); b[j]=1;
            }
            b[i]=1;
        }
        for (int rep=0; rep<50; ++rep) {
            for (int i=0; i<n; ++i) { a[i]=int(rng()%3); b[i]=int(rng()%3); }
            check(a,b);
        }
    }
}
