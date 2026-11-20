#pragma once

#include <condition_variable>
#include <cstdio>
#include <mutex>
#include <stdexcept>
#include <thread>

namespace turbohap_output {

struct DenseBatch {
    const char* genotypes;
    const char* haplotypes;
    const int* usable;
    int count;
};

inline const char* write_dense_batch(DenseBatch batch, FILE* genotypes, size_t genotype_width,
                                     FILE* haplotypes, size_t haplotype_width) {
    for (int first = 0; first < batch.count;) {
        while (first < batch.count && batch.usable[first] != 1) ++first;
        int last = first;
        while (last < batch.count && batch.usable[last] == 1) ++last;
        if (last > first) {
            if (genotypes) {
                const size_t bytes = size_t(last - first) * genotype_width;
                if (fwrite(batch.genotypes + size_t(first) * genotype_width, 1, bytes, genotypes) != bytes)
                    return "cannot write genotype output batch";
            }
            if (haplotypes) {
                const size_t bytes = size_t(last - first) * haplotype_width;
                if (fwrite(batch.haplotypes + size_t(first) * haplotype_width, 1, bytes, haplotypes) != bytes)
                    return "cannot write haplotype output batch";
            }
        }
        first = last;
    }
    return nullptr;
}

// One batch can be in flight while the OpenMP team fills a second buffer.
// submit() waits for the previous writer before allowing either slot's reuse.
class DenseOutputWriter {
    FILE* genotypes_;
    size_t genotype_width_;
    FILE* haplotypes_;
    size_t haplotype_width_;
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
            const DenseBatch batch = batch_;
            lock.unlock();
            const char* error = write_dense_batch(batch, genotypes_, genotype_width_,
                                                  haplotypes_, haplotype_width_);
            lock.lock();
            error_ = error;
            pending_ = false;
            ready_.notify_all();
        }
    }

public:
    DenseOutputWriter(FILE* genotypes, size_t genotype_width, FILE* haplotypes, size_t haplotype_width)
        : genotypes_(genotypes), genotype_width_(genotype_width),
          haplotypes_(haplotypes), haplotype_width_(haplotype_width), worker_([this] { run(); }) {}

    ~DenseOutputWriter() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stopping_ = true;
        }
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

} // namespace turbohap_output
