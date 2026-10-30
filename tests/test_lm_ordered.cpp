#include "../src/lm_ordered_index.hpp"
#include <cassert>
#include <numeric>
#include <random>
struct Entry { uint64_t zero, two; };
int main() {
    std::mt19937_64 rng(271026);
    for(int count : {1,63,64,65,127,128,129,1024,1500,32768,65535}) {
        std::vector<int> order(count+1),freq(count);
        std::vector<Entry> entries(count);
        std::iota(order.begin(),order.end(),0);
        for(auto& f:freq) f=int(rng()%100)-50;
        for(auto& e:entries) {e.zero=rng();e.two=rng() & ~e.zero;}
        LowMemoryCandidateLookup index(64);
        assert(index.prepare(order.data(),freq.data(),count,entries,true));
        assert(index.resident_bytes() <= LowMemoryCandidateLookup::storage_bound(64,count));
        for(int rep=0;rep<3000;++rep) {
            int h=1+rng()%count;freq[h-1]+=int(rng()%9)-4;index.frequency_changed(h);
            if(rep%3==0) {
                int a=1+rng()%count,b=1+rng()%count;std::swap(order[a],order[b]);
                index.changed_positions(a,b,entries);
            }
            if(rep%7==0) {auto previous=entries[h-1];entries[h-1]={rng(),0};entries[h-1].two=rng() & ~entries[h-1].zero;index.refresh(h,previous.zero,previous.two);}
            int first=1+rng()%count,last=1+rng()%count;if(first>last)std::swap(first,last);
            int want=99999999,pos=0;
            for(int k=first;k<=last;++k)if(freq[order[k]-1]<want){want=freq[order[k]-1];pos=k;}
            int got=99999999,gpos=0;index.minimum(first,last,got,gpos);assert(got==want && gpos==pos);
            uint64_t zero=rng(),two=rng() & ~zero;
            int next=first;
            while(next<=last && ((zero&entries[order[next]-1].two)|(two&entries[order[next]-1].zero)))++next;
            assert(index.next(first,last,zero,two)==next);
        }
    }
    // A library beyond the 16-bit position range must fall back before any
    // position is truncated or an index allocation is made.
    std::vector<int> large_order(65537), large_frequencies(65536, 0);
    std::vector<Entry> large_entries(65536, Entry{0,0});
    std::iota(large_order.begin(), large_order.end(), 0);
    LowMemoryCandidateLookup fallback(64);
    assert(!fallback.prepare(large_order.data(), large_frequencies.data(),
                             65536, large_entries));
    assert(fallback.resident_bytes() == 0);
}
