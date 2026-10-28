#include "../src/high_memory_index.hpp"
#include <cassert>
#include <numeric>
#include <random>
struct Entry { uint64_t zero, two; };
int main() {
    std::mt19937_64 rng(271026);
    for(int count : {1,63,64,65,127,128,129,1024,1500}) {
        std::vector<int> order(count+1),freq(count);
        std::vector<Entry> entries(count);
        std::iota(order.begin(),order.end(),0);
        for(auto& f:freq) f=int(rng()%100)-50;
        for(auto& e:entries) {e.zero=rng();e.two=rng() & ~e.zero;}
        OrderedCandidateLookup index(64);
        assert(index.prepare(order.data(),freq.data(),count,entries,true));
        for(int rep=0;rep<3000;++rep) {
            int h=1+rng()%count;freq[h-1]+=int(rng()%9)-4;index.frequency_changed(h);
            if(rep%3==0) {
                int a=1+rng()%count,b=1+rng()%count;std::swap(order[a],order[b]);
                index.changed_positions(a,b,entries);
            }
            if(rep%7==0) {entries[h-1]={rng(),0};entries[h-1].two=rng() & ~entries[h-1].zero;index.refresh(h,entries[h-1].zero,entries[h-1].two);}
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
}
