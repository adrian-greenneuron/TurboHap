#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include "platform_policy.hpp"
#if FINDHAP_ORDERED_SCAN_NEON && defined(__ARM_NEON)
#include <arm_neon.h>
#endif
namespace findhap_ordered_scan {
template<class Block,class Any,class Remove>
inline __attribute__((always_inline)) Block filter(Block candidates, uint64_t first, uint64_t second,
        const uint64_t* first_planes,const uint64_t* second_planes,size_t words,size_t word,Any any,Remove remove) {
#if FINDHAP_ORDERED_SCAN_ORDER == 3
    while((first || second) && any(candidates)) {
        if(first) {candidates=remove(candidates,first_planes+size_t(__builtin_ctzll(first))*words+word);first&=first-1;}
        if(second && any(candidates)) {candidates=remove(candidates,second_planes+size_t(__builtin_ctzll(second))*words+word);second&=second-1;}
    }
#else
    while(first && any(candidates)) {candidates=remove(candidates,first_planes+size_t(__builtin_ctzll(first))*words+word);first&=first-1;}
    while(second && any(candidates)) {candidates=remove(candidates,second_planes+size_t(__builtin_ctzll(second))*words+word);second&=second-1;}
#endif
    return candidates;
}
// Word-major planes retain adjacent loci for each 64-candidate word. Only
// rejection-predicate order changes; candidate positions remain increasing.
#if FINDHAP_ORDERED_SCAN_ORDER
inline int next_word_major(int first,int last,size_t width,const uint64_t* active,
        const uint64_t* zero,const uint64_t* two,uint64_t query_zero,uint64_t query_two) {
    const uint64_t* p0=two;const uint64_t* p1=zero;
    uint64_t q0=query_zero,q1=query_two;
#if FINDHAP_ORDERED_SCAN_ORDER == 1
    const bool swap=true;
#elif FINDHAP_ORDERED_SCAN_ORDER == 2
    const bool swap=q1 && (!q0 || __builtin_popcountll(q1)<__builtin_popcountll(q0));
#else
    const bool swap=false;
#endif
    if(swap){std::swap(p0,p1);std::swap(q0,q1);}
    const size_t initial=size_t(first-1)/64, final=size_t(last-1)/64;
    for(size_t word=initial;word<=final;++word) {
        uint64_t candidates=active[word];
        if(word==initial)candidates&=UINT64_MAX<<((first-1)%64);
        candidates=filter(candidates,q0,q1,p0+word*width,p1+word*width,1,0,
            [](uint64_t value){return value!=0;},
            [](uint64_t value,const uint64_t* plane){return value&~*plane;});
        if(candidates)return std::min(last+1,int(word*64+__builtin_ctzll(candidates)+1));
    }
    return last+1;
}
#endif

// These are independent Boolean rejection filters. Their evaluation order and
// word width cannot change the earliest surviving original order position.
inline int next(int first,int last,size_t words,const uint64_t* active,
                const uint64_t* zero,const uint64_t* two,uint64_t query_zero,uint64_t query_two) {
    if(first>last)return last+1;
    const uint64_t* p0=two;const uint64_t* p1=zero;
    uint64_t q0=query_zero,q1=query_two;
#if FINDHAP_ORDERED_SCAN_ORDER == 1
    const bool swap=true;
#elif FINDHAP_ORDERED_SCAN_ORDER == 2
    const bool swap=q1 && (!q0 || __builtin_popcountll(q1)<__builtin_popcountll(q0));
#else
    const bool swap=false;
#endif
    if(swap){std::swap(p0,p1);std::swap(q0,q1);}
    const size_t initial=size_t(first-1)/64,final=size_t(last-1)/64;
    for(size_t word=initial;word<=final;) {
#if FINDHAP_ORDERED_SCAN_NEON && defined(__ARM_NEON)
        if(word+1<words && word+1<=final) {
            auto candidates=vld1q_u64(active+word);
            if(word==initial)candidates=vsetq_lane_u64(vgetq_lane_u64(candidates,0)&(UINT64_MAX<<((first-1)%64)),candidates,0);
            candidates=filter(candidates,q0,q1,p0,p1,words,word,
                [](uint64x2_t v){return vmaxvq_u32(vreinterpretq_u32_u64(v))!=0;},
                [](uint64x2_t v,const uint64_t* p){return vbicq_u64(v,vld1q_u64(p));});
            const uint64_t low=vgetq_lane_u64(candidates,0),high=vgetq_lane_u64(candidates,1);
            if(low)return std::min(last+1,int(word*64+__builtin_ctzll(low)+1));
            if(high)return std::min(last+1,int((word+1)*64+__builtin_ctzll(high)+1));
            word+=2;continue;
        }
#endif
        uint64_t candidates=active[word];
        if(word==initial)candidates&=UINT64_MAX<<((first-1)%64);
        candidates=filter(candidates,q0,q1,p0,p1,words,word,
            [](uint64_t v){return v!=0;},[](uint64_t v,const uint64_t* p){return v&~*p;});
        if(candidates)return std::min(last+1,int(word*64+__builtin_ctzll(candidates)+1));
        ++word;
    }
    return last+1;
}
}
