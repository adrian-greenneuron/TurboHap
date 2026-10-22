#pragma once

#include <array>
#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
#include <fcntl.h>
#include <unistd.h>

// Persistent population genotypes have four states: 0, 1, 2 and missing (5).
// Keep segments byte-aligned so parallel segment owners never share a packed
// byte. Each matcher expands only its current segment and saves conflict edits.
template<bool External = false>
class BasicGenotypeWorkspace {
    size_t records_, loci_;
    bool packed_ = true;
    std::vector<int> starts_, lengths_;
    std::vector<size_t> strides_, offsets_;
    std::unique_ptr<uint8_t[]> data_;
    std::vector<int> chromosomes_;
    std::vector<size_t> dirty_first_, dirty_end_;
    int backing_ = -1;
    size_t batch_first_ = 0, batch_records_ = 0, tile_first_ = 0;
    size_t input_batch_ = 0, buffer_bytes_ = 0;
    uint64_t read_bytes_ = 0, write_bytes_ = 0;

    [[noreturn]] static void io_error(const char* operation) {
        throw std::runtime_error(std::string("genotype workspace ") + operation +
                                 ": " + std::strerror(errno));
    }

    void transfer(bool write, uint8_t* bytes, size_t count, size_t offset,
                  uint64_t* worker_counter = nullptr) {
        while (count) {
            const size_t part = std::min(count, size_t(1024) * 1024 * 1024);
            const ssize_t done = write ? pwrite(backing_, bytes, part, offset)
                                       : pread(backing_, bytes, part, offset);
            if (done < 0 && errno == EINTR) continue;
            if (done <= 0) {
                if (!done) errno = EIO;
                io_error(write ? "write" : "read");
            }
            (worker_counter ? *worker_counter : write ? write_bytes_ : read_bytes_) += done;
            bytes += done;
            count -= done;
            offset += done;
        }
    }

    void release_file_cache(bool written, size_t offset = 0, size_t bytes = 0) {
        // Do not depend on accumulating a second whole-genome copy in the OS
        // page cache. Dirty pages must reach disk before DONTNEED can drop them.
        if (written && fsync(backing_) != 0) io_error("sync");
#ifdef POSIX_FADV_DONTNEED
        const int error = posix_fadvise(backing_, offset, bytes, POSIX_FADV_DONTNEED);
        if (error) { errno = error; io_error("release file cache"); }
#endif
    }

    static size_t product(size_t a, size_t b) {
        if (b && a > std::numeric_limits<size_t>::max() / b)
            throw std::length_error("genotype workspace is too large");
        return a * b;
    }

    static uint8_t code(int8_t value) {
        return value == 5 ? 3 : static_cast<uint8_t>(value);
    }

    static void pack(uint8_t* out, const int8_t* in, int length) {
        int l = 0;
        for (; l + 3 < length; l += 4)
            out[l / 4] = code(in[l]) | (code(in[l + 1]) << 2) |
                         (code(in[l + 2]) << 4) | (code(in[l + 3]) << 6);
        if (l < length) {
            const int byte = l / 4;
            uint8_t tail = 0;
            for (int shift = 0; l < length; ++l, shift += 2)
                tail |= code(in[l]) << shift;
            out[byte] = tail;
        }
    }

