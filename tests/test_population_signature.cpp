#include "../src/candidate_index.hpp"
#include <cassert>
#include <cmath>
#include <random>

int main() {
    std::mt19937 rng(271026);
    for (int length : {1,15,16,17,31,32,33,47,48,49,63,64,65,100,127,128,129,257}) {
        // Exact-sized allocations expose any over-read to ASan.
        std::vector<int8_t> query(length), library(length * 3);
        for (int trial = 0; trial < 1024; ++trial) {
            for (auto& v : query) v = trial < 512 ? int(rng()%3) : int(rng()%256)-128;
            for (auto& v : library) v = trial < 512 ? int(rng()%3) : int(rng()%256)-128;
            CandidateIndex index(length, library.data(), length, 3);
            const auto q = index.encode(query.data());
            auto expected = [&](const int8_t* a) {
                CandidateIndex::Signature s;
                const int width = std::min(length,64);
                for (int k=0;k<width;++k) {
                    const int pos = width>1 ? int(int64_t(k)*(length-1)/(width-1)) : 0;
                    s.zero |= uint64_t(a[pos]==0)<<k;
                    s.two |= uint64_t(a[pos]==2)<<k;
#if FINDHAP_POP_COMPLETE
                    s.invalid |= uint64_t(a[pos]<0 || a[pos]>2)<<k;
#endif
                }
                return s;
            };
            auto eq = [&](auto a, auto b) {
                assert(a.zero==b.zero && a.two==b.two);
#if FINDHAP_POP_COMPLETE
                assert(a.invalid==b.invalid);
#endif
            };
            eq(q, expected(query.data()));
            for (int h=1;h<=3;++h) {
                const auto v=expected(library.data()+(h-1)*length);
                eq(index.entry(h),v);
                assert(index.conflicts(q,h)==((q.zero&v.two)|(q.two&v.zero)));
#if FINDHAP_POP_COMPLETE
                const bool complete=length<=64 && !(q.invalid|v.invalid);
                assert(index.complete(q,h)==complete);
                if (complete) {
                    int count=0;
                    for (int k=0;k<length;++k) count+=std::abs(int(query[k])-int(library[(h-1)*length+k]))>1;
                    assert(count==__builtin_popcountll(index.conflicts(q,h)));
                }
#endif
            }
#if FINDHAP_POP_FULL
            for (int h=1;h<=3;++h) {
                bool valid=length<=128;
                int count=0;std::vector<int> fill;
                for(int k=0;k<length;++k) {
                    const int a=query[k],b=library[(h-1)*length+k];
                    valid=valid && a>=0 && a<=2 && b>=0 && b<=2;
                    count+=std::abs(a-b)>1;
                    if(b==1 && a!=1) fill.push_back(k);
                }
                int got=-1;
                assert(index.full_count(q,h,got)==valid);
                if(valid) {
                    assert(got==count);std::vector<int> positions(length);
                    int n=index.full_fill(q,h,positions.data());positions.resize(n);assert(positions==fill);
                }
            }
#endif
            index.refresh(2, query.data()); eq(index.entry(2),q);
            index.refresh(4, query.data()); eq(index.entry(4),q);
        }
        // Position every byte value in each lane, including vector tails.
        for(int pos=0;pos<length;++pos) for(int v=-128;v<=127;++v) {
            std::fill(query.begin(),query.end(),1); query[pos]=int8_t(v);
            CandidateIndex index(length,query.data(),length,1);
            auto s=index.encode(query.data());
            assert(index.conflicts(s,1)==0);
#if FINDHAP_POP_FULL
            int got=-1;assert(index.full_count(s,1,got)==(length<=128 && v>=0 && v<=2));
            if(length<=128 && v>=0 && v<=2) assert(got==0);
#endif
#if FINDHAP_POP_COMPLETE
            if(length<=64) assert(index.complete(s,1)==(v>=0 && v<=2));
#endif
        }
    }
}
