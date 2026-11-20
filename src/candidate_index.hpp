#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <vector>
#include "platform_policy.hpp"
#include "neon_bits.hpp"
#include "bit_extract.hpp"
#ifndef FINDHAP_LM_ORDERED_BYTES
#define FINDHAP_LM_ORDERED_BYTES 0
#endif
#if defined(FINDHAP_HIGH_MEMORY) || (defined(FINDHAP_LOW_MEMORY) && FINDHAP_LM_ORDERED_BYTES > 0)
#define FINDHAP_USE_ORDERED 1
#else
#define FINDHAP_USE_ORDERED 0
#endif
// Modified build: exact whole-segment allele bitsets for segments longer than
// 64 loci (see MODIFICATIONS.md). They replace the byte comparison of a
// candidate by a few word operations; results are unchanged.
#ifndef FINDHAP_MOD_WIDE
// Standard mode with AVX2 only: no gain was measured in low-memory mode or
// with scalar encoding.
#if defined(__linux__) && defined(__AVX2__) && defined(FINDHAP_HIGH_MEMORY) && !FINDHAP_POP_FULL
#define FINDHAP_MOD_WIDE 1
#else
#define FINDHAP_MOD_WIDE 0
#endif
#endif
#if (FINDHAP_POP_ENCODE || FINDHAP_MOD_WIDE) && defined(__AVX2__)
#include <immintrin.h>
#endif
#if FINDHAP_USE_ORDERED
#include <memory>
#include "high_memory_index.hpp"
#if defined(FINDHAP_LOW_MEMORY)
#include "lm_ordered_index.hpp"
#endif
#endif

