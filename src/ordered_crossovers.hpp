#pragma once
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <limits>
#include <memory>
#include <stdexcept>
#include "platform_policy.hpp"
#include "phase_buffer.hpp"

namespace findhap {
// Chromosome owners fill disjoint slots. The serial writer consumes them in
// the original animal/segment/record order after the worksharing barrier.
class OrderedCrossoverBatch {
    static constexpr size_t record_bytes = 80, records_per_slot = 10;
    static constexpr size_t slot_bytes = record_bytes * records_per_slot;
    size_t segments_, rows_;
    std::unique_ptr<int[]> counts_;
    PhaseBuffer<char> records_;
public:
    static size_t capacity(size_t segments, size_t budget = FINDHAP_CROSSOVER_BATCH_BYTES) {
        if (!segments || segments > std::numeric_limits<size_t>::max() / (slot_bytes + sizeof(int))) return 0;
        return std::min(size_t(FINDHAP_CROSSOVER_BATCH_ROWS), budget / (segments * (slot_bytes + sizeof(int))));
    }
    explicit OrderedCrossoverBatch(size_t segments, size_t budget = FINDHAP_CROSSOVER_BATCH_BYTES)
        : segments_(segments), rows_(capacity(segments, budget)) {
        if (!rows_) throw std::length_error("crossover batch exceeds bounded budget");
        counts_.reset(new int[rows_ * segments_]);
        records_ = make_phase_buffer<char>(rows_ * segments_ * slot_bytes);
    }
    size_t rows() const { return rows_; }
    size_t resident_bound() const { return rows_ * segments_ * (slot_bytes + sizeof(int)); }
    void capture(size_t row, size_t segment, int count, const char* source) {
        if (row >= rows_ || segment >= segments_ || count < 0 || count > int(records_per_slot))
            throw std::out_of_range("invalid crossover batch slot/count");
        const size_t slot = segment * rows_ + row;
        counts_[slot] = count;
        if (count) std::memcpy(records_.get() + slot * slot_bytes, source, size_t(count) * record_bytes);
    }
    bool emit(FILE* destination, size_t rows) const {
        if (rows > rows_) throw std::out_of_range("invalid crossover batch row count");
#if FINDHAP_CROSSOVER_BATCH_WRITE
        char buffer[65536];size_t used=0;
        auto flush=[&](){
            if(used && std::fwrite(buffer,1,used,destination)!=used)return false;
            used=0;return true;
        };
#endif
        for (size_t row = 0; row < rows; ++row)
            for (size_t segment = 0; segment < segments_; ++segment) {
                const size_t slot = segment * rows_ + row;
                for (int record = 0; record < counts_[slot]; ++record) {
                    const char* text=records_.get() + slot * slot_bytes + size_t(record) * record_bytes;
#if FINDHAP_CROSSOVER_BATCH_WRITE
                    const size_t bytes=std::strlen(text);
                    if(used+bytes+1>sizeof(buffer) && !flush())return false;
                    std::memcpy(buffer+used,text,bytes);used+=bytes;buffer[used++]='\n';
#else
                    if (std::fprintf(destination, "%s\n", text) < 0)return false;
#endif
                }
            }
#if FINDHAP_CROSSOVER_BATCH_WRITE
        return flush();
#else
        return true;
#endif
    }
};
}
