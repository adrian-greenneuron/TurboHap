#pragma once

#include "genotype_workspace.hpp"
#include "low_memory_plus_storage.hpp"

// Disk-backed working genotypes for TurboHap LM. Population workers retain
// bounded animal blocks while preserving each segment's matching state
// and animal traversal order.
class BoundedGenotypeWorkspace {
    size_t records_, loci_, population_budget_;
    std::vector<size_t> starts_, lengths_, strides_, offsets_;
    std::unique_ptr<low_memory_plus::BackingFile> file_;
    std::unique_ptr<ChromosomeWorkspace> dense_;
    std::unique_ptr<uint8_t[]> input_;
    size_t input_capacity_ = 0, input_batch_ = 0, input_first_ = 0, input_count_ = 0;
    uint64_t read_bytes_ = 0, write_bytes_ = 0;

public:
    BoundedGenotypeWorkspace(int records, int loci, const int* starts, const int* lengths,
                            int segments, const int* chromosomes,
                            size_t staging_budget = 64 * 1024 * 1024,
                            size_t population_budget = 128 * 1024 * 1024)
        : records_(records), loci_(loci), population_budget_(std::max(size_t(1), population_budget)),
          starts_(segments), lengths_(segments), strides_(segments), offsets_(segments + 1, 0) {
        size_t next = 0, row_bytes = 0;
        bool partitioned = true;
        for (int s = 0; s < segments; ++s) {
            if (starts[s + 1] < 1 || lengths[s + 1] < 0 ||
                size_t(starts[s + 1] - 1) > loci_ ||
                size_t(lengths[s + 1]) > loci_ - size_t(starts[s + 1] - 1))
                throw std::length_error("genotype segment is outside the genome");
            starts_[s] = starts[s + 1] - 1;
            lengths_[s] = lengths[s + 1];
            if (starts_[s] != next) partitioned = false;
            next = starts_[s] + lengths_[s];
            strides_[s] = (lengths_[s] + 3) / 4;
            const size_t bytes = low_memory_plus::product(records_, strides_[s]);
            if (bytes > std::numeric_limits<size_t>::max() - offsets_[s])
                throw std::length_error("genotype workspace is too large");
            offsets_[s + 1] = offsets_[s] + bytes;
            row_bytes += strides_[s];
        }
        if (next != loci_) partitioned = false;
        if (!partitioned) {
            // Preserve the existing shared-locus representation for overlapping
            // priors. This path is explicitly outside the bounded-cache claim.
            dense_ = std::make_unique<ChromosomeWorkspace>(records, loci, starts, lengths,
                                                         segments, chromosomes, staging_budget);
            return;
        }
        input_batch_ = std::min(records_, std::max(size_t(1), staging_budget /
                                                  std::max(size_t(1), row_bytes)));
        file_ = std::make_unique<low_memory_plus::BackingFile>(offsets_.back());
    }

    size_t input_batch_size() const { return dense_ ? dense_->input_batch_size() : input_batch_; }

    void begin_input(size_t first, size_t count) {
        if (dense_) { dense_->begin_input(first, count); return; }
        if (first > records_ || count > records_ - first)
            throw std::out_of_range("working-genotype input batch exceeds record count");
        input_first_ = first;
        input_count_ = count;
        size_t bytes = 0;
        for (auto stride : strides_) bytes += low_memory_plus::product(count, stride);
        if (bytes > input_capacity_) {
            input_.reset();
            input_capacity_ = 0;
            input_.reset(new uint8_t[bytes]);
            input_capacity_ = bytes;
        }
    }

    void store_row(size_t record, const int8_t* row) {
        if (dense_) { dense_->store_row(record, row); return; }
        size_t begin = 0;
        for (size_t s = 0; s < starts_.size(); ++s) {
            if (!lengths_[s]) continue;
            low_memory_plus::pack(input_.get() + begin + (record - input_first_) * strides_[s],
                               row + starts_[s], lengths_[s]);
            begin += input_count_ * strides_[s];
        }
    }

    void end_input() {
        if (dense_) { dense_->end_input(); return; }
        size_t begin = 0;
        for (size_t s = 0; s < strides_.size(); ++s) {
            const size_t bytes = input_count_ * strides_[s];
            if (!bytes) continue;
            file_->transfer(true, input_.get() + begin, bytes, offsets_[s] + input_first_ * strides_[s]);
            write_bytes_ += bytes;
            begin += bytes;
        }
        file_->discard(true);
    }

    void finish_input() {
        if (dense_) dense_->finish_input();
        input_.reset();
        input_capacity_ = 0;
    }

    // A worker needs at least one packed animal from the widest segment, not
    // that segment's complete population. Smaller blocks preserve concurrency
    // when animal count grows. A single oversized animal remains permitted.
    size_t population_workers(size_t requested) const {
        size_t workers = std::min(std::max(size_t(1), requested),
                                  std::max(size_t(1), strides_.size()));
        if (dense_) return workers;
        const size_t stride = strides_.empty() ? 0 : *std::max_element(strides_.begin(), strides_.end());
        if (stride) workers = std::min(workers, std::max(size_t(1), population_budget_ / stride));
        return workers;
    }