#ifdef FINDHAP_ENGINE_NAMESPACE
namespace FINDHAP_ENGINE_NAMESPACE {
#endif

#if FINDHAP_USE_ORDERED
#ifdef FINDHAP_LOW_MEMORY
using ActiveOrderedLookup = LowMemoryCandidateLookup;
#else
using ActiveOrderedLookup = OrderedCandidateLookup;
#endif
#endif

// An exact rejection filter keyed by haplotype ID. Opposite known alleles at
// sampled loci are a lower bound on full-segment conflicts. Unknown alleles
// never reject a candidate, and the caller retains its original search order.
class CandidateIndex {
public:
    struct Signature {
        uint64_t zero = 0;
        uint64_t two = 0;
#if FINDHAP_POP_FULL
        uint64_t full_zero[2] = {}, full_two[2] = {};
        bool full_valid = false;
#endif
#if FINDHAP_POP_COMPLETE
        uint64_t invalid = 0;
#endif
    };

private:
    int width_;
    bool contiguous_;
#if FINDHAP_POP_ENCODE >= 2
    int length_;
#endif
#if FINDHAP_POP_ENCODE == 2
    std::array<int, 64> gather_positions_{}, gather_shifts_{};
#endif
#if FINDHAP_POP_ENCODE == 3 || FINDHAP_POP_ENCODE == 5
    std::array<uint8_t, 64> byte_positions_{};
#endif
    std::array<int, 64> positions_{};
#if FINDHAP_POP_COMPRESS_SIGNATURE && FINDHAP_POP_FULL && !FINDHAP_POP_COMPLETE
    findhap_bit_extract::Selector selectors_[2];
#endif
#if FINDHAP_POP_X86_PEXT && FINDHAP_POP_FULL && !FINDHAP_POP_COMPLETE
    uint64_t selected_masks_[2] = {};
#endif
#if (FINDHAP_POP_COMPRESS_SIGNATURE || FINDHAP_POP_X86_PEXT) && FINDHAP_POP_FULL && !FINDHAP_POP_COMPLETE
    unsigned low_selected_ = 0;
#endif
#if FINDHAP_POP_FULL && FINDHAP_POP_COMPACT
    struct StoredSignature {
        uint64_t zero=0, two=0;
#if FINDHAP_POP_COMPLETE
        uint64_t invalid=0;
#endif
    };
    struct FullData { uint64_t zero[2]={}, two[2]={}; };
    std::vector<StoredSignature> entries_;
    std::vector<FullData> full_;
    std::vector<uint8_t> valid_;
#else
    std::vector<Signature> entries_;
#endif
#if FINDHAP_USE_ORDERED
    std::unique_ptr<ActiveOrderedLookup> ordered_;
    bool ordered_disabled_ = false;
#endif
#if FINDHAP_MOD_WIDE
    // Per haplotype: wide_words_ words of "allele 0" bits, then as many of
    // "allele 2" bits. wide_valid_ is set when every allele is 0, 1 or 2.
    int wide_length_ = 0, wide_words_ = 0;
    std::vector<uint64_t> wide_;
    std::vector<uint8_t> wide_valid_;
#endif

public:
#if FINDHAP_MOD_WIDE
    int wide_words() const { return wide_words_; }
    // Bitsets of one segment. Returns false when an allele is outside 0..2.
    static bool wide_encode(const int8_t* alleles, int length, uint64_t* zero, uint64_t* two) {
        const int words = (length + 63) / 64;
        for (int w = 0; w < words; ++w) { zero[w] = 0; two[w] = 0; }
        bool valid = true;
        int q = 0;
#if defined(__AVX2__)
        const auto vzero = _mm256_setzero_si256();
        const auto vtwo = _mm256_set1_epi8(2);
        auto bad = _mm256_setzero_si256();
        for (; q + 32 <= length; q += 32) {
            const auto values = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(alleles + q));
            zero[q / 64] |= uint64_t(uint32_t(_mm256_movemask_epi8(_mm256_cmpeq_epi8(values, vzero)))) << (q % 64);
            two[q / 64] |= uint64_t(uint32_t(_mm256_movemask_epi8(_mm256_cmpeq_epi8(values, vtwo)))) << (q % 64);
            // Unsigned maximum with 2 differs from 2 exactly for values outside 0..2.
            bad = _mm256_or_si256(bad, _mm256_xor_si256(_mm256_max_epu8(values, vtwo), vtwo));
        }
        valid = _mm256_testz_si256(bad, bad);
#endif
        for (; q < length; ++q) {
            const int value = alleles[q];
            zero[q / 64] |= uint64_t(value == 0) << (q % 64);
            two[q / 64] |= uint64_t(value == 2) << (q % 64);
            valid = valid && value >= 0 && value <= 2;
        }
        return valid;
    }
    // Query side, for any allele values (a backsolved segment holds -2..4):
    // ge2 marks alleles that conflict with a haplotype 0, le0 those that
    // conflict with a haplotype 2, out those that conflict with a haplotype 1.
    static void wide_query(const int8_t* alleles, int length, uint64_t* ge2, uint64_t* le0, uint64_t* out) {
        const int words = (length + 63) / 64;
        for (int w = 0; w < words; ++w) { ge2[w] = 0; le0[w] = 0; out[w] = 0; }
        int q = 0;
#if defined(__AVX2__)
        const auto vzero = _mm256_setzero_si256();
        const auto vone = _mm256_set1_epi8(1);
        const auto vtwo = _mm256_set1_epi8(2);
        for (; q + 32 <= length; q += 32) {
            const auto values = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(alleles + q));
            ge2[q / 64] |= uint64_t(uint32_t(_mm256_movemask_epi8(_mm256_cmpgt_epi8(values, vone)))) << (q % 64);
            le0[q / 64] |= uint64_t(uint32_t(_mm256_movemask_epi8(_mm256_cmpgt_epi8(vone, values)))) << (q % 64);
            out[q / 64] |= uint64_t(uint32_t(_mm256_movemask_epi8(_mm256_or_si256(
                _mm256_cmpgt_epi8(values, vtwo), _mm256_cmpgt_epi8(vzero, values))))) << (q % 64);
        }
#endif
        for (; q < length; ++q) {
            const int value = alleles[q];
            ge2[q / 64] |= uint64_t(value >= 2) << (q % 64);
            le0[q / 64] |= uint64_t(value <= 0) << (q % 64);
            out[q / 64] |= uint64_t(value > 2 || value < 0) << (q % 64);
        }
    }
    // Exact number of loci where |query - haplotype| > 1, as the byte
    // comparison counts them. With stop_at_first the count is only told apart
    // from zero. False when the haplotype row holds values outside 0..2.
    bool wide_count(const uint64_t* ge2, const uint64_t* le0, const uint64_t* out, int haplotype,
                    bool stop_at_first, int& count) const {
        if (!wide_valid_[haplotype - 1]) return false;
        const uint64_t* hap = &wide_[size_t(haplotype - 1) * 2 * wide_words_];
        int total = 0;
        for (int w = 0; w < wide_words_; ++w) {
            const uint64_t zero = hap[w], two = hap[wide_words_ + w];
            const uint64_t bits = (ge2[w] & zero) | (le0[w] & two) | (out[w] & ~(zero | two));
            if (bits) {
                if (stop_at_first) { count = 1; return true; }
                total += __builtin_popcountll(bits);
            }
        }
        count = total;
        return true;
    }
    // After a zero count: loci, in increasing order, where the haplotype is
    // unknown (1) and the query is not (it is 0 or 2 there).
    int wide_fill(const uint64_t* ge2, const uint64_t* le0, int haplotype, int* positions) const {
        const uint64_t* hap = &wide_[size_t(haplotype - 1) * 2 * wide_words_];
        int count = 0;
        for (int w = 0; w < wide_words_; ++w) {
            uint64_t bits = ~(hap[w] | hap[wide_words_ + w]) & (ge2[w] | le0[w]);
            while (bits) { positions[count++] = w * 64 + __builtin_ctzll(bits); bits &= bits - 1; }
        }
        return count;
    }
