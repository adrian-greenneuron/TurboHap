#pragma once

#include "platform_policy.hpp"
#include <algorithm>
#include <cstddef>
#include <cstdint>

#if (defined(__i386__) || defined(__x86_64__)) && (defined(__GNUC__) || defined(__clang__))
#include <immintrin.h>
#define TURBOHAP_X86_SCAN_DISPATCH 1
#else
#define TURBOHAP_X86_SCAN_DISPATCH 0
#if defined(__ARM_NEON)
#include <arm_neon.h>
#endif
#endif

namespace findhap_ordered_scan {
constexpr size_t batch_words = 8;
constexpr size_t batch_candidates = 64 * batch_words;

#if TURBOHAP_RUNTIME_SCAN_LAYOUT
inline bool runtime_blocked_layout() { return __builtin_cpu_supports("avx512f"); }
#endif

// In the blocked layout, an eight-word block owns its adjacent loci. The last
// incomplete block is compact, so this still uses exactly words * width.
inline size_t plane_index(size_t site, size_t word, size_t words, size_t width) {
#if TURBOHAP_TILED_SCAN || TURBOHAP_RUNTIME_SCAN_LAYOUT
#if TURBOHAP_RUNTIME_SCAN_LAYOUT
    if (!runtime_blocked_layout()) return word * width + site;
#endif
    const size_t base = word / batch_words * batch_words;
    const size_t stride = std::min(batch_words, words - base);
    return base * width + site * stride + word - base;
#elif FINDHAP_ORDERED_LAYOUT
    (void)words;
    return word * width + site;
#else
    (void)width;
    return site * words + word;
#endif
}

// The two masks describe opposite known alleles. Intersecting the planes in
// either order gives the same survivors. The pointers start at this block.
struct Query {
    const uint64_t* planes[2];
    uint64_t masks[2];
    size_t word_stride;
    size_t site_stride;
};

inline void scalar(const uint64_t* active, const Query& query, uint64_t* out, size_t words) {
    for (size_t w = 0; w < words; ++w) {
        uint64_t live = active[w];
        for (int side = 0; side < 2; ++side) {
            for (uint64_t sites = query.masks[side]; sites && live; sites &= sites - 1) {
                const size_t site = __builtin_ctzll(sites);
                live &= ~query.planes[side][site * query.site_stride + w * query.word_stride];
            }
        }
        out[w] = live;
    }
}

#if TURBOHAP_X86_SCAN_DISPATCH
// Keep these instructions local to their target functions. A portable x86
// executable can select either kernel after checking CPU and OS support.
__attribute__((target("avx512f")))
inline void avx512(const uint64_t* active, const Query& q, uint64_t* out) {
#if !TURBOHAP_TILED_SCAN && !TURBOHAP_RUNTIME_SCAN_LAYOUT
    const int w = static_cast<int>(q.word_stride);  // 1 or the signature width (at most 64).
    const __m256i offsets = _mm256_setr_epi32(0, w, 2*w, 3*w, 4*w, 5*w, 6*w, 7*w);
#endif
    __m512i live = _mm512_loadu_si512(active);
    for (int side = 0; side < 2; ++side) {
        for (uint64_t sites = q.masks[side]; sites && _mm512_test_epi64_mask(live, live); sites &= sites - 1) {
            const auto* plane = q.planes[side] + __builtin_ctzll(sites) * q.site_stride;
#if TURBOHAP_TILED_SCAN || TURBOHAP_RUNTIME_SCAN_LAYOUT
            live = _mm512_andnot_si512(_mm512_loadu_si512(plane), live);
#else
            live = _mm512_andnot_si512(_mm512_i32gather_epi64(offsets, plane, 8), live);
#endif
        }
    }
    _mm512_storeu_si512(out, live);
}

__attribute__((target("avx2")))
inline void avx2(const uint64_t* active, const Query& q, uint64_t* out) {
#if !TURBOHAP_FIXED_TILE_STRIDE
    const int w = static_cast<int>(q.word_stride);
#endif
#if !TURBOHAP_FIXED_TILE_STRIDE && !TURBOHAP_NATIVE_MILAN
    const __m128i offsets = _mm_setr_epi32(0, w, 2*w, 3*w);
#endif
    __m256i lo = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(active));
    __m256i hi = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(active + 4));
    for (int side = 0; side < 2; ++side) {
        for (uint64_t sites = q.masks[side]; sites; sites &= sites - 1) {
            const __m256i both = _mm256_or_si256(lo, hi);
            if (_mm256_testz_si256(both, both)) break;
            const auto* plane = q.planes[side] + __builtin_ctzll(sites) * q.site_stride;
#if TURBOHAP_FIXED_TILE_STRIDE
            lo = _mm256_andnot_si256(_mm256_loadu_si256(reinterpret_cast<const __m256i*>(plane)), lo);
            hi = _mm256_andnot_si256(_mm256_loadu_si256(reinterpret_cast<const __m256i*>(plane + 4)), hi);
#elif TURBOHAP_NATIVE_MILAN
            // Zen 3 loads these regularly strided words faster with ordinary loads.
            lo = _mm256_andnot_si256(_mm256_setr_epi64x(plane[0*w], plane[1*w], plane[2*w], plane[3*w]), lo);
            hi = _mm256_andnot_si256(_mm256_setr_epi64x(plane[4*w], plane[5*w], plane[6*w], plane[7*w]), hi);
#else
            lo = _mm256_andnot_si256(_mm256_i32gather_epi64(reinterpret_cast<const long long*>(plane), offsets, 8), lo);
            hi = _mm256_andnot_si256(_mm256_i32gather_epi64(reinterpret_cast<const long long*>(plane + 4*w), offsets, 8), hi);
#endif
        }
    }
    _mm256_storeu_si256(reinterpret_cast<__m256i*>(out), lo);
    _mm256_storeu_si256(reinterpret_cast<__m256i*>(out + 4), hi);
}