    static void unpack(int8_t* out, const uint8_t* in, int length) {
        static constexpr auto decoded = [] {
            std::array<std::array<int8_t, 4>, 256> table{};
            for (int byte = 0; byte < 256; ++byte)
                for (int l = 0; l < 4; ++l) {
                    const int value = (byte >> (2 * l)) & 3;
                    table[byte][l] = value == 3 ? 5 : value;
                }
            return table;
        }();
        int l = 0;
        for (; l + 3 < length; l += 4)
            std::memcpy(out + l, decoded[in[l / 4]].data(), 4);
        for (; l < length; ++l) {
            const uint8_t value = (in[l / 4] >> (2 * (l % 4))) & 3;
            out[l] = value == 3 ? 5 : value;
        }
    }

public:
    BasicGenotypeWorkspace(int records, int loci, const int* starts, const int* lengths,
                          int segments, const int* chromosomes = nullptr,
                          size_t staging_budget = 64 * 1024 * 1024)
        : records_(records), loci_(loci), starts_(segments), lengths_(segments),
          strides_(segments), offsets_(segments + 1, 0) {
        size_t next = 0;
        for (int s = 0; s < segments; ++s) {
            starts_[s] = starts[s + 1] - 1;
            lengths_[s] = lengths[s + 1];
            if (starts_[s] < 0 || lengths_[s] < 0 ||
                (size_t)starts_[s] + lengths_[s] > loci_)
                throw std::length_error("genotype segment is outside the genome");
            if ((size_t)starts_[s] != next) packed_ = false;
            next = (size_t)starts_[s] + lengths_[s];
            strides_[s] = ((size_t)lengths_[s] + 3) / 4;
            const size_t bytes = product(records_, strides_[s]);
            if (bytes > std::numeric_limits<size_t>::max() - offsets_[s])
                throw std::length_error("genotype workspace is too large");
            offsets_[s + 1] = offsets_[s] + bytes;
        }
        if (next != loci_) packed_ = false;
        // Unusual prior layouts may overlap. Preserve shared-locus mutations
        // with the original dense representation instead of duplicating them.
        if constexpr (External) {
            if (packed_) {
                if (!chromosomes) throw std::invalid_argument("missing workspace chromosome map");
                chromosomes_.assign(chromosomes, chromosomes + segments);
                dirty_first_.resize(segments);
                dirty_end_.resize(segments);
                size_t row_bytes = 0;
                for (size_t stride : strides_) row_bytes += stride;
                input_batch_ = std::min(records_, std::max(size_t(1), staging_budget / std::max(size_t(1), row_bytes)));
                if (offsets_.back() > static_cast<uint64_t>(std::numeric_limits<off_t>::max()))
                    throw std::length_error("genotype workspace file is too large");
                // Use the output filesystem, rather than an implicit /tmp
                // which could be RAM-backed. Unlink immediately, even on error.
                char name[] = ".findhap-workspace-XXXXXX";
                backing_ = mkstemp(name);
                if (backing_ < 0) io_error("create");
                if (unlink(name) != 0) {
                    const int error = errno;
                    close(backing_); backing_ = -1; errno = error; io_error("unlink");
                }
                if (ftruncate(backing_, offsets_.back()) != 0) {
                    const int error = errno;
                    close(backing_); backing_ = -1; errno = error; io_error("size");
                }
                return;
            }
        }
        data_.reset(new uint8_t[packed_ ? offsets_.back() : product(records_, loci_)]);
    }

    ~BasicGenotypeWorkspace() {
        if (backing_ >= 0) close(backing_);
    }

    size_t input_batch_size() const {
        if constexpr (External) if (packed_) return input_batch_;
        return records_;
    }

    void begin_input(size_t first, size_t count) {
        if constexpr (External) if (packed_) {
            batch_first_ = first;
            batch_records_ = count;
            size_t bytes = 0;
            for (size_t stride : strides_) bytes += product(count, stride);
            if (bytes > buffer_bytes_) {
                data_.reset();
                data_.reset(new uint8_t[bytes]);
                buffer_bytes_ = bytes;
            }
        }
    }

    void end_input() {
        if constexpr (External) if (packed_) {
            size_t begin = 0;
            for (size_t s = 0; s < strides_.size(); ++s) {
                const size_t bytes = batch_records_ * strides_[s];
                transfer(true, data_.get() + begin, bytes, offsets_[s] + batch_first_ * strides_[s]);
                begin += bytes;
            }
            release_file_cache(true);
        }
    }

    void finish_input() {
        if constexpr (External) if (packed_) {
            data_.reset();
            buffer_bytes_ = 0;
        }
    }