private:
    void wide_refresh(int haplotype, const int8_t* alleles) {
        if (!wide_words_) return;
        if (wide_valid_.size() < size_t(haplotype)) {
            const size_t grown = std::max(size_t(haplotype), wide_valid_.size() + wide_valid_.size() / 2);
            wide_valid_.resize(grown);
            wide_.resize(grown * 2 * wide_words_);
        }
        uint64_t* hap = &wide_[size_t(haplotype - 1) * 2 * wide_words_];
        wide_valid_[haplotype - 1] = wide_encode(alleles, wide_length_, hap, hap + wide_words_);
    }
public:
#endif
    CandidateIndex(int length, const int8_t* library, int stride, int haplotypes)
        : width_(std::min(length, 64)), contiguous_(length <= 64), entries_(haplotypes) {
        for (int k = 0; k < width_; ++k)
            positions_[k] = width_ > 1 ? (int)((int64_t)k * (length - 1) / (width_ - 1)) : 0;
#if FINDHAP_POP_ENCODE >= 2
        length_ = length;
#endif
#if (FINDHAP_POP_COMPRESS_SIGNATURE || FINDHAP_POP_X86_PEXT) && FINDHAP_POP_FULL && !FINDHAP_POP_COMPLETE
        if (length > 64 && length <= 128) {
            uint64_t low = 0, high = 0;
            for (int k = 0; k < width_; ++k) {
                if (positions_[k] < 64) {
                    low |= uint64_t(1) << positions_[k];
                    ++low_selected_;
                } else {
                    high |= uint64_t(1) << (positions_[k] - 64);
                }
            }
#if FINDHAP_POP_COMPRESS_SIGNATURE
            selectors_[0].prepare(low); selectors_[1].prepare(high);
#endif
#if FINDHAP_POP_X86_PEXT
            selected_masks_[0] = low; selected_masks_[1] = high;
#endif
        }
#endif
#if FINDHAP_POP_ENCODE == 2
        for (int k=0; k<width_; ++k) {
            gather_positions_[k] = std::min(positions_[k], std::max(0,length-4));
            gather_shifts_[k] = (positions_[k]-gather_positions_[k])*8;
        }
#endif
#if FINDHAP_POP_ENCODE == 3 || FINDHAP_POP_ENCODE == 5
        for (int k=0; k<width_; ++k) byte_positions_[k] = uint8_t(positions_[k]);
#endif
#if FINDHAP_POP_FULL && FINDHAP_POP_COMPACT
        if (length <= 128) valid_.resize(haplotypes);
        if (length > 64 && length <= 128) full_.resize(haplotypes);
#endif
#if FINDHAP_MOD_WIDE
        if (length > 64) {
            wide_length_ = length; wide_words_ = (length + 63) / 64;
            wide_valid_.resize(haplotypes);
            wide_.resize(size_t(haplotypes) * 2 * wide_words_);
        }
#endif
        for (int h = 0; h < haplotypes; ++h)
            refresh(h+1, library + (size_t)h * stride);
    }

    Signature encode(const int8_t* alleles) const {
        Signature result;
        int k = 0;
#if FINDHAP_POP_FULL && (FINDHAP_POP_ENCODE == 4 || FINDHAP_POP_ENCODE == 5) && defined(__ARM_NEON)
        // Exact full planes extend the existing full-count/fill path to NEON.
        // Invalid signed bytes keep the original full scan authoritative.
        if (length_ <= 128) {
            result.full_valid = true;
            int q = 0;
#if FINDHAP_POP_FUSED_VALIDITY && !FINDHAP_POP_COMPLETE
            uint8x16_t invalid_seen = vdupq_n_u8(0);
#endif
            for (; q + 16 <= length_; q += 16) {
                const auto values = vld1q_s8(alleles + q);
                const auto zero = findhap_neon::lane_mask(vceqq_s8(values, vdupq_n_s8(0)));
                const auto two = findhap_neon::lane_mask(vceqq_s8(values, vdupq_n_s8(2)));
#if FINDHAP_POP_FUSED_VALIDITY && !FINDHAP_POP_COMPLETE
                // Reinterpreting signed invalid bytes as unsigned also rejects
                // negative alleles. Reduce once after all full vector loads.
                invalid_seen = vorrq_u8(invalid_seen, vcgtq_u8(
                    vreinterpretq_u8_s8(values), vdupq_n_u8(2)));
#else
                const auto invalid = findhap_neon::lane_mask(vorrq_u8(
                    vcltq_s8(values, vdupq_n_s8(0)), vcgtq_s8(values, vdupq_n_s8(2))));
#endif
                result.full_zero[q / 64] |= uint64_t(zero) << (q % 64);
                result.full_two[q / 64] |= uint64_t(two) << (q % 64);
#if !FINDHAP_POP_FUSED_VALIDITY || FINDHAP_POP_COMPLETE
                result.full_valid = result.full_valid && !invalid;
#endif
#if FINDHAP_POP_COMPLETE
                if (contiguous_) result.invalid |= uint64_t(invalid) << q;
#endif
            }
#if FINDHAP_POP_FUSED_VALIDITY && !FINDHAP_POP_COMPLETE
            result.full_valid = vmaxvq_u8(invalid_seen) == 0;
#endif
            for (; q < length_; ++q) {
                const int value = alleles[q];
                result.full_zero[q / 64] |= uint64_t(value == 0) << (q % 64);
                result.full_two[q / 64] |= uint64_t(value == 2) << (q % 64);
                result.full_valid = result.full_valid && value >= 0 && value <= 2;
#if FINDHAP_POP_COMPLETE
                if (contiguous_) result.invalid |= uint64_t(value < 0 || value > 2) << q;
#endif
            }
            if (contiguous_) {
                result.zero = result.full_zero[0]; result.two = result.full_two[0];
                return result;
            }
#if FINDHAP_POP_COMPRESS_SIGNATURE && !FINDHAP_POP_COMPLETE
            // The exact planes already contain all original sampled alleles.
            // Reuse them instead of loading and gathering those bytes again.
            result.zero = selectors_[0].extract(result.full_zero[0]) |
                (selectors_[1].extract(result.full_zero[1]) << low_selected_);
            result.two = selectors_[0].extract(result.full_two[0]) |
                (selectors_[1].extract(result.full_two[1]) << low_selected_);
            return result;
#endif
        }
#endif
#if FINDHAP_POP_ENCODE == 5 && defined(__ARM_NEON)
        if (!contiguous_ && length_ <= 128) {
            // Two 64-byte table lookups gather exactly the original sampled
            // positions. Copy only the valid partial tail before vector loads.
            alignas(16) uint8_t tail[64] = {};
            std::memcpy(tail, alleles + 64, size_t(length_ - 64));
            const auto* bytes=reinterpret_cast<const uint8_t*>(alleles);
            const uint8x16x4_t low={{vld1q_u8(bytes),vld1q_u8(bytes+16),vld1q_u8(bytes+32),vld1q_u8(bytes+48)}};
            const uint8x16x4_t high={{vld1q_u8(tail),vld1q_u8(tail+16),vld1q_u8(tail+32),vld1q_u8(tail+48)}};
            for(;k+16<=width_;k+=16) {
                const auto positions=vld1q_u8(byte_positions_.data()+k);
                const auto values=vreinterpretq_s8_u8(vorrq_u8(vqtbl4q_u8(low,positions),
                    vqtbl4q_u8(high,vsubq_u8(positions,vdupq_n_u8(64)))));
                result.zero|=uint64_t(findhap_neon::lane_mask(vceqq_s8(values,vdupq_n_s8(0))))<<k;
                result.two|=uint64_t(findhap_neon::lane_mask(vceqq_s8(values,vdupq_n_s8(2))))<<k;
#if FINDHAP_POP_COMPLETE
                result.invalid|=uint64_t(findhap_neon::lane_mask(vorrq_u8(vcltq_s8(values,vdupq_n_s8(0)),
                    vcgtq_s8(values,vdupq_n_s8(2)))))<<k;
#endif
            }
        }
#endif
#if (FINDHAP_POP_ENCODE == 4 || FINDHAP_POP_ENCODE == 5) && defined(__ARM_NEON)
        if (contiguous_) {
            for (; k+16<=width_; k+=16) {
                const auto values = vld1q_s8(alleles+k);
                result.zero |= uint64_t(findhap_neon::lane_mask(vceqq_s8(values,vdupq_n_s8(0)))) << k;
                result.two |= uint64_t(findhap_neon::lane_mask(vceqq_s8(values,vdupq_n_s8(2)))) << k;
#if FINDHAP_POP_COMPLETE
                result.invalid |= uint64_t(findhap_neon::lane_mask(vorrq_u8(
                    vcltq_s8(values,vdupq_n_s8(0)),vcgtq_s8(values,vdupq_n_s8(2))))) << k;
#endif
            }
        }
#endif
#if FINDHAP_POP_ENCODE == 3 && defined(__AVX512VBMI__) && defined(__AVX512BW__)
        if (length_ <= 128) {
            const uint64_t first_mask = length_ >= 64 ? UINT64_MAX : (uint64_t(1)<<length_)-1;
            auto values = _mm512_maskz_loadu_epi8(first_mask, alleles);
#if FINDHAP_POP_FULL
            result.full_zero[0] = uint64_t(_mm512_cmpeq_epi8_mask(values,_mm512_setzero_si512())) & first_mask;
            result.full_two[0] = uint64_t(_mm512_cmpeq_epi8_mask(values,_mm512_set1_epi8(2))) & first_mask;
#if FINDHAP_POP_FUSED_VALIDITY
            result.full_valid = !_mm512_cmp_epu8_mask(values,_mm512_set1_epi8(2),_MM_CMPINT_GT);
#else
            result.full_valid = !(_mm512_cmpgt_epi8_mask(values,_mm512_set1_epi8(2)) |
                                  _mm512_cmpgt_epi8_mask(_mm512_setzero_si512(),values));
#endif
#endif
            if (length_ > 64) {
                const uint64_t second_mask = length_ == 128 ? UINT64_MAX : (uint64_t(1)<<(length_-64))-1;
                const auto second = _mm512_maskz_loadu_epi8(second_mask, alleles+64);
#if FINDHAP_POP_FULL
                result.full_zero[1] = uint64_t(_mm512_cmpeq_epi8_mask(second,_mm512_setzero_si512())) & second_mask;
                result.full_two[1] = uint64_t(_mm512_cmpeq_epi8_mask(second,_mm512_set1_epi8(2))) & second_mask;
#if FINDHAP_POP_FUSED_VALIDITY
                result.full_valid = result.full_valid && !_mm512_cmp_epu8_mask(second,_mm512_set1_epi8(2),_MM_CMPINT_GT);
#else
                result.full_valid = result.full_valid && !(_mm512_cmpgt_epi8_mask(second,_mm512_set1_epi8(2)) |
                                  _mm512_cmpgt_epi8_mask(_mm512_setzero_si512(),second));
#endif
#endif
#if FINDHAP_POP_FULL && !FINDHAP_POP_COMPLETE && (FINDHAP_POP_X86_PEXT || FINDHAP_POP_COMPRESS_SIGNATURE)
                // Both paths select exactly the existing sampled positions
                // from full bitplanes, preserving their increasing bit order.
#if FINDHAP_POP_X86_PEXT
                result.zero = _pext_u64(result.full_zero[0],selected_masks_[0]) |
                    (_pext_u64(result.full_zero[1],selected_masks_[1]) << low_selected_);
                result.two = _pext_u64(result.full_two[0],selected_masks_[0]) |
                    (_pext_u64(result.full_two[1],selected_masks_[1]) << low_selected_);
#else
                result.zero = selectors_[0].extract(result.full_zero[0]) |
                    (selectors_[1].extract(result.full_zero[1]) << low_selected_);
                result.two = selectors_[0].extract(result.full_two[0]) |
                    (selectors_[1].extract(result.full_two[1]) << low_selected_);
#endif
                return result;
#endif
                values = _mm512_permutex2var_epi8(values, _mm512_loadu_si512(byte_positions_.data()), second);
            }
            result.zero = uint64_t(_mm512_cmpeq_epi8_mask(values,_mm512_setzero_si512())) & first_mask;
            result.two = uint64_t(_mm512_cmpeq_epi8_mask(values,_mm512_set1_epi8(2))) & first_mask;
#if FINDHAP_POP_COMPLETE
            result.invalid = uint64_t(_mm512_cmpgt_epi8_mask(values,_mm512_set1_epi8(2)) |
                                      _mm512_cmpgt_epi8_mask(_mm512_setzero_si512(),values)) & first_mask;
#endif
            return result;
        }
#endif
#if FINDHAP_POP_ENCODE == 2 && defined(__AVX2__)
        if (!contiguous_) {
            for (; k<64; k+=8) {
                // Gather dwords entirely inside the segment, then extract the
                // requested byte; even the final sampled locus cannot over-read.
                const auto offsets = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(gather_positions_.data()+k));
                const auto shifts = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(gather_shifts_.data()+k));
                const auto packed = _mm256_i32gather_epi32(reinterpret_cast<const int*>(alleles),offsets,1);
                const auto values = _mm256_and_si256(_mm256_srlv_epi32(packed,shifts),_mm256_set1_epi32(255));
                result.zero |= uint64_t(uint32_t(_mm256_movemask_ps(_mm256_castsi256_ps(_mm256_cmpeq_epi32(values,_mm256_setzero_si256())))))<<k;
                result.two |= uint64_t(uint32_t(_mm256_movemask_ps(_mm256_castsi256_ps(_mm256_cmpeq_epi32(values,_mm256_set1_epi32(2))))))<<k;
#if FINDHAP_POP_COMPLETE
                result.invalid |= uint64_t(uint32_t(_mm256_movemask_ps(_mm256_castsi256_ps(_mm256_cmpgt_epi32(values,_mm256_set1_epi32(2))))))<<k;
#endif
            }
            return result;
        }
