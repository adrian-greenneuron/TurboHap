#include "../src/population_segment.hpp"
#include <algorithm>
#include <cstdlib>
#include <stdexcept>
#include <vector>
#include <sys/mman.h>
#include <unistd.h>
static_assert(FINDHAP_POP_FINAL_SCAN_NEON==FINDHAP_TEST_POP_FINAL_SCAN_NEON);
static_assert(FINDHAP_POP_FINAL_SCAN_NEON==1);
static void require(bool v) {if(!v)throw std::runtime_error("final scan state oracle mismatch");}
static bool scalar(const int8_t* segment,const int8_t* hap,int n,int* fill,int& miss) {
    miss=0;
    for(int l=0;l<n;++l) {
        if(std::abs(int(segment[l])-int(hap[l]))>1)return false;
        if(hap[l]==1 && segment[l]!=1) {++miss;fill[miss-1]=l;}
    }
    return true;
}
static void check(std::vector<int8_t> segment,std::vector<int8_t> hap) {
    const int n=int(segment.size());auto old_segment=segment,old_hap=hap;
    std::vector<int> a(n+4),b;for(int l=0;l<n+4;++l)a[l]=-100-l;b=a;
    int ma=-3,mb=-7;
    const bool ra=scalar(segment.data(),hap.data(),n,a.data()+2,ma);
    const bool rb=findhap_population::final_fill_positions(segment.data(),hap.data(),n,b.data()+2,mb);
    require(ra==rb && ma==mb && a==b && segment==old_segment && hap==old_hap);
    // The caller commits hap1 only on success; compare the complete final state.
    auto ha=hap,hb=hap;
    if(ra)for(int i=0;i<ma;++i)ha[a[i+2]]=segment[a[i+2]];
    if(rb)for(int i=0;i<mb;++i)hb[b[i+2]]=segment[b[i+2]];
    require(ha==hb);
}
int main() {
    for(int n:{0,1,15,16,17,31,32,33,63,64,65,99,100,127,128,129,257}) {
        std::vector<int8_t> segment(n,0),hap(n,1);
        check(segment,hap);
        // Every first-conflict locus, including every lane and scalar tail.
        for(int first=0;first<n;++first) {
            hap.assign(n,1);hap[first]=2;
            if(first+1<n)hap[first+1]=2;
            check(segment,hap);
        }
        // Preserve the ENTIRE original backsolve scratch, even on failed scan.
        for(int alias=0;alias<2;++alias) {
            std::vector<int8_t> raw(n),h2(n);hap.assign(n,1);
            for(int l=0;l<n;++l){raw[l]=l%4==3?5:int8_t(l%3);h2[l]=int8_t((l+1)%3);hap[l]=int8_t((l+2)%3);}
            const auto& second=alias?hap:h2;
            for(int l=0;l<n;++l)segment[l]=(raw[l]==5 || second[l]==1)?1:int8_t(2*int(raw[l])-int(second[l]));
            check(segment,hap);
        }
    }
    // Exact signed-byte conflict semantics, independently counted as int.
    for(int a=-128;a<=127;++a)for(int b=-128;b<=127;++b)
        check(std::vector<int8_t>(16,int8_t(a)),std::vector<int8_t>(16,int8_t(b)));
    const size_t page=size_t(sysconf(_SC_PAGESIZE));
    auto allocate=[&]{auto p=static_cast<unsigned char*>(mmap(nullptr,3*page,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0));require(p!=MAP_FAILED);require(!mprotect(p+2*page,page,PROT_NONE));return p;};
    auto ps=allocate(),ph=allocate(),pf=allocate();
    for(int n:{0,1,15,16,17,31,32,33,63,64,65,99,100,127,128,129,257}) {
        auto s=reinterpret_cast<int8_t*>(ps+2*page-n),h=reinterpret_cast<int8_t*>(ph+2*page-n);
        auto f=reinterpret_cast<int*>(pf+2*page-size_t(n)*sizeof(int));
        for(int l=0;l<n;++l){s[l]=0;h[l]=1;f[l]=-1;}
        int miss=-1;require(findhap_population::final_fill_positions(s,h,n,f,miss) && miss==n);
        for(int l=0;l<n;++l)require(f[l]==l);
    }
    const int a=munmap(ps,3*page),b=munmap(ph,3*page),c=munmap(pf,3*page);
    require(a==0 && b==0 && c==0);
}
