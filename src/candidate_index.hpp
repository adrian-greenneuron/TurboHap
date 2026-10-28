#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>
#include "platform_policy.hpp"
#if FINDHAP_POP_ENCODE && defined(__AVX2__)
#include <immintrin.h>
#endif
#ifdef FINDHAP_HIGH_MEMORY
#include <memory>
#include "high_memory_index.hpp"
#endif

#ifdef FINDHAP_ENGINE_NAMESPACE
namespace FINDHAP_ENGINE_NAMESPACE {
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
#if FINDHAP_POP_ENCODE == 3
    std::array<uint8_t, 64> byte_positions_{};
#endif
    std::array<int, 64> positions_{};
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
#ifdef FINDHAP_HIGH_MEMORY
    std::unique_ptr<OrderedCandidateLookup> ordered_;
    bool ordered_disabled_ = false;
#endif

public:
    CandidateIndex(int length, const int8_t* library, int stride, int haplotypes)
        : width_(std::min(length, 64)), contiguous_(length <= 64), entries_(haplotypes) {
        for (int k = 0; k < width_; ++k)
            positions_[k] = width_ > 1 ? (int)((int64_t)k * (length - 1) / (width_ - 1)) : 0;
#if FINDHAP_POP_ENCODE >= 2
        length_ = length;
#endif
#if FINDHAP_POP_ENCODE == 2
        for (int k=0; k<width_; ++k) {
            gather_positions_[k] = std::min(positions_[k], std::max(0,length-4));
            gather_shifts_[k] = (positions_[k]-gather_positions_[k])*8;
        }
#endif
#if FINDHAP_POP_ENCODE == 3
        for (int k=0; k<width_; ++k) byte_positions_[k] = uint8_t(positions_[k]);
#endif
#if FINDHAP_POP_FULL && FINDHAP_POP_COMPACT
        if (length <= 128) valid_.resize(haplotypes);
        if (length > 64 && length <= 128) full_.resize(haplotypes);
#endif
        for (int h = 0; h < haplotypes; ++h)
            refresh(h+1, library + (size_t)h * stride);
    }

    Signature encode(const int8_t* alleles) const {
        Signature result;
        int k = 0;
#if FINDHAP_POP_ENCODE == 3 && defined(__AVX512VBMI__) && defined(__AVX512BW__)
        if (length_ <= 128) {
            const uint64_t first_mask = length_ >= 64 ? UINT64_MAX : (uint64_t(1)<<length_)-1;
            auto values = _mm512_maskz_loadu_epi8(first_mask, alleles);
#if FINDHAP_POP_FULL
            result.full_zero[0] = uint64_t(_mm512_cmpeq_epi8_mask(values,_mm512_setzero_si512())) & first_mask;
            result.full_two[0] = uint64_t(_mm512_cmpeq_epi8_mask(values,_mm512_set1_epi8(2))) & first_mask;
            result.full_valid = !(_mm512_cmpgt_epi8_mask(values,_mm512_set1_epi8(2)) |
                                  _mm512_cmpgt_epi8_mask(_mm512_setzero_si512(),values));
#endif
            if (length_ > 64) {
                const uint64_t second_mask = length_ == 128 ? UINT64_MAX : (uint64_t(1)<<(length_-64))-1;
                const auto second = _mm512_maskz_loadu_epi8(second_mask, alleles+64);
#if FINDHAP_POP_FULL
                result.full_zero[1] = uint64_t(_mm512_cmpeq_epi8_mask(second,_mm512_setzero_si512())) & second_mask;
                result.full_two[1] = uint64_t(_mm512_cmpeq_epi8_mask(second,_mm512_set1_epi8(2))) & second_mask;
                result.full_valid = result.full_valid && !(_mm512_cmpgt_epi8_mask(second,_mm512_set1_epi8(2)) |
                                  _mm512_cmpgt_epi8_mask(_mm512_setzero_si512(),second));
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
#ifdef FINDHAP_HIGH_MEMORY
        if (ordered_) {
            const auto value = entries_[haplotype - 1];
            ordered_->refresh(haplotype, value.zero, value.two);
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
#ifdef FINDHAP_HIGH_MEMORY
    OrderedCandidateLookup* ordered(const int* order, const int* frequencies, int count,
                                    bool track_frequencies = true) {
        // Small libraries fit the compact signature scan well; amortise the
        // larger ordered bitmap and frequency tree only over longer searches.
        if (count < 1024) return nullptr;
        if (ordered_disabled_) return nullptr;
        if (!ordered_) ordered_ = std::make_unique<OrderedCandidateLookup>(width_);
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