    // End is exclusive. Keep whole chromosomes, adding neighbours only when
    // there are too few segments for the workers. The byte budget bounds the
    // packed tile, not all process memory; one chromosome may exceed it.
    // The full pedigree/reconciliation pass still follows all population tiles.
    size_t tile_end(size_t first, size_t workers = 1,
                    size_t byte_budget = 128 * 1024 * 1024) const {
        if constexpr (External) if (packed_) {
            size_t end = first + 1;
            while (end < chromosomes_.size() && chromosomes_[end] == chromosomes_[first]) ++end;
            while (end < chromosomes_.size() && end - first < workers) {
                size_t next = end + 1;
                while (next < chromosomes_.size() && chromosomes_[next] == chromosomes_[end]) ++next;
                if (offsets_[next] - offsets_[first] > byte_budget) break;
                end = next;
            }
            return end;
        }
        return strides_.size();
    }

    void begin_tile(size_t first, size_t end) {
        if constexpr (External) if (packed_) {
            tile_first_ = first;
            const size_t bytes = offsets_[end] - offsets_[first];
            if (bytes > buffer_bytes_) {
                data_.reset();
                data_.reset(new uint8_t[bytes]);
                buffer_bytes_ = bytes;
            }
            transfer(false, data_.get(), bytes, offsets_[first]);
            release_file_cache(false, offsets_[first], bytes);
            for (size_t s = first; s < end; ++s) {
                dirty_first_[s] = records_;
                dirty_end_[s] = 0;
            }
        }
    }

    void end_tile(size_t end) {
        if constexpr (External) if (packed_) {
            bool written = false;
            for (size_t s = tile_first_; s < end; ++s) {
                if (dirty_first_[s] >= dirty_end_[s]) continue;
                const size_t offset = offsets_[s] + dirty_first_[s] * strides_[s];
                const size_t bytes = (dirty_end_[s] - dirty_first_[s]) * strides_[s];
                transfer(true, data_.get() + offset - offsets_[tile_first_], bytes, offset);
                written = true;
            }
            if (written) release_file_cache(true, offsets_[tile_first_], offsets_[end] - offsets_[tile_first_]);
        }
    }

    void finish_population() {
        if constexpr (External) if (packed_) {
            data_.reset();
            buffer_bytes_ = 0;
        }
    }

    // A population segment owns all of its records and packed bytes. Rolling
    // workers can therefore visit segments independently, with the existing
    // full population/pedigree barrier after the queue. Input staging and
    // overlapping-prior dense storage retain their original representation.
    size_t population_buffer_bytes() const {
        if constexpr (External) if (packed_ && !strides_.empty())
            return product(records_, *std::max_element(strides_.begin(), strides_.end()));
        return 0;
    }

    size_t population_workers(size_t requested, size_t budget = 128 * 1024 * 1024) const {
        size_t workers = std::min(std::max(size_t(1), requested),
                                  std::max(size_t(1), strides_.size()));
        const size_t bytes = population_buffer_bytes();
        // Account for each worker's full retained capacity, not only the size
        // of its current segment. One oversized segment always makes progress.
        if (bytes) workers = std::min(workers, std::max(size_t(1), budget / bytes));
        return workers;
    }