#endif
#if FINDHAP_POP_ENCODE && defined(__AVX2__)
        // Loads never cross the segment boundary. Longer sampled signatures
        // retain the original evenly spaced positions and scalar encoding.
        if (contiguous_) {
            const auto zero = _mm256_setzero_si256();
            const auto two = _mm256_set1_epi8(2);
            for (; k + 32 <= width_; k += 32) {
                const auto values = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(alleles + k));
                result.zero |= uint64_t(uint32_t(_mm256_movemask_epi8(_mm256_cmpeq_epi8(values, zero)))) << k;
                result.two |= uint64_t(uint32_t(_mm256_movemask_epi8(_mm256_cmpeq_epi8(values, two)))) << k;
#if FINDHAP_POP_COMPLETE
                const auto valid = _mm256_or_si256(_mm256_cmpeq_epi8(values, zero),
                    _mm256_or_si256(_mm256_cmpeq_epi8(values, two), _mm256_cmpeq_epi8(values, _mm256_set1_epi8(1))));
                result.invalid |= uint64_t(uint32_t(~_mm256_movemask_epi8(valid))) << k;
#endif
            }
            if (k + 16 <= width_) {
                const auto values = _mm_loadu_si128(reinterpret_cast<const __m128i*>(alleles + k));
                result.zero |= uint64_t(uint32_t(_mm_movemask_epi8(_mm_cmpeq_epi8(values, _mm_setzero_si128())))) << k;
                result.two |= uint64_t(uint32_t(_mm_movemask_epi8(_mm_cmpeq_epi8(values, _mm_set1_epi8(2))))) << k;
#if FINDHAP_POP_COMPLETE
                const auto valid = _mm_or_si128(_mm_cmpeq_epi8(values, _mm_setzero_si128()),
                    _mm_or_si128(_mm_cmpeq_epi8(values, _mm_set1_epi8(2)), _mm_cmpeq_epi8(values, _mm_set1_epi8(1))));
                result.invalid |= uint64_t(uint32_t(~_mm_movemask_epi8(valid)) & 65535u) << k;
#endif
                k += 16;
            }
        }
