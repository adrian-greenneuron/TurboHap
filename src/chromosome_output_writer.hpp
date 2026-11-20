#pragma once
#include "dense_output_writer.hpp"
#include <vector>

namespace turbohap_output {
struct ChromosomeDenseLayout {
    FILE* genotypes = nullptr;
    FILE* haplotypes = nullptr;
    size_t genotype_offset = 0, haplotype_offset = 0;
    size_t genotype_width = 0, haplotype_width = 0;
};

inline const char* write_chromosome_batch(DenseBatch batch,
        const std::vector<ChromosomeDenseLayout>& layout, size_t capacity) {
    for (size_t c = 0; c < layout.size(); ++c) {
        const auto& d = layout[c];
        if (!d.genotypes && !d.haplotypes) continue;
        DenseBatch part{batch.genotypes ? batch.genotypes + d.genotype_offset : nullptr,
                        batch.haplotypes ? batch.haplotypes + d.haplotype_offset : nullptr,
                        batch.usable + c * capacity, batch.count};
        if (const char* error = write_dense_batch(part, d.genotypes, d.genotype_width,
                                                d.haplotypes, d.haplotype_width)) return error;
    }
    return nullptr;
}

// Descriptors are immutable; each submitted slot owns its payload AND mask.
// The producer can fill the other slot while this writer drains the previous one.
class ChromosomeOutputWriter {
    const std::vector<ChromosomeDenseLayout>& layout_;
    size_t capacity_;
    std::mutex mutex_;
    std::condition_variable ready_;
    DenseBatch batch_{};
    bool pending_ = false, stopping_ = false;
    const char* error_ = nullptr;
    std::thread worker_;
    void run() {
        std::unique_lock<std::mutex> lock(mutex_);
        for (;;) {
            ready_.wait(lock, [&] { return pending_ || stopping_; });
            if (!pending_) return;
            const auto batch = batch_;
            lock.unlock();
            const char* error = write_chromosome_batch(batch, layout_, capacity_);
            lock.lock();
            error_ = error;
            pending_ = false;
            ready_.notify_all();
        }
    }
public:
    ChromosomeOutputWriter(const std::vector<ChromosomeDenseLayout>& layout, size_t capacity)
        : layout_(layout), capacity_(capacity), worker_([this] { run(); }) {}
    ~ChromosomeOutputWriter() {
        { std::lock_guard<std::mutex> lock(mutex_); stopping_ = true; }
        ready_.notify_one();
        worker_.join();
    }
    void submit(DenseBatch batch) {
        std::unique_lock<std::mutex> lock(mutex_);
        ready_.wait(lock, [&] { return !pending_; });
        if (error_) throw std::runtime_error(error_);
        batch_ = batch;
        pending_ = true;
        ready_.notify_one();
    }
    void finish() {
        std::unique_lock<std::mutex> lock(mutex_);
        ready_.wait(lock, [&] { return !pending_; });
        if (error_) throw std::runtime_error(error_);
    }
};
}