    size_t population_buffer_bytes(size_t workers) const {
        if (dense_ || strides_.empty()) return 0;
        const size_t stride = *std::max_element(strides_.begin(), strides_.end());
        if (!stride) return 0;
        return std::min(low_memory_plus::product(records_, stride),
                        std::max(stride, population_budget_ / std::max(size_t(1), workers)));
    }

    class PopulationWorker {
        BoundedGenotypeWorkspace& workspace_;
        std::unique_ptr<uint8_t[]> buffer_;
        size_t capacity_, segment_ = 0, first_ = 0, count_ = 0;
        size_t dirty_first_ = 0, dirty_end_ = 0;

        void flush() {
            if (dirty_first_ < dirty_end_) {
                const size_t stride = workspace_.strides_[segment_];
                const size_t offset = workspace_.offsets_[segment_] + (first_ + dirty_first_) * stride;
                const size_t bytes = (dirty_end_ - dirty_first_) * stride;
                workspace_.file_->transfer(true, buffer_.get() + dirty_first_ * stride, bytes, offset);
                write_bytes += bytes;
                workspace_.file_->discard(true, offset, bytes);
            }
            dirty_first_ = count_;
            dirty_end_ = 0;
        }

        void load(size_t record) {
            flush();
            const size_t stride = workspace_.strides_[segment_];
            const size_t block = std::max(size_t(1), capacity_ / std::max(size_t(1), stride));
            first_ = record / block * block;
            count_ = std::min(block, workspace_.records_ - first_);
            dirty_first_ = count_;
            dirty_end_ = 0;
            const size_t offset = workspace_.offsets_[segment_] + first_ * stride;
            const size_t bytes = count_ * stride;
            workspace_.file_->transfer(false, buffer_.get(), bytes, offset);
            read_bytes += bytes;
            workspace_.file_->discard(false, offset, bytes);
            const size_t remaining = workspace_.records_ - first_ - count_;
            workspace_.file_->prefetch(offset + bytes, std::min(block, remaining) * stride);
        }

    public:
        uint64_t read_bytes = 0, write_bytes = 0;
        PopulationWorker(BoundedGenotypeWorkspace& workspace, size_t capacity)
            : workspace_(workspace), buffer_(capacity ? new uint8_t[capacity] : nullptr), capacity_(capacity) {}

        size_t resident_bytes() const { return capacity_; }

        void begin_segment(size_t segment) {
            segment_ = segment;
            first_ = count_ = dirty_first_ = dirty_end_ = 0;
        }

        int8_t* segment(size_t record, int8_t* scratch) {
            if (workspace_.dense_) return workspace_.dense_->segment(record, segment_, scratch);
            if (record >= workspace_.records_) throw std::out_of_range("invalid working-genotype record");
            if (!workspace_.lengths_[segment_]) return scratch;
            if (record < first_ || record - first_ >= count_) load(record);
            low_memory_plus::unpack(scratch, buffer_.get() + (record - first_) * workspace_.strides_[segment_],
                                 workspace_.lengths_[segment_]);
            return scratch;
        }

        void save_segment(size_t record, const int8_t* scratch) {
            if (workspace_.dense_) { workspace_.dense_->save_segment(record, segment_, scratch); return; }
            if (!workspace_.lengths_[segment_]) return;
            if (record < first_ || record - first_ >= count_)
                throw std::out_of_range("working-genotype edit is outside its prepared block");
            const size_t relative = record - first_;
            low_memory_plus::pack(buffer_.get() + relative * workspace_.strides_[segment_],
                               scratch, workspace_.lengths_[segment_]);
            dirty_first_ = std::min(dirty_first_, relative);
            dirty_end_ = std::max(dirty_end_, relative + 1);
        }

        void end_segment() {
            if (!workspace_.dense_) {
                flush();
                // Drop any read-ahead that was not consumed because later
                // animals were skipped by this population pass.
                const size_t offset = workspace_.offsets_[segment_];
                const size_t bytes = workspace_.offsets_[segment_ + 1] - offset;
                if (bytes) workspace_.file_->discard(false, offset, bytes);
            }
        }
    };

    using PopulationWorkers = std::vector<std::unique_ptr<PopulationWorker>>;
    PopulationWorkers make_population_workers(size_t count) {
        PopulationWorkers workers;
        const size_t capacity = population_buffer_bytes(count);
        workers.reserve(count);
        for (size_t i = 0; i < count; ++i)
            workers.push_back(std::make_unique<PopulationWorker>(*this, capacity));
        return workers;
    }

    void finish_population_workers(PopulationWorkers& workers) {
        for (const auto& worker : workers) {
            read_bytes_ += worker->read_bytes;
            write_bytes_ += worker->write_bytes;
        }
        workers.clear();
    }

    void finish_population() { if (dense_) dense_->finish_population(); }
    bool packed() const { return !dense_; }
    size_t bytes() const { return dense_ ? dense_->bytes() : offsets_.back(); }
    uint64_t read_bytes() const { return read_bytes_; }
    uint64_t write_bytes() const { return write_bytes_; }
};