#endif
        for (; k < width_; ++k) {
            const int value = alleles[positions_[k]];
            result.zero |= uint64_t(value == 0) << k;
            result.two |= uint64_t(value == 2) << k;
#if FINDHAP_POP_COMPLETE
            result.invalid |= uint64_t(value < 0 || value > 2) << k;
#endif
        }
        return result;
    }

    uint64_t conflicts(const Signature& query, int haplotype) const {
        const auto& candidate = entries_[haplotype - 1];
        return (query.zero & candidate.two) | (query.two & candidate.zero);
    }

#if FINDHAP_POP_COMPLETE
    // A complete signature is an exact count only for the 0/1/2 domain.
    // Unusual bytes and sampled (>64-locus) signatures keep the old full scan.
    bool complete(Signature query, int haplotype) const {
        return contiguous_ && !(query.invalid | entries_[haplotype - 1].invalid);
    }
#endif

#if FINDHAP_POP_FULL
    bool full_count(const Signature& query, int haplotype, int& count) const {
#if FINDHAP_POP_COMPACT
        if (!query.full_valid || !valid_[haplotype-1]) return false;
        if (contiguous_) {
            const auto& hap=entries_[haplotype-1];
            count=__builtin_popcountll((query.zero&hap.two)|(query.two&hap.zero));
        } else {
            const auto& hap=full_[haplotype-1];
            count=__builtin_popcountll((query.full_zero[0]&hap.two[0])|(query.full_two[0]&hap.zero[0])) +
                  __builtin_popcountll((query.full_zero[1]&hap.two[1])|(query.full_two[1]&hap.zero[1]));
        }
#else
        const auto& hap = entries_[haplotype-1];
        if (!query.full_valid || !hap.full_valid) return false;
        count = __builtin_popcountll((query.full_zero[0]&hap.full_two[0])|(query.full_two[0]&hap.full_zero[0])) +
                __builtin_popcountll((query.full_zero[1]&hap.full_two[1])|(query.full_two[1]&hap.full_zero[1]));
#endif
        return true;
    }
    int full_fill(const Signature& query, int haplotype, int* positions) const {
        int count=0;
        for (int word=0;word<(contiguous_ ? 1 : 2);++word) {
#if FINDHAP_POP_COMPACT
            const uint64_t known=contiguous_ ? entries_[haplotype-1].zero|entries_[haplotype-1].two :
                full_[haplotype-1].zero[word]|full_[haplotype-1].two[word];
#else
            const auto& hap = entries_[haplotype-1];
            const uint64_t known=hap.full_zero[word]|hap.full_two[word];
#endif
            uint64_t bits=~known & (query.full_zero[word]|query.full_two[word]);
            while(bits) {positions[count++]=word*64+__builtin_ctzll(bits);bits&=bits-1;}
        }
        return count;
    }
