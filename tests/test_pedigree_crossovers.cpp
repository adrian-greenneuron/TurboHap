#include <algorithm>
#include "../src/pedigree_crossovers.hpp"
#include <cassert>
#include <random>
#include <vector>

static findhap_pedigree::CrossoverScan oracle(const std::vector<int8_t>& child,
                                             const std::vector<int8_t>& a,
                                             const std::vector<int8_t>& b) {
    findhap_pedigree::CrossoverScan out;
    const std::vector<int8_t>* parents[] = {&a, &b};
    const int n = child.size();
    for (int start=0; start<2; ++start) {
        int k=start; out.last[start]=n+1;
        for (int l=0; l<n; ++l) {
            if (std::abs(int(child[l])-int((*parents[k])[l]))>1) {
                ++out.count[start]; out.last[start]=l+1; k=1-k;
                if (std::abs(int(child[l])-int((*parents[k])[l]))>1) ++out.count[start];
            } else if (std::abs(int(child[l])-int((*parents[1-k])[l]))>1 && !out.count[start]) {
                out.first[start]=l+1;
            }
        }
    }
    return out;
}
template<int Backend> static void check_backend(const std::vector<int8_t>& c,
        const std::vector<int8_t>& a, const std::vector<int8_t>& b,
        const findhap_pedigree::CrossoverScan& expected) {
    auto got=findhap_pedigree::scan_crossovers<Backend>(c.data(),a.data(),b.data(),c.size());
    for (int i=0;i<2;++i) {
        assert(got.count[i]==expected.count[i]);
        assert(got.first[i]==expected.first[i]);
        assert(got.last[i]==expected.last[i]);
    }
}
static void check(const std::vector<int8_t>& c,const std::vector<int8_t>& a,
                  const std::vector<int8_t>& b) {
    auto e=oracle(c,a,b);
    check_backend<0>(c,a,b,e);
#if defined(__x86_64__) || defined(__i386__)
    if (findhap_simd::supported(2)) check_backend<2>(c,a,b,e);
    if (findhap_simd::supported(3)) check_backend<3>(c,a,b,e);
#endif
}
int main() {
    // Exhaust every short sequence of the four parental-conflict states.
    for (int n=0;n<=8;++n) {
        std::vector<int8_t> c(n,0),a(n),b(n);
        for (unsigned pattern=0;pattern<(1u<<(2*n));++pattern) {
            for (int j=0;j<n;++j) {a[j]=2*((pattern>>(2*j))&1);b[j]=2*((pattern>>(2*j+1))&1);}
            check(c,a,b);
        }
    }
    std::mt19937 rng(271026);
    for (int n : {15,16,17,31,32,33,63,64,65,127,128,129,255,256,257}) {
        std::vector<int8_t> c(n,0),a(n,0),b(n,0);
        check(c,a,b);
        for (int j=0;j<n;++j) {a[j]=2;check(c,a,b);b[j]=2;check(c,a,b);a[j]=b[j]=0;}
        for (int rep=0;rep<500;++rep) {
            for (int j=0;j<n;++j) {
                c[j]=rep<250 ? int(rng()%3) : int(rng()%256)-128;
                a[j]=rep<250 ? int(rng()%3) : int(rng()%256)-128;
                b[j]=rep<250 ? int(rng()%3) : int(rng()%256)-128;
            }
            check(c,a,b);
        }
    }
}
