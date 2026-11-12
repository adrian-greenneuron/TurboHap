#include "../src/candidate_index.hpp"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <random>

static void require(bool ok) {
    if(!ok){std::fputs("Sampled-predicate set or full signature changed\n",stderr);std::exit(1);}
}

int main() {
    std::mt19937 rng(9112026);
    for(int length:{1,15,16,17,31,32,33,49,50,51,63,64,65,99,100,127,128,129,257}) {
        const int width=std::min(length,64);
        std::vector<int8_t> query(length),library(length*3);
        for(int trial=0;trial<1024;++trial) {
            for(auto& x:query)x=trial<512?int8_t(rng()%3):int8_t(rng()%256);
            for(auto& x:library)x=trial<512?int8_t(rng()%3):int8_t(rng()%256);
            CandidateIndex index(length,library.data(),length,3);
            const auto signature=index.encode(query.data());
            for(int h=1;h<=3;++h) {
                const auto* values=library.data()+(h-1)*length;
                int sampled_conflicts=0,full_conflicts=0,zeros=0,twos=0;
                bool valid=length<=128;
                for(int k=0;k<width;++k) {
                    const int pos=width>1?int(int64_t(k)*(length-1)/(width-1)):0;
                    const int a=query[pos],b=values[pos];
                    sampled_conflicts+=(a==0&&b==2)||(a==2&&b==0);
                    zeros+=a==0;twos+=a==2;
                }
                require(__builtin_popcountll(signature.zero)==zeros);
                require(__builtin_popcountll(signature.two)==twos);
                require(__builtin_popcountll(index.conflicts(signature,h))==sampled_conflicts);
                std::vector<int> fill;
                for(int k=0;k<length;++k) {
                    int a=query[k],b=values[k];valid=valid&&a>=0&&a<=2&&b>=0&&b<=2;
                    full_conflicts+=std::abs(a-b)>1;
                    if(b==1&&a!=1)fill.push_back(k);
                }
#if FINDHAP_POP_FULL
                int count=-1;require(index.full_count(signature,h,count)==valid);
                if(valid) {
                    require(count==full_conflicts);std::vector<int> actual(length);
                    int n=index.full_fill(signature,h,actual.data());actual.resize(n);require(actual==fill);
                }
#endif
            }
        }
        // Every original sampled locus must appear exactly once, including
        // unchanged compact short widths and the 64/128-byte boundaries.
        for(int k=0;k<width;++k) {
            int pos=width>1?int(int64_t(k)*(length-1)/(width-1)):0;
            std::fill(query.begin(),query.end(),1);std::fill(library.begin(),library.end(),1);
            query[pos]=0;library[pos]=2;
            CandidateIndex index(length,library.data(),length,1);
            require(__builtin_popcountll(index.conflicts(index.encode(query.data()),1))==1);
        }
    }
    std::puts("PASS: unchanged sampled-locus sets, signed bytes, full conflicts and ordered fill positions");
}