inline void scalar_full(const uint64_t* active, const Query& q, uint64_t* out) {
    scalar(active, q, out, batch_words);
}
using Kernel = void (*)(const uint64_t*, const Query&, uint64_t*);
inline Kernel selected_kernel() {
    // The compiler's feature probe includes the OS vector-register state.
    static const Kernel kernel = __builtin_cpu_supports("avx512f") ? avx512 :
                                 __builtin_cpu_supports("avx2") ? avx2 : scalar_full;
    return kernel;
}
#elif defined(__ARM_NEON)
inline void neon(const uint64_t* active, const Query& q, uint64_t* out) {
    uint64x2_t live[4];
    for (size_t v = 0; v < 4; ++v) live[v] = vld1q_u64(active + 2*v);
    for (int side = 0; side < 2; ++side) {
        for (uint64_t sites = q.masks[side]; sites; sites &= sites - 1) {
            const auto either = vorrq_u64(vorrq_u64(live[0], live[1]), vorrq_u64(live[2], live[3]));
#if defined(__aarch64__) || defined(__arm64__)
            if (!vmaxvq_u32(vreinterpretq_u32_u64(either))) break;
#else
            if (!(vgetq_lane_u64(either, 0) | vgetq_lane_u64(either, 1))) break;
#endif
            const auto* plane = q.planes[side] + __builtin_ctzll(sites) * q.site_stride;
            for (size_t v = 0; v < 4; ++v) {
#if TURBOHAP_FIXED_TILE_STRIDE
                const auto conflicts = vld1q_u64(plane + 2*v);
#else
                const auto* p = plane + 2*v*q.word_stride;
#if TURBOHAP_APPLE_ARM_NEON || TURBOHAP_BLOCKED_LOADS
                // Each adjacent word pair is valid in a full block. The
                // 128-bit load requires no alignment beyond the uint64_t array.
                const auto conflicts = q.word_stride == 1 ? vld1q_u64(p) :
                    vcombine_u64(vld1_u64(p), vld1_u64(p + q.word_stride));
#else
                const auto conflicts = vcombine_u64(vld1_u64(p), vld1_u64(p + q.word_stride));
#endif
#endif
                live[v] = vbicq_u64(live[v], conflicts);
            }
        }
    }
    for (size_t v = 0; v < 4; ++v) vst1q_u64(out + 2*v, live[v]);
}
#endif

inline const char* backend() {
#if TURBOHAP_X86_SCAN_DISPATCH
    const Kernel kernel = selected_kernel();
    if (kernel == avx512) return "AVX-512F";
    if (kernel == avx2) return "AVX2";
#elif defined(__ARM_NEON)
    return "NEON";
#endif
    return "scalar";
}

