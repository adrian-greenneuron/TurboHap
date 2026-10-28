#include "../src/conflict_scan.hpp"
#include <vector>
#include <iostream>
#include <random>
#include <sys/mman.h>
#include <unistd.h>
#include <cassert>
using namespace findhap_simd;

template<int B> void test() {
    if (!supported(B)) return;
    constexpr int width=B==3?64:B==2?32:16;
    std::vector<int8_t>a(128),b(128);
    for(int x=-128;x<=127;++x) for(int y=-128;y<=127;++y) {
        std::fill(a.begin(),a.end(),x);std::fill(b.begin(),b.end(),y);
        auto mask=B==1?uint64_t(mask16(a.data()+1,b.data()+1)):B==2?uint64_t(mask32(a.data()+1,b.data()+1)):mask64(a.data()+1,b.data()+1);
        auto expected=std::abs(x-y)>1?(width==64?UINT64_MAX:(uint64_t(1)<<width)-1):0;
        assert(mask==expected);
    }
    const size_t page=size_t(sysconf(_SC_PAGESIZE));
    auto alloc=[&]{auto p=(int8_t*)mmap(nullptr,3*page,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);assert(p!=MAP_FAILED);assert(!mprotect(p+2*page,page,PROT_NONE));return p;};
    auto p=alloc(),q=alloc();std::mt19937 rng(271026);
    for(int len:{0,1,15,16,17,31,32,33,63,64,65,127,128,129,257,600,4097}) {
        auto s=p+2*page-len,t=q+2*page-len;
        int exact=0;for(int l=0;l<len;++l) {s[l]=int(rng()%256)-128;t[l]=int(rng()%256)-128;exact+=std::abs(int(s[l])-int(t[l]))>1;}
        for(float limit:{-1.f,0.f,.5f,1.f,17.f,1000000.f}) {
            auto n=scan<0,B>(s,t,len,limit);
            assert((n<0||n>limit)==(exact>limit));
            if(n>=0)assert(n==exact);
            auto z=scan<1,B>(s,t,len,limit);
            assert((z<0||z>0||z>limit)==(exact>0||exact>limit));
        }
    }
    munmap(p,3*page);munmap(q,3*page);
    std::cout<<"PASS backend "<<B<<": 65536 pairs, unaligned vectors, guarded tails, thresholds\n";
}
int main(){test<1>();test<2>();test<3>();}