#endif

    void refresh(int haplotype, const int8_t* alleles) {
#if FINDHAP_USE_ORDERED && defined(FINDHAP_LOW_MEMORY)
        const uint64_t old_zero = size_t(haplotype) <= entries_.size() ? entries_[haplotype-1].zero : 0;
        const uint64_t old_two = size_t(haplotype) <= entries_.size() ? entries_[haplotype-1].two : 0;
#endif
        if ((size_t)haplotype > entries_.size()) entries_.resize(haplotype);
#if FINDHAP_POP_FULL && FINDHAP_POP_COMPACT
        const auto signature=encode(alleles);
        auto& stored=entries_[haplotype-1];stored.zero=signature.zero;stored.two=signature.two;
#if FINDHAP_POP_COMPLETE
        stored.invalid=signature.invalid;
#endif
        if (length_ <= 128) {
            if (valid_.size()<size_t(haplotype)) valid_.resize(haplotype);
            valid_[haplotype-1]=signature.full_valid;
            if (!contiguous_) {
                if (full_.size()<size_t(haplotype)) full_.resize(haplotype);
                auto& value=full_[haplotype-1];
                for(int k=0;k<2;++k) {value.zero[k]=signature.full_zero[k];value.two[k]=signature.full_two[k];}
            }
        }
#else
        entries_[haplotype - 1] = encode(alleles);
#endif
#if FINDHAP_MOD_WIDE
        wide_refresh(haplotype, alleles);
#endif
#if FINDHAP_USE_ORDERED
        if (ordered_) {
#ifdef FINDHAP_LOW_MEMORY
            ordered_->refresh(haplotype, old_zero, old_two);
#else
            const auto value = entries_[haplotype - 1];
            ordered_->refresh(haplotype, value.zero, value.two);
#endif
        }
#endif
    }

    Signature entry(int haplotype) const {
#if FINDHAP_POP_FULL && FINDHAP_POP_COMPACT
        Signature result;result.zero=entries_[haplotype-1].zero;result.two=entries_[haplotype-1].two;
#if FINDHAP_POP_COMPLETE
        result.invalid=entries_[haplotype-1].invalid;
#endif
        if (length_<=128) {
            result.full_valid=valid_[haplotype-1];
            if(contiguous_) {result.full_zero[0]=result.zero;result.full_two[0]=result.two;}
            else for(int k=0;k<2;++k) {result.full_zero[k]=full_[haplotype-1].zero[k];result.full_two[k]=full_[haplotype-1].two[k];}
        }
        return result;
#else
        return entries_[haplotype - 1];
#endif
    }