// Per lookup instance: the next search can resume within the block, but any
// change to its active slots, alleles or storage invalidates the saved mask.
class Cache {
    size_t base_ = SIZE_MAX;
    uint64_t query_zero_ = 0, query_two_ = 0;
    uint64_t survivors_[batch_words]{};
public:
    void invalidate() { base_ = SIZE_MAX; }

    int next(int first, int last, size_t words, size_t width, const uint64_t* active,
             const uint64_t* zero, const uint64_t* two, uint64_t q0, uint64_t q2) {
        if (first > last) return last + 1;
        const size_t first_word = size_t(first - 1) / 64, last_word = size_t(last - 1) / 64;
#if TURBOHAP_RUNTIME_SCAN_LAYOUT
        const bool blocked = runtime_blocked_layout();
        const size_t word_stride = blocked ? 1 : width;
#elif TURBOHAP_FIXED_TILE_STRIDE
        constexpr size_t word_stride = 1;
#elif !TURBOHAP_TILED_SCAN
        const size_t word_stride = FINDHAP_ORDERED_LAYOUT ? width : 1;
        const size_t site_stride = FINDHAP_ORDERED_LAYOUT ? 1 : words;
#endif
        for (size_t base = first_word / batch_words * batch_words; base <= last_word; base += batch_words) {
            if (base != base_ || q0 != query_zero_ || q2 != query_two_) {
                // Keep the expression order of each timed kernel.
#if TURBOHAP_APPLE_ARM_NEON || TURBOHAP_BLOCKED_LOADS || TURBOHAP_RUNTIME_SCAN_LAYOUT
                const size_t used = std::min(batch_words, words - base);
#elif TURBOHAP_FIXED_TILE_STRIDE
                const size_t site_stride = std::min(batch_words, words - base);
#endif
#if TURBOHAP_TILED_SCAN || TURBOHAP_RUNTIME_SCAN_LAYOUT
                const size_t offset = base * width;
#else
                const size_t offset = base * word_stride;
#endif
#if TURBOHAP_RUNTIME_SCAN_LAYOUT
                Query q{{two + offset, zero + offset}, {q0, q2}, word_stride, blocked ? used : 1};
#elif (TURBOHAP_APPLE_ARM_NEON && TURBOHAP_TILED_SCAN) || TURBOHAP_BLOCKED_LOADS
                Query q{{two + offset, zero + offset}, {q0, q2}, 1, used};
#else
                Query q{{two + offset, zero + offset}, {q0, q2}, word_stride, site_stride};
#endif
#if FINDHAP_ORDERED_SCAN_ORDER == 1
                const bool swap = true;
#elif FINDHAP_ORDERED_SCAN_ORDER == 2
                const bool swap = q2 && (!q0 || __builtin_popcountll(q2) < __builtin_popcountll(q0));
#else
                const bool swap = false;
#endif
                if (swap) {
                    std::swap(q.planes[0], q.planes[1]);
                    std::swap(q.masks[0], q.masks[1]);
                }
#if !TURBOHAP_APPLE_ARM_NEON && !TURBOHAP_BLOCKED_LOADS && !TURBOHAP_RUNTIME_SCAN_LAYOUT
                const size_t used = std::min(batch_words, words - base);
#endif
                if (used < batch_words) {
                    scalar(active + base, q, survivors_, used);
                } else {
#if TURBOHAP_X86_SCAN_DISPATCH
                    selected_kernel()(active + base, q, survivors_);
#elif defined(__ARM_NEON)
                    neon(active + base, q, survivors_);
#else
                    scalar(active + base, q, survivors_, batch_words);
#endif
                }
                base_ = base;
                query_zero_ = q0;
                query_two_ = q2;
            }
            const size_t end = std::min(base + batch_words, last_word + 1);
            for (size_t w = std::max(first_word, base); w < end; ++w) {
                uint64_t live = survivors_[w - base];
                if (w == first_word) live &= UINT64_MAX << ((first - 1) % 64);
                if (live) return std::min(last + 1, int(w * 64 + __builtin_ctzll(live) + 1));
            }
        }
        return last + 1;
    }
};
}
