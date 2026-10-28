#include "../src/conflict_scan.hpp"
#include <chrono>
#include <vector>
#include <random>
#include <iostream>
#include <algorithm>
using namespace findhap_simd;

__attribute__((noinline)) int portable(const int8_t*a,const int8_t*b,int len,float limit){
 int conflicts=0;
 for(int begin=0;begin<len;){int end=begin+std::min(len-begin,32);
 #pragma omp simd reduction(+:conflicts)
 for(int l=begin;l<end;++l)conflicts+=(std::abs(int(a[l])-int(b[l]))>1);
 if(conflicts>limit)return -1;begin=end;}return conflicts;
}
template<int B> __attribute__((noinline)) int kernel(const int8_t*a,const int8_t*b,int len,float limit){return scan<0,B>(a,b,len,limit);}
int main(){
 using Fn=int(*)(const int8_t*,const int8_t*,int,float);
 Fn funcs[]={portable,kernel<1>,kernel<2>,kernel<3>};volatile uint64_t sink=0;std::mt19937 rng(271026);
 std::cout<<"backend,length,pattern,repeat,nanoseconds_per_call,checksum\n";
 for(int len:{24,30,31,32,44,64,100,128,257})for(int pattern=0;pattern<4;++pattern){
  std::vector<int8_t>a(32*len),b(32*len);
  for(int q=0;q<32;++q){for(int l=0;l<len;++l){a[q*len+l]=2*(rng()%2);b[q*len+l]=a[q*len+l];}
   if(pattern==1)b[q*len]=2-a[q*len];
   if(pattern==2)b[q*len+len-1]=2-a[q*len+len-1];
   if(pattern==3)for(int l=0;l<len;l+=7)b[q*len+l]=2-a[q*len+l];}
  for(int rep=0;rep<5;++rep)for(int x=0;x<4;++x){int backend=(x+rep)%4;if(!supported(backend))continue;
   auto fn=funcs[backend];uint64_t sum=0;auto start=std::chrono::steady_clock::now();
   for(int i=0;i<100000;++i){int off=(i%32)*len;sum+=uint64_t(fn(a.data()+off,b.data()+off,len,0));}
   auto end=std::chrono::steady_clock::now();sink+=sum;
   std::cout<<backend<<','<<len<<','<<pattern<<','<<rep<<','<<std::chrono::duration<double,std::nano>(end-start).count()/100000<<','<<sum<<'\n';
  }
 }return sink==12345;
}
