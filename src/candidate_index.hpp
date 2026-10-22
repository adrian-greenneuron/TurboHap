#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>
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
    };

private:
    int width_;
    std::array<int, 64> positions_{};
    std::vector<Signature> entries_;
#ifdef FINDHAP_HIGH_MEMORY
    std::unique_ptr<OrderedCandidateLookup> ordered_;
    bool ordered_disabled_ = false;
#endif

public:
    CandidateIndex(int length, const int8_t* library, int stride, int haplotypes)
        : width_(std::min(length, 64)), entries_(haplotypes) {
        for (int k = 0; k < width_; ++k)
            positions_[k] = width_ > 1 ? (int)((int64_t)k * (length - 1) / (width_ - 1)) : 0;
        for (int h = 0; h < haplotypes; ++h)
            entries_[h] = encode(library + (size_t)h * stride);
    }

    Signature encode(const int8_t* alleles) const {
        Signature result;
        for (int k = 0; k < width_; ++k) {
            result.zero |= uint64_t(alleles[positions_[k]] == 0) << k;
            result.two |= uint64_t(alleles[positions_[k]] == 2) << k;
        }
        return result;
    }

    uint64_t conflicts(Signature query, int haplotype) const {
        const Signature candidate = entries_[haplotype - 1];
        return (query.zero & candidate.two) | (query.two & candidate.zero);
    }

    void refresh(int haplotype, const int8_t* alleles) {
        if ((size_t)haplotype > entries_.size()) entries_.resize(haplotype);
        entries_[haplotype - 1] = encode(alleles);
#ifdef FINDHAP_HIGH_MEMORY
        if (ordered_) {
            const auto value = entries_[haplotype - 1];
            ordered_->refresh(haplotype, value.zero, value.two);
        }
#endif
    }

    Signature entry(int haplotype) const { return entries_[haplotype - 1]; }
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
