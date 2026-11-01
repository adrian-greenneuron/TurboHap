#include "../src/packed_genotypes.hpp"
#include "../src/genotype_text.hpp"
#include <cassert>
#include <random>
#include <vector>
#include <sys/mman.h>
#include <unistd.h>

struct Guard {
    void* base;size_t bytes;uint8_t* data;
    explicit Guard(size_t count) {
        const size_t page=size_t(sysconf(_SC_PAGESIZE)), active=std::max(page,(count+page-1)/page*page);
        bytes=active+page;base=mmap(nullptr,bytes,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANON,-1,0);
        assert(base!=MAP_FAILED);assert(mprotect(static_cast<char*>(base)+active,page,PROT_NONE)==0);
        data=static_cast<uint8_t*>(base)+active-count;
    }
    ~Guard(){assert(munmap(base,bytes)==0);}
};
int main() {
    std::mt19937 rng(311026);
    std::vector<size_t> lengths;
    for(size_t n=0;n<=257;++n)lengths.push_back(n);
    for(size_t n : {511,512,513,4095,4096,4097})lengths.push_back(n);
    for(size_t n : lengths) {
        Guard source(n),packed((n+3)/4),decoded(n);
        std::vector<uint8_t> expected((n+3)/4,0);
        for(size_t l=0;l<n;++l) {
            source.data[l]=uint8_t(rng());
            const auto code=source.data[l]==5?3:source.data[l];
            expected[l/4]|=code<<(2*(l%4));
        }
        findhap_text::decode(reinterpret_cast<const char*>(source.data),reinterpret_cast<int8_t*>(decoded.data),int(n));
        for(size_t l=0;l<n;++l)assert(static_cast<int8_t>(decoded.data[l]) == (source.data[l]==' '?0:static_cast<int8_t>(source.data[l]-'0')));
        findhap_packed::pack(packed.data,reinterpret_cast<int8_t*>(source.data),n);
        assert(std::equal(expected.begin(),expected.end(),packed.data));
        // Exercise every packed byte independently of the packer's input domain.
        for(size_t l=0;l<(n+3)/4;++l)packed.data[l]=uint8_t(l+rng());
        findhap_packed::unpack(reinterpret_cast<int8_t*>(decoded.data),packed.data,n);
        for(size_t l=0;l<n;++l) {
            const auto code=(packed.data[l/4]>>(2*(l%4)))&3;
            assert(decoded.data[l]==(code==3?5:code));
        }
    }
}