    class PopulationWorker {
        BasicGenotypeWorkspace& workspace_;
        std::unique_ptr<uint8_t[]> bytes_;
        size_t segment_ = 0, dirty_first_ = 0, dirty_end_ = 0;
    public:
        uint64_t read_bytes = 0, write_bytes = 0;
        explicit PopulationWorker(BasicGenotypeWorkspace& workspace) : workspace_(workspace) {
            const size_t capacity = workspace_.population_buffer_bytes();
            if (capacity) bytes_.reset(new uint8_t[capacity]);
        }
        void begin_segment(size_t s) {
            segment_ = s;
            if constexpr (External) if (workspace_.packed_) {
                const size_t offset = workspace_.offsets_[s];
                const size_t size = workspace_.offsets_[s + 1] - offset;
                workspace_.transfer(false, bytes_.get(), size, offset, &read_bytes);
                workspace_.release_file_cache(false, offset, size);
                dirty_first_ = workspace_.records_;
                dirty_end_ = 0;
            }
        }
        int8_t* segment(size_t record, int8_t* scratch) {
            if constexpr (External) if (workspace_.packed_) {
                unpack(scratch, bytes_.get() + record * workspace_.strides_[segment_],
                       workspace_.lengths_[segment_]);
                return scratch;
            }
            return workspace_.segment(record, segment_, scratch);
        }
        void save_segment(size_t record, const int8_t* scratch) {
            if constexpr (External) if (workspace_.packed_) {
                pack(bytes_.get() + record * workspace_.strides_[segment_], scratch,
                     workspace_.lengths_[segment_]);
                dirty_first_ = std::min(dirty_first_, record);
                dirty_end_ = std::max(dirty_end_, record + 1);
                return;
            }
            workspace_.save_segment(record, segment_, scratch);
        }
        void end_segment() {
            if constexpr (External) if (workspace_.packed_ && dirty_first_ < dirty_end_) {
                const size_t relative = dirty_first_ * workspace_.strides_[segment_];
                const size_t offset = workspace_.offsets_[segment_] + relative;
                const size_t size = (dirty_end_ - dirty_first_) * workspace_.strides_[segment_];
                workspace_.transfer(true, bytes_.get() + relative, size, offset, &write_bytes);
                workspace_.release_file_cache(true, offset, size);
            }
        }
    };

    using PopulationWorkers = std::vector<std::unique_ptr<PopulationWorker>>;
    PopulationWorkers make_population_workers(size_t count) {
        PopulationWorkers workers;
        workers.reserve(count);
        for (size_t i = 0; i < count; ++i)
            workers.push_back(std::make_unique<PopulationWorker>(*this));
        return workers;
    }
    void finish_population_workers(PopulationWorkers& workers) {
        // The caller has joined every worker before aggregating these counters.
        for (const auto& worker : workers) {
            read_bytes_ += worker->read_bytes;
            write_bytes_ += worker->write_bytes;
        }
        workers.clear();
    }

    void store_row(size_t record, const int8_t* row) {
        if constexpr (External) if (packed_) {
            size_t begin = 0;
            for (size_t s = 0; s < starts_.size(); ++s) {
                pack(data_.get() + begin + (record - batch_first_) * strides_[s],
                     row + starts_[s], lengths_[s]);
                begin += batch_records_ * strides_[s];
            }
            return;
        }
        if (!packed_) {
            std::memcpy(data_.get() + record * loci_, row, loci_);
            return;
        }
        for (size_t s = 0; s < starts_.size(); ++s)
            pack(data_.get() + offsets_[s] + record * strides_[s],
                 row + starts_[s], lengths_[s]);
    }

    int8_t* segment(size_t record, size_t s, int8_t* scratch) {
        if (!packed_)
            return reinterpret_cast<int8_t*>(data_.get() + record * loci_ + starts_[s]);
        size_t offset = offsets_[s] + record * strides_[s];
        if constexpr (External) offset -= offsets_[tile_first_];
        unpack(scratch, data_.get() + offset, lengths_[s]);
        return scratch;
    }

    void save_segment(size_t record, size_t s, const int8_t* scratch) {
        if (packed_) {
            size_t offset = offsets_[s] + record * strides_[s];
            if constexpr (External) {
                offset -= offsets_[tile_first_];
                // Each population segment has one writer; different segments
                // own separate bytes and dirty-range entries.
                dirty_first_[s] = std::min(dirty_first_[s], record);
                dirty_end_[s] = std::max(dirty_end_[s], record + 1);
            }
            pack(data_.get() + offset, scratch, lengths_[s]);
        }
    }

    bool packed() const { return packed_; }
    size_t bytes() const { return packed_ ? offsets_.back() : records_ * loci_; }
    uint64_t read_bytes() const { return read_bytes_; }
    uint64_t write_bytes() const { return write_bytes_; }
};

using GenotypeWorkspace = BasicGenotypeWorkspace<>;
using ChromosomeWorkspace = BasicGenotypeWorkspace<true>;