#if FINDHAP_USE_ORDERED
    ActiveOrderedLookup* ordered(const int* order, const int* frequencies, int count,
                                    bool track_frequencies = true) {
        // Small libraries fit the compact signature scan well; amortise the
        // larger ordered bitmap and frequency tree only over longer searches.
        if (count < FINDHAP_ORDERED_MIN_HAPS) return nullptr;
        if (ordered_disabled_) return nullptr;
#if defined(FINDHAP_LOW_MEMORY) && FINDHAP_LM_ORDERED_BYTES > 0
        if (ActiveOrderedLookup::storage_bound(width_, count) > FINDHAP_LM_ORDERED_BYTES) {
            ordered_disabled_ = true;
            ordered_.reset();
            return nullptr;
        }
#endif
        if (!ordered_) ordered_ = std::make_unique<ActiveOrderedLookup>(width_);
        if (!ordered_->prepare(order, frequencies, count, entries_, track_frequencies)) {
            ordered_disabled_ = true;
            ordered_.reset();
        }
        return ordered_.get();
    }
    void frequency_changed(int haplotype) {
        if (ordered_) ordered_->frequency_changed(haplotype);
    }
    void changed_positions(int first, int second) {
        if (ordered_) ordered_->changed_positions(first, second, entries_);
    }
#endif
};

#ifdef FINDHAP_ENGINE_NAMESPACE
} // namespace FINDHAP_ENGINE_NAMESPACE
#endif
