#pragma once

#include "low_memory_plus_storage.hpp"
#include <memory>
#include <vector>

// Immutable original genotype records. Disk bytes use the same lossless
// four-state/raw-row representation as GenotypeCache. Only a prepared batch
// is resident; row() performs no I/O and is safe for concurrent readers.
class ExternalGenotypeCache {
    struct SelectedRow { size_t record, offset; };
    size_t records_, capacity_, budget_, count_ = 0, used_ = 0;
    std::unique_ptr<size_t[]> offsets_;
    std::unique_ptr<uint8_t[]> packed_, buffer_;
    low_memory_plus::BackingFile file_;
    size_t buffer_capacity_ = 0, append_bytes_ = 0, append_start_ = 0;
    size_t first_ = 0, end_ = 0;
    bool sealed_ = false, gathered_ = false;
    std::vector<SelectedRow> selected_;
    uint64_t read_bytes_ = 0, write_bytes_ = 0;

    void reserve(size_t bytes) {
        if (bytes > buffer_capacity_) {
            buffer_.reset();
            buffer_capacity_ = 0;
            buffer_.reset(new uint8_t[bytes]);
            buffer_capacity_ = bytes;
        }
    }

    void flush_append() {
        if (!append_bytes_) return;
        file_.transfer(true, buffer_.get(), append_bytes_, append_start_);
        write_bytes_ += append_bytes_;
        file_.discard(true, append_start_, append_bytes_);
        append_start_ += append_bytes_;
        append_bytes_ = 0;
    }

    void read(size_t source, size_t bytes, size_t destination) {
        if (!bytes) return;
        file_.transfer(false, buffer_.get() + destination, bytes, source);
        read_bytes_ += bytes;
        file_.discard(false, source, bytes);
    }

    void require_sealed() const {
        if (!sealed_) throw std::logic_error("genotype cache has not been sealed");
    }

public:
    static constexpr size_t absent = std::numeric_limits<size_t>::max();

    ExternalGenotypeCache(size_t records, size_t bytes, size_t budget = 32 * 1024 * 1024)
        : records_(records), capacity_(bytes), budget_(std::max(size_t(1), budget)),
          offsets_(new size_t[records + 1]), packed_(new uint8_t[records]), file_(bytes) {
        offsets_[0] = 0;
        reserve(std::min(bytes, budget_));
    }

    void append(const int8_t* row, size_t length) {
        bool packed = true;
        for (size_t l = 0; l < length; ++l)
            packed &= row[l] == 0 || row[l] == 1 || row[l] == 2 || row[l] == 5;
        const size_t bytes = packed ? length / 4 + (length % 4 != 0) : length;
        if (sealed_ || count_ >= records_ || bytes > capacity_ - used_)
            throw std::length_error("genotypes.txt changed while loading the cache");
        if (bytes > buffer_capacity_ - append_bytes_) flush_append();
        // As with the existing output buffers, a single unusually wide record
        // may exceed the budget. It never requires the whole dataset in RAM.
        reserve(bytes);
        if (length) {
            if (packed) low_memory_plus::pack(buffer_.get() + append_bytes_, row, length);
            else std::memcpy(buffer_.get() + append_bytes_, row, length);
        }
        packed_[count_++] = packed;
        used_ += bytes;
        offsets_[count_] = used_;
        append_bytes_ += bytes;
    }

    bool complete() const { return count_ == records_ && used_ == capacity_; }
    size_t bytes() const { return capacity_; }
    size_t resident_bytes() const { return buffer_capacity_; }
    uint64_t read_bytes() const { return read_bytes_; }
    uint64_t write_bytes() const { return write_bytes_; }

    void finish_append() {
        if (!complete()) throw std::length_error("incomplete original-genotype cache");
        flush_append();
        sealed_ = true;
        release_batch();
    }

    // Returns a positive prefix of the requested records, bounded by payload
    // bytes (except one oversized row). Expansion retains original row order.
    size_t prepare_sequential(size_t first, size_t requested) {
        require_sealed();
        if (first > records_ || requested > records_ - first)
            throw std::out_of_range("genotype input batch exceeds record count");
        first_ = first;
        end_ = first;
        gathered_ = false;
        selected_.clear();
        while (end_ < first + requested) {
            const size_t bytes = offsets_[end_ + 1] - offsets_[first];
            if (bytes > budget_ && end_ > first) break;
            ++end_;
            if (bytes > budget_) break;
        }
        const size_t bytes = offsets_[end_] - offsets_[first];
        reserve(bytes);
        read(offsets_[first], bytes, 0);
        file_.prefetch(offsets_[end_], std::min(budget_, capacity_ - offsets_[end_]));
        return end_ - first_;
    }

    // Output visits animals in ID order, which need not match input order.
    // Select a bounded output prefix, sort only its disk reads, and coalesce
    // consecutive records. Formatting still follows the original animal order.
    size_t prepare_gather(const size_t* records, size_t requested) {
        require_sealed();
        selected_.clear();
        gathered_ = true;
        size_t count = 0, bytes = 0;
        for (; count < requested; ++count) {
            const size_t record = records[count];
            if (record == absent) continue;
            if (record >= records_) throw std::out_of_range("invalid genotype output record");
            const size_t length = offsets_[record + 1] - offsets_[record];
            if (count && (bytes > budget_ || length > budget_ - bytes)) break;
            selected_.push_back({record, 0});
            bytes += length;
            if (bytes > budget_) { ++count; break; }
        }
        std::sort(selected_.begin(), selected_.end(), [](auto a, auto b) { return a.record < b.record; });
        selected_.erase(std::unique(selected_.begin(), selected_.end(),
            [](auto a, auto b) { return a.record == b.record; }), selected_.end());
        bytes = 0;
        for (auto& row : selected_) {
            row.offset = bytes;
            bytes += offsets_[row.record + 1] - offsets_[row.record];
        }
        reserve(bytes);
        for (size_t i = 0; i < selected_.size();) {
            size_t end = i + 1;
            while (end < selected_.size() && selected_[end].record == selected_[end - 1].record + 1)
                ++end;
            const size_t source = offsets_[selected_[i].record];
            read(source, offsets_[selected_[end - 1].record + 1] - source, selected_[i].offset);
            i = end;
        }
        return count;
    }

    const int8_t* row(size_t record, size_t length, int8_t* scratch) const {
        if (!length) return scratch;
        size_t offset;
        if (gathered_) {
            const auto found = std::lower_bound(selected_.begin(), selected_.end(), record,
                [](auto row, size_t key) { return row.record < key; });
            if (found == selected_.end() || found->record != record)
                throw std::out_of_range("genotype output record was not prepared");
            offset = found->offset;
        } else {
            if (record < first_ || record >= end_)
                throw std::out_of_range("genotype input record was not prepared");
            offset = offsets_[record] - offsets_[first_];
        }
        const size_t encoded = packed_[record] ? length / 4 + (length % 4 != 0) : length;
        if (encoded != offsets_[record + 1] - offsets_[record])
            throw std::length_error("genotype cache record length changed");
        const uint8_t* in = buffer_.get() + offset;
        if (!packed_[record]) return reinterpret_cast<const int8_t*>(in);
        low_memory_plus::unpack(scratch, in, length);
        return scratch;
    }

    void release_batch() {
        buffer_.reset();
        buffer_capacity_ = 0;
        first_ = end_ = 0;
        selected_.clear();
        if (sealed_) file_.discard(false);
    }
};
