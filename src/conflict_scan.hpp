#pragma once
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <cmath>
#if defined(__x86_64__) || defined(__i386__)
#include <immintrin.h>
#endif

namespace findhap_simd {
// Saturation preserves the predicate even for differences outside int8 range.
// No intrinsic load is issued unless the complete vector is inside the segment.
#if defined(__x86_64__) || defined(__i386__)
__attribute__((target("sse2"))) inline uint32_t mask16(const int8_t* a, const int8_t* b) {
    const auto d = _mm_subs_epi8(_mm_loadu_si128(reinterpret_cast<const __m128i*>(a)),
                                 _mm_loadu_si128(reinterpret_cast<const __m128i*>(b)));
    return uint32_t(_mm_movemask_epi8(_mm_or_si128(_mm_cmpgt_epi8(d, _mm_set1_epi8(1)),
                                 _mm_cmpgt_epi8(_mm_set1_epi8(-1), d))));
}
__attribute__((target("avx2"))) inline uint32_t mask32(const int8_t* a, const int8_t* b) {
    const auto d = _mm256_subs_epi8(_mm256_loadu_si256(reinterpret_cast<const __m256i*>(a)),
                                    _mm256_loadu_si256(reinterpret_cast<const __m256i*>(b)));
    return uint32_t(_mm256_movemask_epi8(_mm256_or_si256(_mm256_cmpgt_epi8(d, _mm256_set1_epi8(1)),
                                 _mm256_cmpgt_epi8(_mm256_set1_epi8(-1), d))));
}
__attribute__((target("avx512f,avx512bw"))) inline uint64_t mask64(const int8_t* a, const int8_t* b) {
    const auto d = _mm512_subs_epi8(_mm512_loadu_si512(a), _mm512_loadu_si512(b));
    return uint64_t(_mm512_cmpgt_epi8_mask(d, _mm512_set1_epi8(1))) |
           uint64_t(_mm512_cmpgt_epi8_mask(_mm512_set1_epi8(-1), d));
}
#endif

inline bool supported(int backend) {
#if defined(__x86_64__) || defined(__i386__)
    if (backend == 1) return __builtin_cpu_supports("sse2");
    if (backend == 2) return __builtin_cpu_supports("avx2");
    if (backend == 3) return __builtin_cpu_supports("avx512f") && __builtin_cpu_supports("avx512bw");
#else
    if (backend) return false;
#endif
    return backend == 0;
}
inline int selected_backend() {
    const char* option = std::getenv("TURBOHAP_SIMD");
    int selected = 0;
    if (!option || !std::strcmp(option, "auto")) selected = supported(2) ? 2 : 0;
    else if (!std::strcmp(option,"portable")) selected=0;
    else if (!std::strcmp(option,"sse2")) selected=1;
    else if (!std::strcmp(option,"avx2")) selected=2;
    else if (!std::strcmp(option,"avx512bw")) selected=3;
    else { std::fputs("Invalid TURBOHAP_SIMD backend\n",stderr);std::exit(2); }
    if (!supported(selected)) { std::fputs("Unsupported TURBOHAP_SIMD backend\n",stderr);std::exit(2); }
    return selected;
}

template<int Mode, int Backend>
inline int scan(const int8_t* a,const int8_t* b,int length,float limit) {
    int count=0,l=0;
#if defined(__x86_64__) || defined(__i386__)
    constexpr int width=Backend==3 ? 64 : Backend==2 ? 32 : 16;
    for (; length-l>=width; l+=width) {
        uint64_t mask;
        if constexpr (Backend==3) mask=mask64(a+l,b+l);
        else if constexpr (Backend==2) mask=mask32(a+l,b+l);
        else mask=mask16(a+l,b+l);
        if constexpr (Mode==1) { if (mask) return -1; }
        count+=__builtin_popcountll(mask);
        if (count>limit) return -1;
    }
#endif
    for (;l<length;++l) count+=(std::abs(int(a[l])-int(b[l]))>1);
    return count;
}
} // namespace findhap_simd
