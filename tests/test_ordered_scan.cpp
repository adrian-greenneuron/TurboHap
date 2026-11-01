#include "../src/ordered_scan.hpp"
#include <cassert>
#include <random>
#include <sys/mman.h>
#include <unistd.h>
struct Guard {
    void* base;size_t bytes;uint64_t* data;
    explicit Guard(size_t count) {
        const size_t page=size_t(sysconf(_SC_PAGESIZE)),n=count*sizeof(uint64_t),active=std::max(page,(n+page-1)/page*page);
        bytes=active+page;base=mmap(nullptr,bytes,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANON,-1,0);assert(base!=MAP_FAILED);
        assert(mprotect(static_cast<char*>(base)+active,page,PROT_NONE)==0);data=reinterpret_cast<uint64_t*>(static_cast<char*>(base)+active-n);
    }
    ~Guard(){assert(munmap(base,bytes)==0);}
};
int main() {
    std::mt19937_64 rng(311026);
    for(size_t words : {1,2,3,4,5,8,17}) {
        Guard active(words),zero(words*64),two(words*64);
        for(int trial=0;trial<400;++trial) {
            for(size_t w=0;w<words;++w)active.data[w]=rng();
            for(size_t q=0;q<words*64;++q){zero.data[q]=rng();two.data[q]=rng();}
            uint64_t qz=0,qt=0;
            if(trial%5==1)qz=uint64_t(1)<<(rng()%64);
            if(trial%5==2){qz=uint64_t(1)<<(rng()%64);qt=uint64_t(1)<<(rng()%64);}
            if(trial%5==3){qz=rng()&rng()&rng();qt=rng()&rng()&rng();}
            if(trial%5==4){qz=rng();qt=rng();}
            int first=1+int(rng()%(words*64)),last=1+int(rng()%(words*64));if(first>last)std::swap(first,last);
            int expected=last+1;
            for(int position=first;position<=last;++position) {
                const size_t word=size_t(position-1)/64;const uint64_t bit=uint64_t(1)<<((position-1)%64);
                bool accepted=bool(active.data[word]&bit);
                for(int site=0;site<64 && accepted;++site)
                    if(((qz>>site)&1 && (two.data[size_t(site)*words+word]&bit)) ||
                       ((qt>>site)&1 && (zero.data[size_t(site)*words+word]&bit)))accepted=false;
                if(accepted){expected=position;break;}
            }
            assert(findhap_ordered_scan::next(first,last,words,active.data,zero.data,two.data,qz,qt)==expected);
        }
        assert(findhap_ordered_scan::next(1,0,words,active.data,zero.data,two.data,0,0)==1);
    }
}
