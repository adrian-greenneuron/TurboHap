// TurboHap — C++17 haplotype phasing and genotype imputation
//
// Haplotyping and imputation with dense SNP markers
// Version 3: Can use prior haplotypes to impute only new genotypes,
//            Combines population and pedigree matching in 1 loop,
//            Uses multiple haplotype segment lengths
//
// Original Fortran program written by Paul VanRaden 2009
// Animal Improvement Programs Lab, USDA
// Compatible with the original findhap v3 result-file formats and decisions.
//
// Build from the project root: cmake -S . -B build && cmake --build build -j2
// Standalone engine: [threads] (extra arguments ignored as in findhapV3).
// The default build adds launcher.cpp for standard / --low-memory selection.

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <cinttypes>
#include <cmath>
#include <algorithm>
#include <atomic>
#include <exception>
#include <stdexcept>
#include <limits>
#include <memory>
#include <filesystem>
#include <string>
#include <sstream>
#include <type_traits>
#include <utility>
#include <vector>
#include <fstream>
#include <unistd.h>
#ifdef FINDHAP_TEST_KERNEL_CONTRACTS
#include <cassert>
#endif
#include "options_reader.hpp"
#include "pedigree_reader.hpp"
#include "progeny_x.hpp"
#include "thread_request.hpp"
#include "macos_policy.hpp"
#ifndef FINDHAP_ROW_SCHEDULE
#define FINDHAP_ROW_SCHEDULE static
#endif
#include "phase_buffer.hpp"
#include "workspace_capacity.hpp"
#include "genotype_workspace.hpp"
#include "genotype_cache.hpp"
#include "genotype_text.hpp"
#include "ancestor_quality_filter.hpp"
#include "processing_scope.hpp"
#include "chromosome_output_writer.hpp"
#include "input_validation.hpp"
#include "map_validation.hpp"
#ifdef FINDHAP_LOW_MEMORY_PLUS
#include "external_genotype_cache.hpp"
#include "bounded_genotype_workspace.hpp"
using ActiveGenotypeCache = ExternalGenotypeCache;
#else
using ActiveGenotypeCache = GenotypeCache;
#endif
#include "candidate_index.hpp"
#if TURBOHAP_NATIVE_MILAN && defined(FINDHAP_LOW_MEMORY)
#include "row_histogram_milan.hpp"
#endif
#ifndef FINDHAP_POP_SKIP_UNUSED
#if !defined(FINDHAP_LOW_MEMORY) && (TURBOHAP_NATIVE_INTEL_WORKLOAD_PROFILE || TURBOHAP_NATIVE_CASCADE_LAKE || TURBOHAP_NATIVE_AMD_WORKLOAD_PROFILE || TURBOHAP_NATIVE_MILAN || \
    (defined(__ARM_NEON) && (defined(FINDHAP_NATIVE_GB10) || defined(FINDHAP_AXION_STD_SUGGESTIONS))))
#define FINDHAP_POP_SKIP_UNUSED 1
#else
#define FINDHAP_POP_SKIP_UNUSED 0
#endif
#endif
#ifndef FINDHAP_PACKED_INHERITANCE
#if (defined(__linux__) && defined(FINDHAP_NATIVE_GB10) && defined(__ARM_NEON)) || TURBOHAP_NATIVE_INTEL_WORKLOAD_PROFILE
#define FINDHAP_PACKED_INHERITANCE 1
#else
#define FINDHAP_PACKED_INHERITANCE 0
#endif
#endif
#ifndef FINDHAP_PARALLEL_ASSIGNMENT_COUNTS
#if (defined(__linux__) && defined(FINDHAP_NATIVE_GB10) && defined(__ARM_NEON)) || TURBOHAP_NATIVE_INTEL_WORKLOAD_PROFILE || TURBOHAP_NATIVE_CASCADE_LAKE || TURBOHAP_NATIVE_AMD_WORKLOAD_PROFILE || TURBOHAP_NATIVE_MILAN
#define FINDHAP_PARALLEL_ASSIGNMENT_COUNTS 1
#else
#define FINDHAP_PARALLEL_ASSIGNMENT_COUNTS 0
#endif
#endif
#ifndef FINDHAP_PARALLEL_ASSIGNMENT_OUTPUT
#define FINDHAP_PARALLEL_ASSIGNMENT_OUTPUT FINDHAP_PARALLEL_ASSIGNMENT_COUNTS
#endif
#ifndef FINDHAP_SPLIT_TYPED_SNAPSHOT
#if !defined(FINDHAP_LOW_MEMORY) && !FINDHAP_MMAP_PHASE_BUFFERS && \
    (TURBOHAP_NATIVE_INTEL_WORKLOAD_PROFILE || TURBOHAP_NATIVE_CASCADE_LAKE || TURBOHAP_NATIVE_AMD_WORKLOAD_PROFILE || TURBOHAP_NATIVE_MILAN || (defined(__linux__) && defined(__ARM_NEON) && \
    (defined(FINDHAP_NATIVE_GB10) || defined(FINDHAP_AXION_STD_SNAPSHOT))))
#define FINDHAP_SPLIT_TYPED_SNAPSHOT 1
#else
#define FINDHAP_SPLIT_TYPED_SNAPSHOT 0
#endif
#endif
#ifndef FINDHAP_INDEX_MIN_LENGTH
#define FINDHAP_INDEX_MIN_LENGTH 16
#endif
#ifndef FINDHAP_POP_INDEX_HAPS
#define FINDHAP_POP_INDEX_HAPS 64
#endif
#ifndef FINDHAP_POP_INDEX_LENGTH
#define FINDHAP_POP_INDEX_LENGTH FINDHAP_INDEX_MIN_LENGTH
#endif
#include "profiling.hpp"
#if FINDHAP_PEDIGREE_PLACEMENT
#include "pedigree_placement.hpp"
#endif
#include "report_events.hpp"
#include "library_profile.hpp"
#include "integer_output.hpp"
#include "library_output.hpp"
#include "output_streams.hpp"
#ifndef FINDHAP_INHERIT_RECORD_OUTPUT
#define FINDHAP_INHERIT_RECORD_OUTPUT 0
#endif
#ifndef FINDHAP_OUTPUT_BATCH_WRITE
#define FINDHAP_OUTPUT_BATCH_WRITE 0
#endif
#ifndef FINDHAP_ASYNC_DENSE_OUTPUT
#if (defined(__linux__) && defined(FINDHAP_NATIVE_GB10) && defined(__ARM_NEON)) || TURBOHAP_NATIVE_INTEL_WORKLOAD_PROFILE || TURBOHAP_NATIVE_CASCADE_LAKE || TURBOHAP_NATIVE_AMD_WORKLOAD_PROFILE || TURBOHAP_NATIVE_MILAN
#define FINDHAP_ASYNC_DENSE_OUTPUT 1
#else
#define FINDHAP_ASYNC_DENSE_OUTPUT 0
#endif
#endif
#if FINDHAP_ASYNC_DENSE_OUTPUT
#include "dense_output_writer.hpp"
#endif
#include "pedigree_conflict.hpp"
#include "inheritance_codes.hpp"
#if FINDHAP_POP_CONFLICT_BACKEND
#include "conflict_scan.hpp"
#endif
#if FINDHAP_PEDIGREE_VECTOR_BASE
#include "pedigree_frequency.hpp"
#endif
#if FINDHAP_POP_VECTOR_SEGMENT || FINDHAP_POP_FINAL_SCAN_NEON || FINDHAP_POP_FINAL_SCAN_X86
#include "population_segment.hpp"
#endif
#include "pedigree_rematch.hpp"
#include "ordered_crossovers.hpp"
#include "crossover_output.hpp"
#include "haplotype_initialization.hpp"
#include "haplotype_metadata.hpp"
#if FINDHAP_LIBRARY_METADATA
#include "haplotype_output_order.hpp"
#endif
#if FINDHAP_PEDIGREE_MASK_BACKEND >= 0
#include "pedigree_crossovers.hpp"
#endif
#include <omp.h>
#ifdef __ARM_NEON
#  include <arm_neon.h>
#endif

// The unified executable links two separately optimized engines. Keep their
// entry points and internal types distinct.
#ifndef FINDHAP_ENTRY_POINT
#define FINDHAP_ENTRY_POINT main
#endif
#ifdef FINDHAP_ENGINE_NAMESPACE
namespace FINDHAP_ENGINE_NAMESPACE {
#endif

using turbohap_reporting::legacy_printf;
using turbohap_reporting::status_printf;

// ============================================================================
// Index macros for column-major arrays with Fortran index bounds
//
// Fortran column-major: A(i,j) with dims (dim1, dim2) → offset = (j-1)*dim1 + (i-1)
// Flat storage keeps the first index contiguous for loops over that index:
// flat[(j-1)*dim1 + (i-1)]. Each macro accounts for its array's index bounds.
//
// For 3D: A(i,j,k) with dims (d1,d2,d3) → flat[((k-1)*d2 + (j-1))*d1 + (i-1)]
// ============================================================================

// 2D access: Fortran A(i,j) with dimensions (d1, d2)
#define IDX2(i, j, d1) (((size_t)(j)-1) * (size_t)(d1) + ((size_t)(i)-1))

// 2D access for Fortran arrays with 0-based second dimension: A(i, 0:dim2)
// chiplist(loci, 0:chips+1): chip index j is 0-based
#define CHIPIDX(i, j, d1) ((size_t)(j) * (size_t)(d1) + ((size_t)(i)-1))

// 2D access for Fortran order(0:maxhap, maxseg): first dim 0-based, second dim 1-based
#define ORDIDX(i, seg, d1) (((size_t)(seg)-1) * (size_t)(d1) + (size_t)(i))

// 3D access: Fortran A(i,j,k) with dimensions (d1, d2, d3)
#define IDX3(i, j, k, d1, d2) ((((size_t)(k)-1) * (size_t)(d2) + ((size_t)(j)-1)) * (size_t)(d1) + ((size_t)(i)-1))

// ============================================================================
// keephap/patmat — original seg-first layout matching Fortran column-major
//
// keephap layout: IDX3(pnt, i, seg, 2, maxped)
//   = ((seg-1)*maxped + (i-1))*2 + (pnt-1)
//   Matches Fortran: keephap(pnt, i, seg) with dims (2, maxped, maxseg)
//
// KHIDX(pnt, i, seg, mped) — index into keephap[2 * mped * maxseg]
// PMIDX(i, seg, mped)      — index of the animal's two inheritance codes
// ============================================================================
#define KHIDX(pnt, i, seg, mped) \
    (((size_t)(seg)-1)*(size_t)(mped)*2 + ((size_t)(i)-1)*2 + ((size_t)(pnt)-1))
#define PMIDX(i, seg, mped) \
    (((size_t)(seg)-1)*(size_t)(mped) + ((size_t)(i)-1))

// ============================================================================
// Buffered input and output
// ============================================================================

[[noreturn, gnu::cold, gnu::noinline]] static void input_error(const char* message) {
    throw std::runtime_error(message);
}

static int report_input_error(const char* message) {
    fprintf(stderr, "Input error: %s\n", message);
    return 2;
}

struct HaplotypeCapacityError {
    int limit;
    int segment;
};

[[noreturn, gnu::cold, gnu::noinline]] static void haplotype_capacity_error(int limit, int segment) {
    throw HaplotypeCapacityError{limit, segment};
}

// Catch on the worker that raised the exception. Only rethrow on the caller
// once the team has passed all its worksharing barriers.
class WorkerFailure {
    std::atomic<bool> failed_{false};
    std::exception_ptr error_;

public:
    bool failed() const noexcept { return failed_.load(std::memory_order_relaxed); }

    [[gnu::cold, gnu::noinline]] void capture() noexcept {
        #pragma omp critical(findhap_worker_failure)
        {
            if (!error_) error_ = std::current_exception();
            failed_.store(true, std::memory_order_relaxed);
        }
    }

    void rethrow(const char* phase) const {
        if (!error_) return;
        try {
            std::rethrow_exception(error_);
        } catch (const HaplotypeCapacityError&) {
            throw;
        } catch (...) {
            fprintf(stderr, "TurboHap worker failed during %s\n", phase);
            throw;
        }
    }
};

[[noreturn, gnu::cold, gnu::noinline]] static void duplicate_genotype(int animal, int first_line, int line) {
    char message[200];
    std::snprintf(message, sizeof(message),
                  "genotypes.txt line %d, column 1: duplicate genotype record for animal %d "
                  "(first record at line %d). Keep one record per pedigree animal.",
                  line, animal, first_line);
    input_error(message);
}

static size_t worker_stride(int length) {
#if FINDHAP_PAD_SCRATCH
    // At least one whole M4 cache line between independently written slices,
    // including when malloc's base is aligned to fewer than 128 bytes.
    return (size_t(length) + 255) / 128 * 128;
#else
    return size_t(length);
#endif
}
static void configure_io_buffer(FILE* file) {
#if FINDHAP_IO_BUFFER_BYTES > 0
    if (file && setvbuf(file, nullptr, _IOFBF, FINDHAP_IO_BUFFER_BYTES) != 0)
        input_error("cannot configure I/O buffer");
#endif
}

static constexpr char ancestor_filter_filename[] = "ancestor_filter.tsv";

using FileHandle = std::unique_ptr<FILE, decltype(&fclose)>;

// Fortran OPEN without STATUS='REPLACE' preserves an existing file if no
// records are written. Otherwise CLOSE truncates it after the last record.
class RunOutputs {
    struct File {
        FILE* stream;
        std::string path;
    };
    std::vector<File> files_;

    [[noreturn, gnu::cold, gnu::noinline]] static void fail(const char* operation, const char* path) {
        char message[160];
        std::snprintf(message, sizeof(message), "cannot %s output file %s", operation, path);
        input_error(message);
    }

    static int truncate(int fd, off_t position) noexcept {
        int result;
        do { result = ftruncate(fd, position); } while (result < 0 && errno == EINTR);
        return result;
    }

    static void cleanup_warning(const char* operation, const char* path) noexcept {
        fprintf(stderr, "TurboHap output cleanup: cannot %s %s: %s\n",
                operation, path, std::strerror(errno));
    }

    [[gnu::cold, gnu::noinline]] void discard_stale_tails() noexcept {
        const int saved_errno = errno;
        for (const auto& file : files_) {
            if (!file.stream) continue;
            // fopen starts at zero and the output writer never seeks. Keep
            // the shared descriptor offset through fclose, which flushes any
            // pending bytes. A positive offset is the written prefix even
            // when stdio returned a short write or could not flush it all.
            int fd = dup(fileno(file.stream));
            if (fd < 0) cleanup_warning("inspect", file.path.c_str());
            if (fclose(file.stream) != 0) cleanup_warning("close", file.path.c_str());
            if (fd < 0) continue;
            const off_t written = lseek(fd, 0, SEEK_CUR);
            if (written < 0) cleanup_warning("locate the written prefix in", file.path.c_str());
            if (written > 0 && truncate(fd, written) != 0)
                cleanup_warning("truncate", file.path.c_str());
            close(fd);
        }
        errno = saved_errno;
    }

public:
    RunOutputs() = default;
    RunOutputs(const RunOutputs&) = delete;
    RunOutputs& operator=(const RunOutputs&) = delete;

    ~RunOutputs() {
        if (!files_.empty()) discard_stale_tails();
    }

    FILE* open(std::string path) {
        FileHandle file(fopen(path.c_str(), "r+"), fclose);
        if (!file && errno == ENOENT) file.reset(fopen(path.c_str(), "w+"));
        if (!file) fail("open", path.c_str());
        configure_io_buffer(file.get());
        files_.push_back({file.get(), std::move(path)});
        return file.release();
    }

    void finish() {
        for (auto& file : files_) {
            FILE* stream = file.stream;
            const off_t position = ftello(stream);
            // A failed write can leave ferror set even if a flush succeeds.
            if (ferror(stream)) fail("write", file.path.c_str());
            if (fflush(stream) != 0 || position < 0) fail("flush", file.path.c_str());
            if (position > 0 && truncate(fileno(stream), position) != 0)
                fail("truncate", file.path.c_str());
            if (fclose(std::exchange(file.stream, nullptr)) != 0)
                fail("close", file.path.c_str());
        }
        files_.clear();
    }
};

class ChromosomeResultDirectory {
    const std::filesystem::path destination_{"chromosome-results"};
    std::filesystem::path staging_;

    void require_new_destination() const {
        if (std::filesystem::exists(std::filesystem::symlink_status(destination_)))
            input_error("chromosome-results already exists; use a fresh working directory");
    }

public:
    ChromosomeResultDirectory() {
        require_new_destination();
        char name[] = ".chromosome-results.XXXXXX";
        const char* created = mkdtemp(name);
        if (!created) input_error("cannot create temporary chromosome result directory");
        staging_ = created;
    }
    ChromosomeResultDirectory(const ChromosomeResultDirectory&) = delete;
    ChromosomeResultDirectory& operator=(const ChromosomeResultDirectory&) = delete;
    ~ChromosomeResultDirectory() {
        if (!staging_.empty()) {
            std::error_code error;
            std::filesystem::remove_all(staging_, error);
        }
    }
    const std::filesystem::path& staging() const { return staging_; }
    void publish() {
        require_new_destination();
        std::filesystem::rename(staging_, destination_);
        staging_.clear();
    }
};

struct ResultFiles {
    std::filesystem::path directory;
    FILE *reference = nullptr, *rejected = nullptr, *quality = nullptr;
    FILE *library = nullptr, *assignments = nullptr, *crossovers = nullptr;
    FILE *inheritance = nullptr, *allele_frequency = nullptr, *haplotypes = nullptr, *genotypes = nullptr;
    int reference_count = 0, reported = 0, ancestors = 0, male_ancestors = 0, excluded = 0;
};

static ResultFiles open_result_files(RunOutputs& files, const std::filesystem::path& directory,
                                     int genout, int hapout, int listout) {
    const auto open = [&](const char* name) { return files.open((directory / name).string()); };
    ResultFiles result;
    result.directory = directory;
    result.reference = open("hap.Xseg");
    result.rejected = open("do_not_distrib");
    result.quality = open("hap.filled");
    if (listout != 0) {
        result.library = open("hap.list");
        result.assignments = open("hap.found");
    }
    result.crossovers = open("cross.overs");
    result.inheritance = open("hap.inherit");
    result.allele_frequency = open("allele.frequency");
    if (hapout > 0) result.haplotypes = open("haplotypes.txt");
    if (genout > 0) result.genotypes = open("genotypes.filled");

    FILE* fp28 = result.inheritance;
    fprintf(fp28, " Codes  1 and  2 indicate  progeny hap matches 1st or 2nd parent hap\n");
    fprintf(fp28, " Codes -1 and -2 indicate  progeny and parent match except for 1 SNP\n");
    fprintf(fp28, " Codes X1 and X2 indicate  crossovers from 2 to 1 or from 1 to 2\n");
    fprintf(fp28, " Code   0        indicates progeny hap matches neither hap of parent\n");
    fprintf(fp28, " Code   H        indicates progeny hap matches both (parent is homozygous)\n");
    fprintf(fp28, " Code  ??        indicates progeny and parents each heterozygous for both haps\n");
    fprintf(fp28, " Code  ?1 and ?2 indicate  progeny, parents all heterozygous, set from neighbor segments\n");
    fprintf(fp28, " Codes  m and  p indicate  parents known, not genotyped, no match\n");
    fprintf(fp28, " Code   .        indicates parent  not known\n");
    fprintf(fp28, "\n");
    fprintf(fp28, "  progeny chro pnt haplotype_inheritance_codes\n");
    return result;
}

struct AnimalQuality {
    int observed;
    int filled;
    float score;
    bool reported;
};

static float estimate_output_quality(int observed, int filled, int references,
                                     int progeny, int64_t physical_length, int loci) {
    if (observed > 0) {
        float correct = float(references) / (references + 50.0f);
        correct = correct * observed / (observed + 120.0f * physical_length / 3.0e9);
        return (0.9998f * observed + correct * (loci - observed)) / loci;
    }
    const float correct = float(progeny) / (progeny + 1.0f);
    return (0.9998f * observed + correct * (filled - observed)) / loci;
}

static void write_result_block(FILE* output, int animal, int chip, int loci,
                               const char* payload, size_t bytes) {
    fprintf(output, "%10d%10d%10d ", animal, chip, loci);
    if (fwrite(payload, 1, bytes, output) != bytes || fputc('\n', output) == EOF)
        input_error("cannot write chromosome result record");
}

// Priors can contain very long records; never split them at a fixed line length.
static bool read_record(FILE* file, std::string& record) {
    record.clear();
    char buffer[4096];
    while (fgets(buffer, sizeof(buffer), file)) {
        record += buffer;
        if (!record.empty() && record.back() == '\n') return true;
    }
    if (ferror(file)) input_error("cannot read prior file");
    return !record.empty();
}

template <typename T>
static T prior_field(const std::string& record, size_t offset, size_t width) {
    if (record.size() < offset + width) input_error("truncated hap.listpriors header");
    std::istringstream field(record.substr(offset, width));
    T value;
    if (!(field >> value)) input_error("invalid hap.listpriors header field");
    field >> std::ws;
    if (!field.eof()) input_error("invalid hap.listpriors header field");
    return value;
}

static void skip_prior_headers(FILE* file) {
    std::string record;
    if (!read_record(file, record) || !read_record(file, record))
        input_error("prior file needs two header records");
}

class HaplotypeTextBuffers {
    std::unique_ptr<char[]> list_, found_;
    size_t found_capacity_;

public:
    explicit HaplotypeTextBuffers(size_t max_loci)
        : list_(new char[max_loci + 61]), found_(new char[max_loci + 31]),
          found_capacity_(max_loci + 31) {}

    char* list() const noexcept { return list_.get(); }
    char* found(size_t required) {
        if (required > found_capacity_) {
            found_.reset();
            found_.reset(new char[required]);
            found_capacity_ = required;
        }
        return found_.get();
    }
};

// Write one hap.list line: 2f10.5,i10,2i3,i7,i8,i6,1x,<nhapdata>i1
static void fast_write_haplist_line(FILE* fp, char* haplist_buf_io, float hap_pct, double tot_pct,
    int hap_num, int chrom, int istep, int iseg, int istart, int seglen,
    const int8_t* hapdata, int nhapdata) {
    FINDHAP_LIBRARY_SAMPLE(format_sample, 3);
#if FINDHAP_LIBRARY_FORMAT
    turbohap_output::haplist_header<(FINDHAP_LIBRARY_FORMAT >= 2)>(haplist_buf_io, hap_pct, tot_pct, hap_num, chrom,
                                   istep, iseg, istart, seglen);
#else
    snprintf(haplist_buf_io, 59, "%10.5f%10.5f%10d%3d%3d%7d%8d%6d ",
             hap_pct, tot_pct, hap_num, chrom, istep, iseg, istart, seglen);
#endif
    for (int l = 0; l < nhapdata; l++) {
        haplist_buf_io[58 + l] = static_cast<char>(hapdata[l] + '0');
    }
    haplist_buf_io[58 + nhapdata] = '\n';
    FINDHAP_LIBRARY_SAMPLE_END(format_sample);
    FINDHAP_LIBRARY_SAMPLE(submit_sample, 4);
    fwrite(haplist_buf_io, 1, 58 + nhapdata + 1, fp);
}

// Write one hap.found line: i10,i2,i3,i6,3x,<nsegs>(1x,i0)
template<typename HaplotypeId>
static size_t format_hapfound_line(char* hapfound_buf_io, int id, int pnt_val, int istep,
    int nsegs, const HaplotypeId* keephap_arr, int pnt_idx, int anml_idx, int maxanml) {
    int pos = snprintf(hapfound_buf_io, 25, "%10d%2d%3d%6d   ", id, pnt_val, istep, nsegs);
    for (int s = 1; s <= nsegs; s++) {
        hapfound_buf_io[pos++] = ' ';
        int v = keephap_arr[KHIDX(pnt_idx, anml_idx, s, maxanml)];
        if (v == 0) {
            hapfound_buf_io[pos++] = '0';
        } else {
            int digits[10], ndigits = 0;
            while (v > 0) {
                digits[ndigits++] = v % 10;
                v /= 10;
            }
            for (int d = ndigits - 1; d >= 0; d--) {
                hapfound_buf_io[pos++] = static_cast<char>(digits[d] + '0');
            }
        }
    }
    hapfound_buf_io[pos++] = '\n';
    return static_cast<size_t>(pos);
}

template<typename HaplotypeId>
static void fast_write_hapfound_line(FILE* fp, HaplotypeTextBuffers& buffers, int id, int pnt_val, int istep,
    int nsegs, const HaplotypeId* keephap_arr, int pnt_idx, int anml_idx,
    int maxanml, int maxseg_dim) {
    FINDHAP_LIBRARY_SAMPLE(format_sample, 0);
    // One separator plus up to ten decimal digits per segment, and the header.
    char* hapfound_buf_io = buffers.found(25 + size_t(11) * nsegs);
    const size_t bytes = format_hapfound_line(hapfound_buf_io, id, pnt_val, istep,
                                             nsegs, keephap_arr, pnt_idx, anml_idx, maxanml);
    FINDHAP_LIBRARY_SAMPLE_END(format_sample);
    FINDHAP_LIBRARY_SAMPLE(submit_sample, 1);
    fwrite(hapfound_buf_io, 1, bytes, fp);
}

#if FINDHAP_PARALLEL_ASSIGNMENT_OUTPUT
static constexpr size_t assignment_buffer_bytes = 8 * 1024 * 1024;

template<bool group_writes = false, typename HaplotypeId>
static void write_typed_assignments(FILE* output, const HaplotypeId* assignments, const int* ids,
    const int* genotyped, int animals, int max_animals, int segments, int step, int threads,
    size_t row_stride) {
    const size_t capacity = std::min({assignment_buffer_bytes / row_stride, size_t(1024), size_t(animals)});
    auto records = findhap::make_phase_buffer<char>(capacity * row_stride);
    std::vector<int> indices(capacity);
    std::vector<size_t> sizes(capacity);
    int next = 1;
    size_t count = 0;
    bool failed = false;
    #pragma omp parallel num_threads(threads)
    {
        const size_t workers = size_t(omp_get_num_threads());
        for (;;) {
            #pragma omp single
            {
                count = 0;
                while (count < capacity && next <= animals) {
                    if (genotyped[next] > 0) indices[count++] = next;
                    ++next;
                }
            }
            if (count == 0) break;
            // Pack each contiguous range straight into its own part of the
            // existing buffer. The serial drain writes those ranges in order.
            const size_t group_size = group_writes ? (count + workers - 1) / workers : 1;
            #pragma omp for schedule(static)
            for (size_t first = 0; first < count; first += group_size) {
                char* range = records.get() + first * row_stride;
                size_t bytes = 0;
                const size_t end = std::min(first + group_size, count);
                for (size_t row = first; row < end; ++row) {
                    const int animal = indices[row];
                    bytes += format_hapfound_line(range + bytes, ids[animal], 1, step, segments,
                                                 assignments, 1, animal, max_animals);
                    bytes += format_hapfound_line(range + bytes, ids[animal], 2, step, segments,
                                                 assignments, 2, animal, max_animals);
                }
                sizes[first] = bytes;
            }
            #pragma omp single
            {
                for (size_t first = 0; first < count; first += group_size) {
                    if (fwrite(records.get() + first * row_stride, 1, sizes[first], output) != sizes[first]) {
                        failed = true;
                        break;
                    }
                }
            }
            if (failed) break;
        }
    }
    if (failed) throw std::runtime_error("cannot write haplotype assignments");
}

// One owner per chromosome preserves each file's original animal/parent order.
// The shared byte budget is split between owners, with no per-batch team or
// serial writer barrier. The existing path handles an exceptionally wide row.
template<typename HaplotypeId>
static bool write_chromosome_assignments(const std::vector<ResultFiles>& results,
    const int* startseg, const int* stopseg, int first_chromosome, int last_chromosome,
    const HaplotypeId* assignments, const int* ids, const int* genotyped,
    int animals, int max_animals, int step, int threads) {
    if (threads < 2 || first_chromosome == last_chromosome) return false;
    size_t widest = 0;
    for (int c = first_chromosome; c <= last_chromosome; ++c)
        if (results[c - first_chromosome].assignments)
            widest = std::max(widest, 2 * (25 + size_t(11) * (stopseg[c - 1] - startseg[c - 1] + 1)));
    if (widest == 0) return true;
    if (widest > assignment_buffer_bytes) return false;
    const int workers = std::min({threads, last_chromosome - first_chromosome + 1,
                                 int(assignment_buffer_bytes / widest)});
    const size_t bytes_per_worker = assignment_buffer_bytes / workers;
    auto buffers = findhap::make_phase_buffer<char>(bytes_per_worker * workers);
    WorkerFailure failure;
    #pragma omp parallel num_threads(workers)
    {
        findhap::macos::worker_enter();
        char* buffer = buffers.get() + size_t(omp_get_thread_num()) * bytes_per_worker;
        #pragma omp for schedule(dynamic,1)
        for (int c = first_chromosome; c <= last_chromosome; ++c) {
            if (failure.failed()) continue;
            FILE* file = results[c - first_chromosome].assignments;
            if (!file) continue;
            try {
                const int first = startseg[c - 1], segments = stopseg[c - 1] - first + 1;
                const auto* slice = assignments + KHIDX(1, 1, first, max_animals);
                const size_t row_bytes = 2 * (25 + size_t(11) * segments);
                size_t used = 0;
                const auto flush = [&] {
                    if (used && fwrite(buffer, 1, used, file) != used)
                        throw std::runtime_error("cannot write chromosome haplotype assignments");
                    used = 0;
                };
                for (int animal = 1; animal <= animals; ++animal) {
                    if (genotyped[animal] == 0) continue;
                    if (bytes_per_worker - used < row_bytes) flush();
                    for (int parent = 1; parent <= 2; ++parent)
                        used += format_hapfound_line(buffer + used, ids[animal], parent, step,
                                                     segments, slice, parent, animal, max_animals);
                }
                flush();
            } catch (...) { failure.capture(); }
        }
    }
    failure.rethrow("chromosome assignment output");
    return true;
}
#endif

// ============================================================================
// nhash function — port of nhash.f90
// ============================================================================

static int nhash(int id, int mode, int* idtab, int isize, int& maxn) {
    if (mode == -1) {
        maxn = 0;
        for (int i = 0; i < isize; i++) idtab[i] = 0;
        return -1;
    }
    if (id == 0) return 0;
    int64_t hash_val = static_cast<int64_t>(id) * 2654435761LL;
    int result = static_cast<int>(std::abs(hash_val) % isize) + 1;
    for (int j = 0; j < isize; j++) {
        if (idtab[result - 1] == 0) {
            if (mode == 1) {
                idtab[result - 1] = id;
                maxn++;
                if (maxn > 0.9 * isize) {
                    status_printf(" hash table size %d   is too small, increase it\n", isize);
                    exit(261);
                }
            } else {
                result = 0;
            }
            return result;
        }
        if (idtab[result - 1] == id) return result;
        result = (result) % isize + 1;  // linear probe: mod(result+1-1, isize) + 1
    }
    status_printf(" Function NHASH dropped out of the loop\n");
    exit(41);
    return 0;
}



// ============================================================================
// listhap — Find haplotype from list (port of subroutine listhap)
// ============================================================================

// The ordered lookup can access order and frequency storage through views
// retained across calls, including calls that bypass lookup preparation.
template <int SWITCH_VAL>
static bool listhap(int8_t* __restrict__ storeseg, int8_t* __restrict__ haplist_arr, int* hapfreq_arr,
    int* order_arr, int* __restrict__ suggest, int len, int maxlen, int* __restrict__ nhap_arr,
    int maxhap, int seg, int maxseg, int* __restrict__ found, float errate,
    CandidateIndex* candidate_index = nullptr) {

    const int sugnum = 16;
    // Keep the original stack storage for short segments. Longer segments use
    // reusable worker-local storage; the actual length bounds every access.
    int8_t short_segment[128];
    int short_fill[128], short_conflict[128];
    int8_t* __restrict__ segment = short_segment;
    int* __restrict__ fill_arr = short_fill;
    int* __restrict__ conflict_arr = short_conflict;
    if (len > 128) {
        static thread_local std::vector<int8_t> segment_storage;
        static thread_local std::vector<int> fill_storage, conflict_storage;
        if (segment_storage.size() < (size_t)len) {
            segment_storage.resize(len);
            fill_storage.resize(len);
            conflict_storage.resize(len);
        }
        segment = segment_storage.data();
        fill_arr = fill_storage.data();
        conflict_arr = conflict_storage.data();
    }
    int check = 0, fnd = 0, minhap = 1, minfreq;
    bool genotype_changed = false;
    int starti, stopi, startj, ii, hold, miss, conflicts;

    // Pre-compute the base offset for this segment's haplist slice
    // haplist(l, hap, seg) → ((seg-1)*maxhap + (hap-1))*maxlen + (l-1)
    // For a given seg, the base is (seg-1)*maxhap*maxlen
    const size_t haplotype_stride = maxlen;
    const size_t seg_base = ((size_t)seg - 1) * maxhap * haplotype_stride;
    // For hapfreq(hap, seg) → (seg-1)*maxhap + (hap-1)
    const size_t freq_base = ((size_t)seg - 1) * maxhap;
    // For order(i, seg) → (seg-1)*(maxhap+1) + i
    const size_t ord_base = ((size_t)seg - 1) * (maxhap + 1);

    // Set missing SNP to 1
#if FINDHAP_POP_VECTOR_SEGMENT
    check = findhap_population::normalise(storeseg, segment, len);
#else
    for (int l = 0; l < len; l++) {
        segment[l] = storeseg[l];
        if (segment[l] == 5) segment[l] = 1;
        if (segment[l] != 1) check++;
    }
#endif
    CandidateIndex::Signature signature;
    if (candidate_index) signature = candidate_index->encode(segment);

    starti = 1;
    // Use suggested haps if locations not switching
    if constexpr (SWITCH_VAL == 0) {
        startj = 1;
    } else {
        startj = sugnum + 1;
    }

    found[0] = 0;
    found[1] = 0;

    // Find 2 haplotypes
    for (int j = startj; j <= sugnum + 2; j++) {
        if (j <= sugnum) {
            // Parent or grandparent haplotypes
            if (suggest[j - 1] > 0) {
                starti = suggest[j - 1];
                stopi = suggest[j - 1];
                // First parent already found
                if (fnd == 1 && j <= sugnum / 2) continue;
            } else {
                continue;
            }
        } else {
            // Population haplotypes
            if (j == sugnum + 1) starti = 1;
            stopi = nhap_arr[(seg - 1) * FINDHAP_NHAP_STRIDE];
        }

        minfreq = 99999999;
#if FINDHAP_USE_ORDERED
        ActiveOrderedLookup* ordered = nullptr;
        if (candidate_index && j > sugnum &&
            (SWITCH_VAL == 1 || (errate * check >= 0 && errate * check < 1)) &&
            __builtin_popcountll(signature.zero | signature.two) >= 4) {
            ordered = candidate_index->ordered(order_arr + ord_base, hapfreq_arr + freq_base,
                                               nhap_arr[(seg - 1) * FINDHAP_NHAP_STRIDE], SWITCH_VAL == 1);
        }
#endif
        // Search list in frequency order
        for (int i = starti; i <= stopi; i++) {
#if FINDHAP_USE_ORDERED
            int previous_minhap = 0;
            if (ordered) {
                i = ordered->next(i, stopi, signature.zero, signature.two);
                if (i > stopi) break;
            }
#endif
            if (j <= sugnum) {
                ii = i;
            } else {
                ii = order_arr[ord_base + i];
                if (ii == 0) continue;
            }

            const size_t freq_idx = freq_base + (ii - 1);
            FINDHAP_COUNT(2);
            if (
#if FINDHAP_USE_ORDERED
                !ordered &&
#endif
                hapfreq_arr[freq_idx] < minfreq) {
                minfreq = hapfreq_arr[freq_idx];
                minhap = i;
            }
            miss = 0;
            conflicts = 0;
            // This view reads the row before and after the fill block.
            const int8_t* hap_ptr = &haplist_arr[seg_base + (ii - 1) * haplotype_stride];

            // Keep minfreq/minhap bookkeeping even for filtered candidates.
            // This only rejects a proven conflict excess; survivors still use
            // the unchanged full scan and position-dependent acceptance rule.
            if (candidate_index
#if FINDHAP_USE_ORDERED && !FINDHAP_ORDERED_RECHECK
                && !ordered
#endif
            ) {
                const uint64_t bits = candidate_index->conflicts(signature, ii);
                if constexpr (SWITCH_VAL == 1) {
                    if (bits) goto label_50;
                } else {
                    if (__builtin_popcountll(bits) > errate * check) goto label_50;
                }
            }

#if FINDHAP_POP_FULL
            if (candidate_index && candidate_index->full_count(signature, ii, conflicts))
                goto population_counted;
#endif
#if FINDHAP_POP_COMPLETE
            if (candidate_index && candidate_index->complete(signature, ii)) {
                conflicts = __builtin_popcountll(candidate_index->conflicts(signature, ii));
                goto population_counted;
            }
#endif
            FINDHAP_COUNT(3);
            // Phase 1: count conflicts in 16 signed-byte lanes with ARM NEON.
            // Explicit byte intrinsics avoid scalar C++ integer promotion.
            conflicts = 0;
#ifdef __ARM_NEON
            {
                int l = 0;
                const uint8x16_t v_one = vdupq_n_u8(1);
                const int vector_end = len - len % 16;
                while (l < vector_end) {
                    // Error-tolerant searches can reject once a partial count
                    // exceeds the same limit used by the complete count. The
                    // zero-error path already rejects on each conflicting vector.
                    constexpr int block_size = SWITCH_VAL == 0 ? 32 : 4096;
                    const int block_end = l + std::min(vector_end - l, block_size);
                    uint16x8_t v_acc = vdupq_n_u16(0);
                    for (; l < block_end; l += 16) {
                        int8x16_t s = vld1q_s8(segment + l);
                        int8x16_t h = vld1q_s8(hap_ptr + l);
                        uint8x16_t abd = vreinterpretq_u8_s8(vabdq_s8(s, h));
                        uint8x16_t mask = vcgtq_u8(abd, v_one);
                        if constexpr (SWITCH_VAL == 1) {
                            if (vmaxvq_u8(mask) != 0) goto label_50;
                        }
                        uint8x16_t ones = vshrq_n_u8(mask, 7);
                        v_acc = vpadalq_u8(v_acc, ones);
                    }
                    conflicts += (int)vaddvq_u32(vpaddlq_u16(v_acc));
                    if constexpr (SWITCH_VAL == 0) {
                        if (conflicts > errate * check) goto label_50;
                    }
                }
                for (; l < len; l++) {                          // scalar tail
                    conflicts += (std::abs(segment[l] - hap_ptr[l]) > 1);
                }
            }
#elif FINDHAP_POP_CONFLICT_BACKEND
            // Use the same signed, saturating byte predicate already qualified
            // for x86 crossover masks. Rejected candidates never reach the
            // unchanged acceptance/fill pass; survivors retain exact counts.
            conflicts = findhap_simd::scan<SWITCH_VAL, FINDHAP_POP_CONFLICT_BACKEND>(
                segment, hap_ptr, len, errate * check);
            if (conflicts < 0) goto label_50;
#else
            for (int begin = 0; begin < len;) {
                const int end = begin + std::min(len - begin, 32);
                #pragma omp simd reduction(+:conflicts)
                for (int l = begin; l < end; l++) {
                    conflicts += (std::abs(segment[l] - hap_ptr[l]) > 1);
                }
                if constexpr (SWITCH_VAL == 1) {
                    if (conflicts > 0) goto label_50;
                }
                if (conflicts > errate * check) goto label_50;
                begin = end;
            }
#endif

#if FINDHAP_POP_COMPLETE || FINDHAP_POP_FULL
        population_counted:
#endif
            // Quick reject: most candidates fail here
            if constexpr (SWITCH_VAL == 1) {
                if (conflicts > 0) goto label_50;
            }
            if (conflicts > errate * check) goto label_50;

            // Phase 2: reuse the complete zero-conflict result. We still
            // collect missing loci in order and apply every original update.
            // Nonzero conflicts retain the position-dependent reference test.
            if (conflicts == 0) {
#if FINDHAP_POP_FULL >= 2
                int exact_count;
                if (candidate_index && candidate_index->full_count(signature, ii, exact_count)) {
                    miss = candidate_index->full_fill(signature, ii, fill_arr);
                } else
#endif
                for (int l = 0; l < len; l++) {
                    if (hap_ptr[l] == 1 && segment[l] != 1) {
                        fill_arr[miss] = l;
                        miss++;
                    }
                }
            } else {
                conflicts = 0;
                for (int l = 0; l < len; l++) {
                    if (std::abs(segment[l] - hap_ptr[l]) > 1) {
                        conflict_arr[conflicts] = l;
                        conflicts++;
                        // Later missing loci must not reject a match already
                        // accepted at the last conflicting locus.
                        if (conflicts > errate * (check - miss)) goto label_50;
                    } else {
                        if (hap_ptr[l] == 1 && segment[l] != 1) {
                            fill_arr[miss] = l;
                            miss++;
                        }
                    }
                }
            }

            // No checkable loci
            if (check - miss == 0 && check > 0) continue;
#if FINDHAP_USE_ORDERED
            // Skipped candidates still determine the legacy frequency minimum.
            // Query before incrementing the accepted haplotype's frequency.
            if constexpr (SWITCH_VAL == 1)
                if (ordered) ordered->minimum(starti, i, minfreq, minhap);
            previous_minhap = minhap;
#endif
            // Whole segment equals haplotype
            if (conflicts > 0) {
                // Set conflicts to missing
                genotype_changed = true;
                for (int m = 0; m < conflicts; m++) {
                    storeseg[conflict_arr[m]] = 5;
                }
            }
            if (miss > 0) {
                // Fill missing values in haplotype list
                int8_t* __restrict__ hap_wr = &haplist_arr[seg_base + (ii - 1) * haplotype_stride];
                for (int m = 0; m < miss; m++) {
                    hap_wr[fill_arr[m]] = segment[fill_arr[m]];
                }
                if (candidate_index) candidate_index->refresh(ii, hap_wr);
            }
            // Location of matching haplotype
            fnd++;
            found[fnd - 1] = ii;
            hapfreq_arr[freq_idx]++;
            check = 0;
            // Backsolve for other haplotype
#if FINDHAP_POP_VECTOR_SEGMENT
            check = findhap_population::backsolve(segment, hap_ptr, storeseg, len);
#else
            for (int l = 0; l < len; l++) {
                segment[l] = 2 * segment[l] - hap_ptr[l];
                if (storeseg[l] == 5) segment[l] = 1;
                if (segment[l] != 1) check++;
            }
#endif
            if (candidate_index && fnd < 2) signature = candidate_index->encode(segment);
            // Switch order if more frequent
            if constexpr (SWITCH_VAL == 1) {
                // Population startup has a dense active order prefix.
#ifdef FINDHAP_TEST_KERNEL_CONTRACTS
                assert(order_arr[ord_base + minhap] > 0);
#endif
                if (hapfreq_arr[freq_idx] >
                    hapfreq_arr[freq_base + order_arr[ord_base + minhap] - 1]) {
                    hold = order_arr[ord_base + minhap];
                    order_arr[ord_base + minhap] = order_arr[ord_base + i];
                    order_arr[ord_base + i] = hold;
                } else {
                    minhap = i;
                }
            } else {
                minhap = i;
            }
#if FINDHAP_USE_ORDERED
            if (candidate_index) {
                if constexpr (SWITCH_VAL == 1) {
                    if (j > sugnum) candidate_index->changed_positions(i, previous_minhap);
                    else candidate_index->frequency_changed(ii);
                } else {
                    candidate_index->frequency_changed(ii);
                }
            }
#endif
            goto label_75;
        label_50:
            continue;
        }
        // No matching haplotype
        if (j > sugnum) {
            fnd++;
            nhap_arr[(seg - 1) * FINDHAP_NHAP_STRIDE]++;
            found[fnd - 1] = nhap_arr[(seg - 1) * FINDHAP_NHAP_STRIDE];
            minhap = nhap_arr[(seg - 1) * FINDHAP_NHAP_STRIDE];
            if (nhap_arr[(seg - 1) * FINDHAP_NHAP_STRIDE] > maxhap)
                haplotype_capacity_error(maxhap, seg);
#if FINDHAP_LIBRARY_METADATA
            // Only this new order slot was previously unpublished. Existing
            // frequency-order swaps cannot have reached it.
#if FINDHAP_METADATA_CHUNK > 1
            if ((minhap-1)%FINDHAP_METADATA_CHUNK==0) {
                const int end=minhap+std::min(maxhap-minhap,FINDHAP_METADATA_CHUNK-1);
                #pragma omp simd
                for(int position=minhap;position<=end;++position)order_arr[ord_base+position]=position;
            }
#else
            order_arr[ord_base + minhap]=minhap;
#endif
#endif
            hapfreq_arr[freq_base + nhap_arr[(seg - 1) * FINDHAP_NHAP_STRIDE] - 1] = 1;
            // Store new haplotype
            int8_t* __restrict__ new_hap = &haplist_arr[seg_base + (minhap - 1) * haplotype_stride];
            findhap_haplotype_init::publish_row(new_hap, maxlen);
            for (int l = 0; l < len; l++) {
                new_hap[l] = segment[l];
            }
            for (int l = len; l < maxlen; l++) {
                new_hap[l] = 0;
            }
            if (candidate_index) candidate_index->refresh(nhap_arr[(seg - 1) * FINDHAP_NHAP_STRIDE], new_hap);
        }
    label_75:
        // Starting location for next search
        if (fnd == 2) break;
        if (j == sugnum + 1) {
            starti = minhap;
        } else {
            minhap = 1;
        }
    }

    // Backsolve for 1st from 2nd haplotype
    {
        const int8_t* __restrict__ hap2 = &haplist_arr[seg_base + (found[1] - 1) * haplotype_stride];
        for (int l = 0; l < len; l++) {
            if (storeseg[l] == 5 || hap2[l] == 1) {
                segment[l] = 1;
            } else {
                segment[l] = 2 * storeseg[l] - hap2[l];
            }
        }
    }
    // Check for opposite homozygotes in 1st hap
    {
        const int8_t* hap1 = &haplist_arr[seg_base + (found[0] - 1) * haplotype_stride];
#if (FINDHAP_POP_FINAL_SCAN_NEON && defined(__ARM_NEON)) || FINDHAP_POP_FINAL_SCAN_X86
        if (!findhap_population::final_fill_positions(segment,hap1,len,fill_arr,miss))
            return genotype_changed;
#else
        miss = 0;
        for (int l = 0; l < len; l++) {
            if (std::abs(segment[l] - hap1[l]) > 1) {
                return genotype_changed;
            } else {
                if (hap1[l] == 1 && segment[l] != 1) {
                    miss++;
                    fill_arr[miss - 1] = l;
                }
            }
        }
#endif
        // Fill missing values in 1st haplotype
        if (miss > 0) {
            int8_t* __restrict__ hap1_wr = &haplist_arr[seg_base + (found[0] - 1) * haplotype_stride];
            for (int m = 0; m < miss; m++) {
                hap1_wr[fill_arr[m]] = segment[fill_arr[m]];
            }
            if (candidate_index) candidate_index->refresh(found[0], hap1_wr);
        }
    }
    return genotype_changed;
}

// ============================================================================
// MAIN PROGRAM
// ============================================================================

template<typename HaplotypeId>
static int run_findhap(int argc, char* argv[], const findhap::Options& options) {
    using InheritanceCode = findhap::inheritance::Code;
#if FINDHAP_PACKED_INHERITANCE
    // Both parent roles are written by the same segment owner before
    // chromosome orientation and output can access the pair.
    using InheritancePair = findhap::inheritance::NibblePair;
#else
    using InheritancePair = findhap::inheritance::BytePair;
#endif
    // ---- Allocatable arrays (will be allocated after sizes are known) ----
    std::unique_ptr<float[]> freq, basefreq, curfreq;
    std::unique_ptr<float[]> linkREL, patREL;
    std::unique_ptr<int8_t[]> genotype, genovec;
    std::unique_ptr<int8_t[]> haplotype;
    std::unique_ptr<int[]> loc1, locN, prog, gprog, pntloc;
    std::unique_ptr<int[]> locate1, locateN, phaps;
    std::unique_ptr<int[]> ped, chiplist_arr;
    std::unique_ptr<int[]> loclist, chiplen, called;
    std::unique_ptr<int[]> animkey, nhap_arr, missall, keyvec;
    std::unique_ptr<int[]> listeqn, ids, eqns, missadd;
    std::unique_ptr<int[]> startloc, prevstartloc;
    std::unique_ptr<int[]> len_arr, prevlen, chro, chip_arr;
    std::unique_ptr<uint8_t[]> high;
    std::unique_ptr<int[]> startseg, stopseg, proghaps;
    std::unique_ptr<int[]> nfreq, ofreq, sumfreq_arr, birth;
    std::unique_ptr<int[]> bfreq, cfreq, sumbase, sumcurr;
    std::unique_ptr<char[]> sexv;
    std::unique_ptr<InheritancePair[]> patmat;
    // These arrays contain only zero or segment-local IDs bounded by maxhap.
    // Animal indices, counts, frequency arithmetic and search remain int.
    std::unique_ptr<HaplotypeId[]> keephap, fromprog;
#if FINDHAP_MMAP_PHASE_BUFFERS && FINDHAP_PHASE_STD_SNAPSHOT_REUSE > 0 && \
    !defined(FINDHAP_LOW_MEMORY) && !FINDHAP_SPLIT_TYPED_SNAPSHOT
    findhap::ReusableSnapshotBuffer<HaplotypeId, FINDHAP_PHASE_STD_SNAPSHOT_REUSE == 2> prevkeephap;
#else
    findhap::PhaseBuffer<HaplotypeId> prevkeephap;
#endif
#if FINDHAP_SPLIT_TYPED_SNAPSHOT
    findhap::PhaseBuffer<HaplotypeId> prev_typed_assignments;
    std::vector<int> previous_row;
    int previous_typed_rows = 0, previous_untyped_rows = 0;
#endif
#if FINDHAP_HAPLIST_INIT == 3
    findhap_haplotype_init::MappedLibrary haplist_arr;
#if FINDHAP_MMAP_PHASE_BUFFERS && FINDHAP_PHASE_STD_SNAPSHOT_REUSE > 0 && !defined(FINDHAP_LOW_MEMORY)
    findhap::ReusableSnapshotBuffer<int8_t, FINDHAP_PHASE_STD_SNAPSHOT_REUSE == 2> prevhaplist;
#else
    findhap::PhaseBuffer<int8_t> prevhaplist;
#endif
#else
    std::unique_ptr<int8_t[]> haplist_arr;
#if FINDHAP_MMAP_PHASE_BUFFERS && FINDHAP_PHASE_STD_SNAPSHOT_REUSE > 0 && !defined(FINDHAP_LOW_MEMORY)
    findhap::ReusableSnapshotBuffer<int8_t, FINDHAP_PHASE_STD_SNAPSHOT_REUSE == 2> prevhaplist;
#else
    findhap::PhaseBuffer<int8_t> prevhaplist;
#endif
#endif
    std::vector<size_t> prevhap_offsets;
    std::unique_ptr<int8_t[]> hapseg_arr;
#if FINDHAP_LIBRARY_METADATA
    findhap_haplotype_metadata::Array hapfreq_arr,order_arr;
#else
    std::unique_ptr<int[]> hapfreq_arr, order_arr;
#endif
    // Cache arrays
    std::unique_ptr<ActiveGenotypeCache> genotype_cache;
    std::unique_ptr<int[]> cache_chipn, cache_id, cache_loc;
    std::unique_ptr<int[]> cache_anml_idx;
    std::unique_ptr<int[]> cache_called_cnt, cache_eqns_nn;
    int cache_ngen;

    // Scalar variables
    int l, i, j, k, n, m, ii, maxnhap = 0, maxnseg, ngen, nprior = 0;
    int baseyr, curryr, today, nbase, step, steps, genout, listout;
    int n15, n16, n25, n31, switch_val, seg, segs, maxseg, Xseg, hapout, damout;
    int length, maxlen, minlen, prevmaxlen, maxn, found[2], known;
    int hapswap, segfirst, seglast, chips, chipn, imputed, status;
    char cthrds[3] = {0};
    char chro1_s[3], chroN_s[3];
    char sex;
    int64_t location, totlngth, maxlngth;
    int mosthap[4], mosthaps[4], search, usable;
    int maxped, loci, nloci, maxped2, mismatch, maxhap;
    int chrome, chrome1, chromes, idnum;
    int prevseg, prevsegs;
    int n13, n13a, hold, cross_arr[2], ncross, pnthap[2], where_arr[2][2];
    int maxl, maxloci, missing, anmlmiss, high1, nhigh, nmed, pedx[2];
    int loc, locus, nped, iter, iter1, iters, suggest[16], agree;
    int pstep, psegs, haplen, haptot, totlen;
    int64_t nped1;
    int nn, pnt, pnt1, pnt2, pnts, grdpnt, bday, nadd, nmale, ndrop;
    int XChrom, match_arr[2][2], matches;
    int maxprog, maxpnt, hap[2];
    float corr2, lenratio, errate;
    float haptotal;
    double totfreq, hapfrq;
    int notfound;
    bool exist, exist15, exist16, exist22;
    int gencode[6][6];
    int64_t genfreq[10];
    int threads, nthr;
    turbohap_reporting::Events report_events;
    report_events.phase("Input and workspace setup", "started");
    FINDHAP_PHASE(setup_trace, "setup");

    int* fields[] = {&iters, &XChrom, &maxlen, &minlen, &steps, &maxhap,
                     &hapout, &genout, &damout, &listout};
    for (size_t field = 0; field < options.controls.size(); ++field)
        *fields[field] = options.controls[field];
    errate = options.errate;
    if (turbohap_reporting::modern_stdout())
        turbohap_reporting::stdout_clock().print("Options: iterations %d; steps %d; maxhap %d; errate %.6f\n",
                                                iters, steps, maxhap, double(errate));
    if (maxlen < 1 || minlen < 1 || maxhap < 1 || steps < 1)
        input_error("segment lengths, maxhap and steps must be positive");
    if (maxhap == std::numeric_limits<int>::max())
        input_error("maxhap must leave room for the terminal index (maximum 2147483646)");
    legacy_printf("    iters,XChrom,maxlen,minlen,steps,maxhap,hapout,genout,damout,listout\n");
    legacy_printf("%7d%7d%7d%7d%7d%7d%7d%7d%7d%7d\n",
           iters, XChrom, maxlen, minlen, steps, maxhap, hapout, genout, damout, listout);
    legacy_printf("\n");
    char line[256];
    legacy_printf("%9.6f\n", errate);
    legacy_printf("    errate\n");
    legacy_printf("\n");

    // ------------------------------------------------------ Input data files
    FileHandle pedigree_file(fopen("pedigree.file", "r"), fclose);
    FileHandle genotype_file(fopen("genotypes.txt", "r"), fclose);
    FileHandle map_file(fopen("chromosome.data", "r"), fclose);
    FILE* fp10 = pedigree_file.get();
    FILE* fp13 = genotype_file.get();
    FILE* fp14 = map_file.get();
    if (!fp10 || !fp13 || !fp14) input_error("cannot open pedigree.file, genotypes.txt or chromosome.data for reading");
    configure_io_buffer(fp10); configure_io_buffer(fp13); configure_io_buffer(fp14);

    // Haplotype priors
    exist15 = std::filesystem::exists("hap.listpriors");
    exist16 = std::filesystem::exists("hap.foundpriors");
    exist22 = std::filesystem::exists("hap.Xseg");
    FileHandle hap_list_priors(exist15 ? fopen("hap.listpriors", "r") : nullptr, fclose);
    FileHandle hap_found_priors(exist16 ? fopen("hap.foundpriors", "r") : nullptr, fclose);
    FILE* fp15 = hap_list_priors.get();
    FILE* fp16 = hap_found_priors.get();
    configure_io_buffer(fp15); configure_io_buffer(fp16);

    if (exist15 && (!fp15 || !fp16 || !exist22))
        input_error("hap.listpriors requires readable hap.foundpriors and hap.Xseg");
    int prior_high1 = 0;
    if (exist15) {
        // Read the prior count before opening hap.Xseg as an output.
        FileHandle previous(fopen("hap.Xseg", "r"), fclose);
        if (!previous || fscanf(previous.get(), "%d", &prior_high1) != 1)
            input_error("invalid prior hap.Xseg");
    }

    // ---------------------------------------------------- Initialize variables
    baseyr = 19900101;
    today = 0;
    nbase = 0;
    missing = 5;

    // genotype code given 2 parent haplotype codes
    //           0 1 2   5
    int gc_init[6][6] = {
        {0,3,1,5,5,5},
        {3,5,4,5,5,5},
        {1,4,2,5,5,5},
        {5,5,5,5,5,5},
        {5,5,5,5,5,5},
        {5,5,5,5,5,5}
    };
    memcpy(gencode, gc_init, sizeof(gencode));
#if FINDHAP_OUTPUT_STREAMS
    alignas(64) uint8_t output_gencode[64] = {};
    for (int a = 0; a < 6; ++a)
        for (int b = 0; b < 6; ++b) output_gencode[6*a+b] = static_cast<uint8_t>(gencode[a][b]);
#endif

    // Default number of threads
    // Resolve once in the launcher before changing affinity. Standalone
    // engines retain the original OpenMP/argument semantics.
    {
        const auto request = findhap::engine_thread_request(argc, argv);
        nthr = request.openmp_limit;
        threads = request.effective;
    }
    if (argc > 1) legacy_printf("\n");

    legacy_printf(" Maximum number of processors set to    %d\n", threads);
    report_events.emit("run_start", 0, 0, 0, nullptr, 1, threads);
    legacy_printf(" Or set threads by command line argument: findhap 8\n");
    legacy_printf("\n");
    legacy_printf(" Number of processors available =       %d\n", nthr);
    legacy_printf(" Can be controlled using:  export OMP_NUM_THREADS=8\n");
    legacy_printf("\n");

    // ------------------------------------------- Get loci, chromes from chromosome.data
    loci = 0;
    chromes = 0;
    chrome1 = 99999;
    {
        findhap::input::MapReader map(fp14);
        findhap::input::MapRecord record;
        while (map.read(record)) {
            loci++;
            chrome1 = std::min(record.chromosome, chrome1);
            chromes = std::max(record.chromosome, chromes);
            chips = record.chips;
        }
    }
    legacy_printf(" %d loci on %d chromosomes read from chromosome.data\n", loci, chromes);
    rewind(fp14);
    const bool chromosome_processing = findhap::processing::by_chromosome;
    const int chromosome_count = chromes - chrome1 + 1;
    if (chromosome_processing && exist15 && chromosome_count > 1)
        input_error("chromosome processing needs a separate reference count per chromosome; "
                    "run each chromosome with its own prior files");
    if (XChrom == 0) XChrom = chromes + 1;
    if (XChrom < chromes) {
        status_printf(" %d X must be last chromosome %d\n", XChrom, chromes);
        exit(0); // The baseline uses a bare STOP for an invalid X chromosome.
    }

    // Allocate chromosome arrays
    loc1.reset(new int[(size_t)(chromes + 1)]);
    locN.reset(new int[(size_t)(chromes + 1)]);
    locate1.reset(new int[(size_t)(chromes + 1)]);
    locateN.reset(new int[(size_t)(chromes + 1)]);
    startseg.reset(new int[(size_t)(chromes + 1)]);
    stopseg.reset(new int[(size_t)(chromes + 1)]);
    chiplist_arr.reset(new int[(size_t)loci * (chips + 2)]); // (loci, 0:chips+1) → loci*(chips+2)
    memset(chiplist_arr.get(), 0, (size_t)loci * ((size_t)chips + 2) * sizeof(int));
    loclist.reset(new int[(size_t)(chips + 1)]);
    chiplen.reset(new int[(size_t)(chips + 1)]);

    memset(chiplen.get(), 0, (chips + 1) * sizeof(int));
    maxloci = 0;
    maxlngth = 0;
    for (int c = 1; c <= chromes; c++) { loc1[c - 1] = 99999999; locN[c - 1] = 0; }
    for (int c = 1; c <= chromes; c++) { locate1[c - 1] = 999999999; locateN[c - 1] = 0; }
    int total_loci = loci;  // Save final loci count for array strides
    loci = 0;

    // --------------------------------------------------- Read chromosome.data
    {
    findhap::input::MapReader map(fp14);
    findhap::input::MapRecord record;
    const int expected_chips = chips;
    while (map.read(record)) {
        if (record.chips != expected_chips || record.chromosome > chromes || loci >= total_loci)
            input_error("chromosome.data changed while loading the map");
        chrome = record.chromosome; nloci = record.within;
        loc = record.overall; location = record.position;
        loci++;
        for (chipn = 1; chipn <= chips; chipn++) {
            loclist[chipn - 1] = record.chip_loci[chipn - 1];
        }
        for (chipn = 1; chipn <= chips; chipn++) {
            if (loclist[chipn - 1] > 0) {
                // chiplist(loclist(chipn), chipn) = loci
                chiplist_arr[CHIPIDX(loclist[chipn - 1], chipn, total_loci)] = loci;
                chiplen[chipn - 1]++;
                if (chiplen[chipn - 1] != loclist[chipn - 1]) {
                    status_printf(" %d %d marker numbers disagree in chromosome.data, chip %d\n",
                           chiplen[chipn - 1], loclist[chipn - 1], chipn);
                    exit(1);
                }
            }
        }
        // chiplist(loci, 0) = loci; chiplist(loci, chips+1) = loci
        chiplist_arr[CHIPIDX(loci, 0, total_loci)] = loci;
        chiplist_arr[CHIPIDX(loci, chips + 1, total_loci)] = loci;
        loc1[chrome - 1] = std::min(loci, loc1[chrome - 1]);
        locN[chrome - 1] = std::max(loci, locN[chrome - 1]);
        locate1[chrome - 1] = std::min(location, static_cast<int64_t>(locate1[chrome - 1]));
        locateN[chrome - 1] = std::max(location, static_cast<int64_t>(locateN[chrome - 1]));
        maxloci = std::max(nloci, maxloci);
        maxlngth = std::max(maxlngth, static_cast<int64_t>(locateN[chrome - 1]) - locate1[chrome - 1] + 1);
        maxl = 0;
        for (int c = 1; c <= chips; c++) maxl = std::max(maxl, chiplen[c - 1]);
    }
    if (loci != total_loci) input_error("chromosome.data changed while loading the map");
    }
    legacy_printf("\n");
    legacy_printf(" %d missing genotype code\n", missing);

    // ----------------------------------------- Get maxped from pedigree.file
    maxped = 0;
    rewind(fp10);
    {
        findhap::PedigreeReader reader(fp10);
        findhap::PedigreeReader::Record record;
        while (reader.read(record)) {
            if (maxped == std::numeric_limits<int>::max() / 2)
                input_error("pedigree.file has too many records for the animal index");
            ++maxped;
        }
        if (!maxped) input_error("pedigree.file contains no animal records");
    }
    rewind(fp10);
    legacy_printf("\n");
    legacy_printf(" %d animals in pedigree.file (maxped)\n", maxped);

    ngen = 0;
    size_t genotype_entries = 0;
    size_t genotype_cache_bytes = 0;
    HaplotypeTextBuffers text_buffers(maxl);
    {
        FINDHAP_PHASE(genotype_count_trace, "genotype_count");
        // Validate in the existing counting pass; no extra full genotype scan.
        int tmp_id, tmp_chipn, tmp_loc;
        auto tmp_geno = std::make_unique<int8_t[]>(maxl);
        findhap::input::Validator input(fp13, maxl, chiplen.get(), chips, findhap::input::policy);
        while (input.read(tmp_id, tmp_chipn, tmp_loc, tmp_geno.get())) {
            if (ngen == std::numeric_limits<int>::max()) input_error("too many genotype records");
            ngen++;
            if ((size_t)tmp_loc > std::numeric_limits<size_t>::max() - genotype_entries)
                input_error("genotype cache is too large");
            genotype_entries += (size_t)tmp_loc;
            const size_t bytes = GenotypeCache::encoded_bytes(tmp_geno.get(), tmp_loc);
            if (bytes > std::numeric_limits<size_t>::max() - genotype_cache_bytes)
                input_error("genotype cache is too large");
            genotype_cache_bytes += bytes;
        }
        input.finish();
    }
    legacy_printf(" %d animals in genotypes.txt (ngen)\n", ngen);
    rewind(fp13);

    // --------------------------------------------------------------- allocate
    maxped2 = 2 * maxped;
    ped.reset(new int[(size_t)2 * maxped]);
    ids.reset(new int[(size_t)(maxped2)]);
    birth.reset(new int[(size_t)(maxped)]);
    called.reset(new int[(size_t)(maxped)]);
    animkey.reset(new int[(size_t)(maxped + 1)]);  // 0:maxped
    sexv.reset(new char[(size_t)(maxped)]);
    eqns.reset(new int[(size_t)(maxped2 + 1)]);    // 0:maxped2
    keyvec.reset(new int[(size_t)(maxped)]);
    haplotype.reset(new int8_t[(size_t)(loci)]);
    freq.reset(new float[(size_t)(loci)]);
    nfreq.reset(new int[(size_t)(loci)]);
    sumfreq_arr.reset(new int[(size_t)(loci)]);
    genotype.reset(new int8_t[(size_t)(loci)]);
    genovec.reset(new int8_t[(size_t)(maxl)]);
    missall.reset(new int[(size_t)(loci)]);
    listeqn.reset(new int[(size_t)(maxped2 + 1)]); // 0:maxped2
    missadd.reset(new int[(size_t)(loci)]);
    pntloc.reset(new int[(size_t)(maxped + 1)]);   // 0:maxped
    basefreq.reset(new float[(size_t)(loci)]);
    curfreq.reset(new float[(size_t)(loci)]);
    ofreq.reset(new int[(size_t)(loci)]);
    prog.reset(new int[(size_t)(maxped + 1)]);     // 0:maxped
    bfreq.reset(new int[(size_t)(loci)]);
    cfreq.reset(new int[(size_t)(loci)]);
    sumbase.reset(new int[(size_t)(loci)]);
    sumcurr.reset(new int[(size_t)(loci)]);
    startloc.reset(new int[(size_t)(loci + 1)]);   // 0:loci
    prevstartloc.reset(new int[(size_t)(loci + 1)]);
    gprog.reset(new int[(size_t)(maxped + 1)]);
    len_arr.reset(new int[(size_t)(loci + 1)]);    // 0:loci
    prevlen.reset(new int[(size_t)(loci + 1)]);
    chro.reset(new int[(size_t)(loci)]);
    linkREL.reset(new float[(size_t)(maxped + 1)]);
    patREL.reset(new float[(size_t)(maxped + 1)]);
    if (!chromosome_processing) high = std::make_unique<uint8_t[]>(maxped);
    chip_arr.reset(new int[(size_t)(maxped)]);

    memset(ofreq.get(), 0, loci * sizeof(int));
    memset(gprog.get(), 0, (maxped + 1) * sizeof(int));

    // ----------------------------------------------------- Read pedigree data
    memset(eqns.get(), 0, (maxped2 + 1) * sizeof(int));
    memset(listeqn.get(), 0, (maxped2 + 1) * sizeof(int));
    nhash(0, -1, ids.get(), maxped2, maxn);
    animkey[0] = 0;  // animkey(0) = 0
    nped = 0;
    {
    findhap::PedigreeReader reader(fp10);
    findhap::PedigreeReader::Record record;
    while (reader.read(record)) {
        if (nped >= maxped) input_error("pedigree.file changed while loading records");
        sex = record.sex; idnum = record.id; pnt1 = record.sire;
        pnt2 = record.dam; bday = record.birth;
        nped++;
        sexv[nped - 1] = sex;
        ped[IDX2(1, nped, 2)] = pnt1;
        ped[IDX2(2, nped, 2)] = pnt2;
        if (pnt1 <= 0) ped[IDX2(1, nped, 2)] = 0;
        if (pnt2 <= 0) ped[IDX2(2, nped, 2)] = 0;
        animkey[nped] = idnum;  // 1-based
        birth[nped - 1] = bday;
        if (bday > today) today = bday;
        if (bday < baseyr) nbase++;
        // Hash individual
        nn = nhash(idnum, 1, ids.get(), maxped2, maxn);
        if (maxn != nped) {
            status_printf(" Duplicate IDs in pedigree.file !\n");
            exit(1);
        }
        eqns[nn] = nped;
    }
    if (nped != maxped) input_error("pedigree.file changed while loading records");
    }
    legacy_printf("\n");

    // Current animals = last 5 years
    curryr = today - 50000;
    if (nbase < 10) baseyr = today;
    legacy_printf(" latest birthday in pedigree.file (today)     =  %d\n", today);
    legacy_printf(" current frequency includes animals born after   %d\n", curryr);
    legacy_printf("    base frequency includes animals born before  %d\n", baseyr);
    legacy_printf("\n");
    legacy_printf("      animal       sire         dam\n");

    // Hash parents
    memset(prog.get(), 0, (maxped + 1) * sizeof(int));
    maxprog = 0;

    for (i = 1; i <= nped; i++) {
        ped[IDX2(1, i, 2)] = eqns[nhash(ped[IDX2(1, i, 2)], 0, ids.get(), maxped2, maxn)];
        ped[IDX2(2, i, 2)] = eqns[nhash(ped[IDX2(2, i, 2)], 0, ids.get(), maxped2, maxn)];
    }

    // Count progeny
    for (i = 1; i <= nped; i++) {
        if (ped[IDX2(1, i, 2)] > 0) prog[ped[IDX2(1, i, 2)]]++;
        if (ped[IDX2(2, i, 2)] > 0) prog[ped[IDX2(2, i, 2)]]++;
        if (i <= 10) legacy_printf(" %d %d %d\n", animkey[i], animkey[ped[IDX2(1, i, 2)]], animkey[ped[IDX2(2, i, 2)]]);
    }
    high1 = prior_high1;

    // Get # animals in hap.foundpriors
    if (exist15) {
        skip_prior_headers(fp16);
        nprior = 0;
        std::string record;
        while (read_record(fp16, record)) {
            std::istringstream fields(record);
            if (!(fields >> idnum >> pnt >> pstep)) input_error("invalid hap.foundpriors record");
            if (pstep > 1) break;
            nprior++;
        }
        legacy_printf(" %d animals with data in hap.foundpriors\n", nprior / 2);
        rewind(fp16);
        legacy_printf("  %d high-density animals in prior data\n", high1);
    }
    if (maxlen == minlen) steps = 1;

    // ----------------------------------------------- Cache genotype file in memory
    // Preserve parsed chip records without whole-genome padding. Four-state
    // records are packed losslessly; other values retain their original bytes.
    genotype_cache = std::make_unique<ActiveGenotypeCache>(ngen, genotype_cache_bytes);
    cache_chipn.reset(new int[(size_t)(ngen)]);
    cache_id.reset(new int[(size_t)(ngen)]);
    cache_loc.reset(new int[(size_t)(ngen)]);
    cache_anml_idx.reset(new int[(size_t)(maxped + 1)]);
    cache_ngen = 0;
    memset(cache_anml_idx.get(), 0, (maxped + 1) * sizeof(int));

    rewind(fp13);
    size_t cache_next = 0;
    {
    FINDHAP_PHASE(genotype_cache_trace, "genotype_cache");
    findhap::input::Validator input(fp13, maxl, chiplen.get(), chips, findhap::input::policy, false);



    while (input.read(idnum, chipn, loc, genovec.get())) {
        if (cache_ngen >= ngen || (size_t)loc > genotype_entries - cache_next)
            input_error("genotypes.txt changed while loading the cache");
        cache_ngen++;
        cache_id[cache_ngen - 1] = idnum;
        cache_chipn[cache_ngen - 1] = chipn;
        cache_loc[cache_ngen - 1] = loc;
        genotype_cache->append(genovec.get(), loc);
        cache_next += (size_t)loc;
        nn = nhash(idnum, 0, ids.get(), maxped2, maxn);
        if (nn > 0 && eqns[nn] > 0) {
            // The validator accepts one record per physical line and rejects blanks.
            int& first_row = cache_anml_idx[eqns[nn]];
            if (first_row != 0) duplicate_genotype(idnum, first_row, cache_ngen);
            first_row = cache_ngen;
        }
    }
    input.finish();
    }
    if (cache_ngen != ngen || cache_next != genotype_entries || !genotype_cache->complete())
        input_error("genotypes.txt changed while loading the cache");
#ifdef FINDHAP_LOW_MEMORY_PLUS
    genotype_cache->finish_append();
    legacy_printf("  %d genotype records cached on disk (bounded input batches)\n", cache_ngen);
#else
    legacy_printf("  %d genotype records cached in memory\n", cache_ngen);
#endif

    // Use the actual rounded schedule: repeated rounding may finish below
    // minlen, and the reference accepts increasing schedules as well.
    std::vector<int> step_lengths(steps, maxlen);
    if (steps > 1) {
        lenratio = powf(1.0f * maxlen / minlen, 1.0f / (steps - 1));
        for (int stage = 1; stage < steps; ++stage) {
            float length = std::round(step_lengths[stage - 1] / lenratio);
            if (!std::isfinite(length) || length < 1 || length >= 2147483648.0f)
                input_error("rounded segment length is outside the supported range");
            step_lengths[stage] = static_cast<int>(length);
        }
    }
    using findhap::checked_array_count;
    std::vector<uint8_t> chromosome_high;
    std::vector<int> locus_chromosome, chromosome_called;
    std::vector<uint8_t> chromosome_retained;
    if (chromosome_processing) {
        chromosome_high.resize(checked_array_count<uint8_t>("chromosome coverage", maxped, chromosome_count));
        locus_chromosome.resize(loci);
        for (int c = chrome1; c <= chromes; ++c) {
            if (locN[c - 1] == 0) continue;
            std::fill(locus_chromosome.begin() + loc1[c - 1] - 1,
                      locus_chromosome.begin() + locN[c - 1], c - chrome1);
        }
        status_printf(" Coverage and output quality are calculated per chromosome\n");
    }
    int abs_maxseg = 0;
    size_t haplist_strides = 0;
    for (int length : step_lengths) {
        int segments = 0;
        for (int chromosome = chrome1; chromosome <= chromes; ++chromosome) {
            const int last = locN[chromosome - 1];
            const int first = std::min(loc1[chromosome - 1], last);
            const int added = 1 + (last - first) / length;
            if (added > std::numeric_limits<int>::max() - 1 - segments)
                input_error("segment count exceeds the supported index range");
            segments += added;
        }
        abs_maxseg = std::max(abs_maxseg, segments);
        haplist_strides = std::max(haplist_strides,
            checked_array_count<int8_t>("haplotype library", segments, length));
    }
    const size_t haplist_capacity_bytes = checked_array_count<int8_t>("haplotype library", haplist_strides, maxhap);
    const size_t hapfreq_entries = checked_array_count<int>("haplotype frequencies", maxhap, abs_maxseg);
    const size_t nhap_entries = checked_array_count<int>("segment counters", abs_maxseg, FINDHAP_NHAP_STRIDE);
    const size_t assignment_entries = checked_array_count<HaplotypeId>("haplotype assignments", size_t{2} * maxped, abs_maxseg);
    const size_t inheritance_entries = checked_array_count<InheritancePair>("inheritance codes", maxped, abs_maxseg);
    const size_t progeny_stride = size_t(maxhap) + 1;
    const size_t order_entries = checked_array_count<int>("haplotype order", progeny_stride, abs_maxseg);
    const size_t crossover_bytes = checked_array_count<char>("crossover output", 80 * 10, abs_maxseg);
    const size_t progeny_scratch_entries = checked_array_count<int>("progeny scratch", progeny_stride, threads);

    // The genotype record mapping is immutable from this point. A parent
    // with a mapped record will have listeqn > 0 in every progeny pass.
    constexpr bool compact_progeny = FINDHAP_COMPACT_PROGENY;
    int totprog = 1;
    for (i = 1; i <= nped; i++) {
        if (!compact_progeny || cache_anml_idx[i] == 0) totprog += prog[i];
    }
    const size_t progeny_entries = checked_array_count<HaplotypeId>("progeny haplotypes", totprog, abs_maxseg);

    // Validate input and workspace dimensions before opening legacy results.
    // ----------------------------------------------------- Output data files
    std::unique_ptr<ChromosomeResultDirectory> result_directory;
    RunOutputs outputs;
    ResultFiles legacy_results;
    std::vector<ResultFiles> chromosome_results;
    if (chromosome_processing) {
        result_directory = std::make_unique<ChromosomeResultDirectory>();
        chromosome_results.resize(chromosome_count);
        for (int c = chrome1; c <= chromes; ++c) {
            if (locN[c - 1] == 0) continue;
            const auto directory = result_directory->staging() / ("chr" + std::to_string(c));
            std::filesystem::create_directory(directory);
            chromosome_results[c - chrome1] = open_result_files(outputs, directory, genout, hapout, listout);
        }
    } else {
        legacy_results = open_result_files(outputs, {}, genout, hapout, listout);
    }
    FILE* fp22 = legacy_results.reference;
    FILE* fp23 = legacy_results.rejected;
    FILE* fp24 = legacy_results.quality;
    FILE* fp25 = legacy_results.library;
    FILE* fp26 = legacy_results.assignments;
    FILE* fp27 = legacy_results.crossovers;
    FILE* fp28 = legacy_results.inheritance;
    FILE* fp30 = legacy_results.allele_frequency;
    FILE* fp29 = legacy_results.haplotypes;
    FILE* fp31 = legacy_results.genotypes;

    // Reuse the workspace across steps. Each step uses its own count and stride.
#if FINDHAP_HAPLIST_INIT == 3
    haplist_arr.allocate(haplist_capacity_bytes);
#else
    haplist_arr.reset(new int8_t[haplist_capacity_bytes]);
#endif
#if FINDHAP_LIBRARY_METADATA
    hapfreq_arr.allocate(hapfreq_entries);
#else
    hapfreq_arr.reset(new int[hapfreq_entries]);
#endif
    nhap_arr.reset(new int[nhap_entries]);
    keephap.reset(new HaplotypeId[assignment_entries]);
    hapseg_arr.reset(new int8_t[(size_t)2 * loci]);
    proghaps.reset(new int[progeny_stride]);
    patmat.reset(new InheritancePair[inheritance_entries]);
#if FINDHAP_LIBRARY_METADATA
    order_arr.allocate(order_entries);
#else
    order_arr.reset(new int[order_entries]);
#endif
    phaps.reset(new int[(size_t)abs_maxseg]);
    auto cross_buf = std::make_unique<char[]>(crossover_bytes);
    auto num_cross = std::make_unique<int[]>(abs_maxseg);

    // Reused by the progeny phase in every step.
    auto l_proghaps_all = std::make_unique<int[]>(progeny_scratch_entries);
    fromprog.reset(new HaplotypeId[progeny_entries]);

    // ------------------------------------- Start with maxlen, end with minlen
    FINDHAP_END(setup_trace);
    if (turbohap_reporting::modern_stdout())
        turbohap_reporting::stdout_clock().print("Inputs: %d pedigree animals; %d genotype records; %d loci; %d chromosomes\n",
                                                maxped, cache_ngen, loci, chromes);
    report_events.phase("Input and workspace setup", "completed");
#if FINDHAP_PEDIGREE_PLACEMENT
    const auto pedigree_assignment = findhap::pedigree_placement::requested_mode(threads, chromes - chrome1 + 1);
    const auto pedigree_cores = findhap::pedigree_placement::fixed_cores(threads, pedigree_assignment);
    const auto pedigree_rates = pedigree_assignment == findhap::pedigree_placement::Mode::weighted
        ? findhap::pedigree_placement::relative_rates(pedigree_cores) : std::vector<double>{};
#endif
#if FINDHAP_MMAP_PHASE_BUFFERS && !defined(FINDHAP_LOW_MEMORY)
    findhap::NativePhaseBufferPool<uint8_t> genotype_workspace_pool;
#endif
#if FINDHAP_MMAP_PHASE_BUFFERS && FINDHAP_PHASE_LM_WORKER_REUSE && defined(FINDHAP_LOW_MEMORY_PLUS)
    findhap::NativePhaseBufferBank<uint8_t> genotype_worker_pools;
#endif
    for (step = 1; step <= steps; step++) {
        FINDHAP_PHASE(step_trace, "step", step);
        report_events.emit("step_start", step);
        if (step == 1) {
            legacy_printf("\n");
            legacy_printf(" Begin with maxlen  %d\n", maxlen);
        } else {
            // Report previous maxhap
            maxnhap = 0;
            for (seg = 1; seg <= maxseg; seg++) {
                if (nhap_arr[(seg - 1) * FINDHAP_NHAP_STRIDE] > maxnhap) {
                    maxnhap = nhap_arr[(seg - 1) * FINDHAP_NHAP_STRIDE];
                    maxnseg = seg;
                }
            }
            legacy_printf("\n");
            legacy_printf("%8d most haplotypes found in segment %8d on chromosome %5d\n",
                   maxnhap, maxnseg, chro[maxnseg - 1]);
            prevmaxlen = maxlen;
            maxlen = step_lengths[step - 1];
            legacy_printf("\n");
            legacy_printf(" Step  %d change maxlen to  %d\n", step, maxlen);
            // Store previous haplotypes
            prevsegs = maxseg;
#if FINDHAP_SPLIT_TYPED_SNAPSHOT
            // Genotype presence is fixed. Expansion reads the typed rows;
            // later rematching reads only the untyped rows.
            if (previous_row.empty()) {
                previous_row.resize(size_t(nped) + 1);
                for (int animal = 1; animal <= nped; ++animal) {
                    previous_row[animal] = listeqn[animal] > 0
                        ? ++previous_typed_rows : -(++previous_untyped_rows);
                }
            }
            prev_typed_assignments = findhap::make_phase_buffer<HaplotypeId>(
                size_t(2) * previous_typed_rows * prevsegs);
            prevkeephap.reset();
            prevkeephap = findhap::make_phase_buffer<HaplotypeId>(
                size_t(2) * previous_untyped_rows * prevsegs);
#elif FINDHAP_MMAP_PHASE_BUFFERS && FINDHAP_PHASE_STD_SNAPSHOT_REUSE > 0 && !defined(FINDHAP_LOW_MEMORY)
            prevkeephap.prepare((size_t)2 * maxped * prevsegs);
#else
            prevkeephap = findhap::make_phase_buffer<HaplotypeId>((size_t)2 * maxped * prevsegs);
#endif
            // The output pass changes nhap_arr to a count of used haplotypes,
            // but their IDs can have gaps. Copy through the largest referenced
            // ID in each segment, preserving those IDs and all required alleles.
            prevhap_offsets.resize(prevsegs);
            std::vector<size_t> previous_sizes(prevsegs);
            size_t previous_bytes = 0;
#if FINDHAP_SPLIT_TYPED_SNAPSHOT
            #pragma omp parallel for schedule(static) num_threads(threads)
            for (int segment = 1; segment <= prevsegs; ++segment) {
                const HaplotypeId* source = keephap.get() + KHIDX(1, 1, segment, maxped);
                HaplotypeId* typed = prev_typed_assignments.get();
                HaplotypeId* untyped = prevkeephap.get();
                if (previous_typed_rows) typed += KHIDX(1, 1, segment, previous_typed_rows);
                if (previous_untyped_rows) untyped += KHIDX(1, 1, segment, previous_untyped_rows);
                int highest_id = 0;
                for (int animal = 1; animal <= nped; ++animal, source += 2) {
                    HaplotypeId*& destination = previous_row[animal] > 0 ? typed : untyped;
                    *destination++ = source[0];
                    *destination++ = source[1];
                    highest_id = std::max({highest_id, int(source[0]), int(source[1])});
                }
                previous_sizes[segment - 1] = size_t(prevmaxlen) * highest_id;
            }
            for (int ss = 0; ss < prevsegs; ++ss) {
                const size_t bytes = previous_sizes[ss];
#else
            for (int ss = 0; ss < prevsegs; ++ss) {
                int highest_id = 0;
                size_t first = (size_t)ss * 2 * maxped;
                for (size_t entry = 0; entry < (size_t)2 * maxped; ++entry) {
                    int id = keephap[first + entry];
                    prevkeephap[first + entry] = id;
                    highest_id = std::max(highest_id, id);
                }
                const size_t bytes = (size_t)prevmaxlen * highest_id;
                previous_sizes[ss] = bytes;
#endif
                prevhap_offsets[ss] = previous_bytes;
                if (bytes > std::numeric_limits<size_t>::max() - previous_bytes)
                    input_error("previous haplotype storage is too large");
                previous_bytes += bytes;
            }
#if FINDHAP_MMAP_PHASE_BUFFERS && FINDHAP_PHASE_STD_SNAPSHOT_REUSE > 0 && !defined(FINDHAP_LOW_MEMORY)
            prevhaplist.prepare(previous_bytes);
#else
            prevhaplist.reset();
            prevhaplist = findhap::make_phase_buffer<int8_t>(previous_bytes);
#endif
            memcpy(prevlen.get(), len_arr.get(), (loci + 1) * sizeof(int));
#if FINDHAP_SPLIT_TYPED_SNAPSHOT
            #pragma omp parallel for schedule(static) num_threads(threads)
#endif
            for (int ss = 0; ss < prevsegs; ++ss) {
                memcpy(prevhaplist.get() + prevhap_offsets[ss],
                       haplist_arr.get() + (size_t)ss * maxhap * prevmaxlen,
                       previous_sizes[ss]);
            }
            memcpy(prevstartloc.get(), startloc.get(), (loci + 1) * sizeof(int));
        }

        if (turbohap_reporting::modern_stdout())
            turbohap_reporting::stdout_clock().print(
                "Step %d of %d: maximum segment length %d loci\n", step, steps, maxlen);

        // Divide chromosomes into segments
        legacy_printf("\n");
        legacy_printf("  chromosome      length     markers    segments\n");
        Xseg = loci;
        maxseg = 0;
        totlngth = 0;
        startloc[0] = 0;
        len_arr[0] = 1;
        for (chrome = chrome1; chrome <= chromes; chrome++) {
            if (loc1[chrome - 1] > locN[chrome - 1]) loc1[chrome - 1] = locN[chrome - 1];
            segs = 1 + (locN[chrome - 1] - loc1[chrome - 1]) / maxlen;
            for (seg = maxseg + 1; seg <= maxseg + segs; seg++) {
                startloc[seg] = startloc[seg - 1] + len_arr[seg - 1];
                len_arr[seg] = (locN[chrome - 1] - startloc[seg] + 1) / (maxseg + segs - seg + 1);
                chro[seg - 1] = chrome;
            }
            if (chrome == XChrom) Xseg = maxseg + 1;
            startseg[chrome - 1] = maxseg + 1;
            stopseg[chrome - 1] = maxseg + segs;
            maxseg += segs;
            legacy_printf("%12d%12ld%12d%12d\n", chrome,
                   (long)(locateN[chrome - 1] - locate1[chrome - 1] + 1),
                   locN[chrome - 1] - loc1[chrome - 1] + 1, segs);
            totlngth += locateN[chrome - 1] - locate1[chrome - 1] + 1;
        }
        std::vector<uint8_t> local_x_context;
        std::vector<uint8_t> local_legacy_maternal;
        if (chromosome_processing) {
            local_x_context.resize(size_t(maxseg) + 1);
            local_legacy_maternal.resize(chromosome_count);
            for (int c = chrome1; c <= chromes; ++c) {
                if (locN[c - 1] == 0) continue;
                const int first = startseg[c - 1], last = stopseg[c - 1];
                const int markers = locN[c - 1] - loc1[c - 1] + 1;
                // A standalone autosome uses its locus count as the no-X
                // sentinel. When every segment is one locus, it is reachable.
                const int segments = last - first + 1;
                if (c == XChrom)
                    std::fill(local_x_context.begin() + first, local_x_context.begin() + last + 1, 1);
                else if (markers == segments)
                    local_x_context[last] = 1;
                // The legacy progeny check uses the preceding loop's endpoint.
                local_legacy_maternal[c - chrome1] = c == XChrom || segments + 1 >= markers;
            }
        }
        const auto uses_x_context = [&](int s) {
            return chromosome_processing ? local_x_context[s] != 0 : s >= Xseg;
        };
        std::vector<FILE*> crossover_destinations;
        if (chromosome_processing && step == steps) {
            crossover_destinations.resize(maxseg);
            for (int s = 1; s <= maxseg; ++s)
                crossover_destinations[s - 1] = chromosome_results[chro[s - 1] - chrome1].crossovers;
        }
        legacy_printf("\n");
        legacy_printf(" %d total segments across chromosomes\n", maxseg);
        if (step == 1) {
            legacy_printf(" %d maximum loci on individual chromosome\n", maxloci);
            legacy_printf(" %ld physical length of longest chromosome\n", (long)maxlngth);
            legacy_printf(" %ld total length of genome processed\n", (long)totlngth);
        }
        legacy_printf("\n");

#if FINDHAP_PEDIGREE_PLACEMENT
        const auto pedigree_cuts = findhap::pedigree_placement::weighted_cuts(len_arr.get() + 1,
                                                                             maxseg, pedigree_rates);
#endif
        report_events.phase("Library initialization", "started", step);

        FINDHAP_PHASE(initialization_trace, "step_initialization", step);
#if FINDHAP_HAPLIST_INIT == 3
        if (step > 1) haplist_arr.allocate(haplist_capacity_bytes);
#endif
        findhap_haplotype_init::begin_step(haplist_arr.get(), haplist_capacity_bytes);
#if FINDHAP_LIBRARY_METADATA
        if(step>1){hapfreq_arr.allocate(size_t(maxhap)*abs_maxseg);order_arr.allocate((size_t(maxhap)+1)*abs_maxseg);}
#endif
        // Initialize arrays in parallel to avoid single-thread memory bandwidth bottleneck
#if defined(__APPLE__) && defined(__aarch64__)
        #pragma omp parallel num_threads(threads)
        {
            findhap::macos::worker_enter();
            #pragma omp for schedule(static)
#else
        #pragma omp parallel for schedule(static) num_threads(threads)
#endif
        for (seg = 1; seg <= maxseg; seg++) {
            findhap_haplotype_init::clear_capacity(
                &haplist_arr[((size_t)seg - 1) * maxhap * maxlen], (size_t)maxhap * maxlen);
#if !FINDHAP_LIBRARY_METADATA
            memset(&hapfreq_arr[((size_t)seg - 1) * maxhap], 0, (size_t)maxhap * sizeof(int));
#endif
        }
#if defined(__APPLE__) && defined(__aarch64__)
        }
#endif
        memset(nhap_arr.get(), 0, (size_t)maxseg * FINDHAP_NHAP_STRIDE * sizeof(int));
        memset(keephap.get(), 0, (size_t)2 * maxped * maxseg * sizeof(HaplotypeId));
        memset(hapseg_arr.get(), 0, 2 * loci * sizeof(int8_t));
        memset(phaps.get(), 0, maxseg * sizeof(int));

#if defined(__APPLE__) && defined(__aarch64__)
        #pragma omp parallel num_threads(threads)
        {
            findhap::macos::worker_enter();
            #pragma omp for schedule(static)
#else
        #pragma omp parallel for schedule(static) num_threads(threads)
#endif
        for (seg = 1; seg <= maxseg; seg++) {
            std::fill_n(patmat.get() + PMIDX(1, seg, maxped), size_t(maxped),
                        InheritancePair::both(InheritanceCode::no_parent));
#if !FINDHAP_LIBRARY_METADATA
            for (int jj = 0; jj <= maxhap; jj++) {
                order_arr[ORDIDX(jj, seg, maxhap + 1)] = 0;
            }
#endif
        }
#if defined(__APPLE__) && defined(__aarch64__)
        }
#endif

        FINDHAP_END(initialization_trace);
        report_events.phase("Library initialization", "completed", step);

        // Read each step in file order, remapping original haplotype numbers
        // to their loaded slots before resetting the population search order.
        if (exist15) {
            if (step == 1) skip_prior_headers(fp15);
            int previous_segment = 0, loaded = 0, total_length = 0, slot = 0;
            std::string record;
            while (read_record(fp15, record)) {
                int prior_step = prior_field<int>(record, 33, 3);
                // The reference consumes the end-of-step sentinel and stops here.
                if (prior_step > step) break;
                int original_hap = prior_field<int>(record, 20, 10);
                int prior_segment = prior_field<int>(record, 36, 7);
                int start = prior_field<int>(record, 43, 8);
                int prior_length = prior_field<int>(record, 51, 6);
                double prior_frequency = prior_field<double>(record, 0, 10);
                if (prior_segment < 1 || prior_segment > maxseg || original_hap < 1 ||
                    original_hap > maxhap || prior_length < 1 || prior_length > maxlen ||
                    start < 1 || start > loci - prior_length + 1 ||
                    record.size() < 58 + (size_t)prior_length)
                    input_error("hap.listpriors dimensions disagree with options/chromosome.data");
                if (prior_segment != previous_segment) {
                    slot = 0;
                    previous_segment = prior_segment;
                    total_length += prior_length;
                }
                if (++slot > maxhap) input_error("prior haplotypes exceed maxhap");
                startloc[prior_segment] = start;
#if FINDHAP_HAPLIST_INIT
                // Prior records may be shorter than the stride. Preserve the
                // original zero padding for every published row.
                int8_t* prior_row = &haplist_arr[IDX3(1, slot, prior_segment, maxlen, maxhap)];
                findhap_haplotype_init::publish_row(prior_row, maxlen);
                std::memset(prior_row, 0, size_t(maxlen));
#endif
                for (int ll = 0; ll < prior_length; ++ll) {
                    char value = record[58 + ll];
                    if (value < '0' || value > '2') input_error("invalid haplotype digit in priors");
                    haplist_arr[IDX3(ll + 1, slot, prior_segment, maxlen, maxhap)] = value - '0';
                }
                hapfreq_arr[IDX2(slot, prior_segment, maxhap)] = std::lround(prior_frequency * nprior / 100.0);
                order_arr[ORDIDX(original_hap, prior_segment, maxhap + 1)] = slot;
                nhap_arr[(prior_segment - 1) * FINDHAP_NHAP_STRIDE] = std::max(slot, nhap_arr[(prior_segment - 1) * FINDHAP_NHAP_STRIDE]);
                ++loaded;
            }
            legacy_printf(" %d records read from hap.listpriors (unit 15)\n", loaded);
            if (total_length != loci) input_error("locus counts in chromosome.data and hap.listpriors disagree");
            if (step == 1) skip_prior_headers(fp16);
            loaded = 0;
            while (read_record(fp16, record)) {
                std::istringstream fields(record);
                int prior_id, parent, prior_step, segments;
                if (!(fields >> prior_id >> parent >> prior_step >> segments))
                    input_error("invalid hap.foundpriors header");
                if (prior_step > step) break;
                if (parent < 1 || parent > 2 || segments != maxseg)
                    input_error("hap.foundpriors parent/segment count is invalid");
                int animal = eqns[nhash(prior_id, 0, ids.get(), maxped2, maxn)];
                for (int ss = 1; ss <= segments; ++ss) {
                    int original_hap;
                    if (!(fields >> original_hap) || original_hap < 0 || original_hap > maxhap)
                        input_error("invalid haplotype number in hap.foundpriors");
                    if (animal > 0)
                        keephap[KHIDX(parent, animal, ss, maxped)] = order_arr[ORDIDX(original_hap, ss, maxhap + 1)];
                }
                ++loaded;
            }
            legacy_printf(" %d records read from hap.foundpriors (unit 16)\n", loaded);
        }

        // Initialize sort order
        for (seg = 1; seg <= maxseg; seg++) {
#if FINDHAP_LIBRARY_METADATA
            const int loaded=nhap_arr[(seg-1)*FINDHAP_NHAP_STRIDE];
            const int published=int(std::min(size_t(maxhap),((size_t(loaded)+FINDHAP_METADATA_CHUNK-1)/FINDHAP_METADATA_CHUNK)*FINDHAP_METADATA_CHUNK));
#else
            const int published=maxhap;
#endif
            for (i = 1; i <= published; i++) {
                order_arr[ORDIDX(i, seg, maxhap + 1)] = i;
            }
        }

        // Initialize variables
        switch_val = 1;
        memset(suggest, 0, sizeof(suggest));
        memset(chip_arr.get(), 0, maxped * sizeof(int));
        memset(proghaps.get(), 0, (maxhap + 1) * sizeof(int));
        memset(fromprog.get(), 0, (size_t)totprog * maxseg * sizeof(HaplotypeId));
        memset(nfreq.get(), 0, loci * sizeof(int));
        memset(bfreq.get(), 0, loci * sizeof(int));
        memset(cfreq.get(), 0, loci * sizeof(int));
        memset(genfreq, 0, sizeof(genfreq));
        memset(sumfreq_arr.get(), 0, loci * sizeof(int));
        memset(sumbase.get(), 0, loci * sizeof(int));
        memset(sumcurr.get(), 0, loci * sizeof(int));

        // ----------------------------------------------- Iterate to find haplotypes
        if (iters < 3) {
            legacy_printf("    iters set to minimum of 3 (was%4d)\n", iters);
            iters = 3;
        }
        legacy_printf("\n");
        iter1 = 1;
        if (exist15) iter1 = iters;

#if FINDHAP_PEDIGREE_REMATCH == 2
        pedigree_rematch::Plan rematch_plan;
        if (step > 1 && !exist15)
            rematch_plan = pedigree_rematch::make_plan(startloc.get(), len_arr.get(), maxseg,
                                                      prevstartloc.get(), prevlen.get(), prevsegs);
#endif

        // ---- Pre-expand genotypes ----
        report_events.phase("Genotype expansion", "started", step);
        FINDHAP_PHASE(expansion_trace, "expansion", step);
        memset(called.get(), 0, maxped * sizeof(int));
#ifdef FINDHAP_LOW_MEMORY_PLUS
#if FINDHAP_MMAP_PHASE_BUFFERS && FINDHAP_PHASE_LM_WORKER_REUSE
        BoundedGenotypeWorkspace cache_expanded(cache_ngen, loci, startloc.get(), len_arr.get(), maxseg, chro.get(),
                                                64 * 1024 * 1024, 128 * 1024 * 1024, &genotype_worker_pools);
#else
        BoundedGenotypeWorkspace cache_expanded(cache_ngen, loci, startloc.get(), len_arr.get(), maxseg, chro.get());
#endif
#elif defined(FINDHAP_LOW_MEMORY)
        ChromosomeWorkspace cache_expanded(cache_ngen, loci, startloc.get(), len_arr.get(), maxseg, chro.get());
#else
#if FINDHAP_MMAP_PHASE_BUFFERS
        GenotypeWorkspace cache_expanded(cache_ngen, loci, startloc.get(), len_arr.get(), maxseg,
                                          nullptr, 64 * 1024 * 1024, &genotype_workspace_pool);
#else
        GenotypeWorkspace cache_expanded(cache_ngen, loci, startloc.get(), len_arr.get(), maxseg);
#endif
#endif
        const size_t expansion_loci_stride = worker_stride(loci), expansion_raw_stride = worker_stride(maxl);
        auto expanded_scratch = std::make_unique<int8_t[]>(expansion_loci_stride * threads);
        auto raw_scratch = std::make_unique<int8_t[]>(expansion_raw_stride * threads);
        int expanded_nhigh = 0;
        const bool count_chromosome_calls = chromosome_processing && step == steps;
        if (count_chromosome_calls)
            chromosome_called.resize(checked_array_count<int>("chromosome observed calls", maxped, chromosome_count));
        int* p_sumfreq = sumfreq_arr.get();
        int* p_nfreq = nfreq.get();
        int* p_sumcurr = sumcurr.get();
        int* p_cfreq = cfreq.get();
        int* p_ofreq = ofreq.get();
        int* p_birth = birth.get();
        uint8_t* p_high = high.get();
        if (!cache_called_cnt) cache_called_cnt.reset(new int[(size_t)(cache_ngen)]);
        if (!cache_eqns_nn) cache_eqns_nn.reset(new int[(size_t)(cache_ngen)]);
        memset(cache_eqns_nn.get(), 0, cache_ngen * sizeof(int));

        int* __restrict__ p_cache_called_cnt = cache_called_cnt.get();
        int* __restrict__ p_cache_eqns_nn = cache_eqns_nn.get();
        const int* __restrict__ p_cache_id = cache_id.get();
        const int* __restrict__ p_cache_chipn = cache_chipn.get();
        const int* __restrict__ p_cache_loc = cache_loc.get();
        int* __restrict__ p_ids = ids.get(); // mutable for nhash
        const int* __restrict__ p_chiplen = chiplen.get();
        const int* __restrict__ p_eqns = eqns.get();
        int* __restrict__ p_chip_arr = chip_arr.get();
        int* __restrict__ p_called = called.get();
        const int* __restrict__ p_chiplist = chiplist_arr.get();
#if FINDHAP_SPLIT_TYPED_SNAPSHOT
        const HaplotypeId* __restrict__ p_prevkeephap = prev_typed_assignments.get();
        const int typed_snapshot_rows = previous_typed_rows;
#else
        const HaplotypeId* __restrict__ p_prevkeephap = prevkeephap.get();
        const int typed_snapshot_rows = maxped;
#endif
        const int8_t* __restrict__ p_prevhaplist = prevhaplist.get();
        const size_t* __restrict__ p_prevhap_offsets = prevhap_offsets.data();
        const int* __restrict__ p_prevlen = prevlen.get();
        const int* __restrict__ p_prevstartloc = prevstartloc.get();

        for (size_t input_first = 0; input_first < (size_t)cache_ngen;) {
#ifdef FINDHAP_LOW_MEMORY_PLUS
        const size_t input_count = genotype_cache->prepare_sequential(input_first,
            std::min(cache_expanded.input_batch_size(), (size_t)cache_ngen - input_first));
        WorkerFailure expansion_failure;
#else
        const size_t input_count = std::min(cache_expanded.input_batch_size(), (size_t)cache_ngen - input_first);
#endif
        cache_expanded.begin_input(input_first, input_count);
#if defined(__APPLE__) && defined(__aarch64__)
        #pragma omp parallel private(n, idnum, chipn, loc, nn, locus, seg, pnt, l, hap, anmlmiss) reduction(+: expanded_nhigh, p_sumfreq[:loci], p_nfreq[:loci], p_sumcurr[:loci], p_cfreq[:loci], p_ofreq[:loci], genfreq[:6]) num_threads(threads)
        {
            findhap::macos::worker_enter();
            #pragma omp for schedule(FINDHAP_ROW_SCHEDULE)
#else
        #pragma omp parallel for private(n, idnum, chipn, loc, nn, locus, seg, pnt, l, hap, anmlmiss) \
                reduction(+: expanded_nhigh, p_sumfreq[:loci], p_nfreq[:loci], p_sumcurr[:loci], p_cfreq[:loci], p_ofreq[:loci], genfreq[:6]) \
                schedule(FINDHAP_ROW_SCHEDULE) num_threads(threads)
#endif
        for (n = (int)input_first + 1; n <= (int)(input_first + input_count); n++) {
            int8_t* expanded = expanded_scratch.get() + (size_t)omp_get_thread_num() * expansion_loci_stride;
            int l_idnum = p_cache_id[n - 1];
            int l_chipn = p_cache_chipn[n - 1];
            int l_loc = p_cache_loc[n - 1];
            int l_nn = nhash(l_idnum, 0, p_ids, maxped2, maxn);
            if (l_nn == 0) {
                p_cache_eqns_nn[n - 1] = 0;
                memset(expanded, missing, loci);
                p_cache_called_cnt[n - 1] = 0;
#ifdef FINDHAP_LOW_MEMORY
                cache_expanded.store_row(n - 1, expanded);
#endif
                continue;
            }
            if (p_chiplen[l_chipn - 1] != l_loc) {
                #pragma omp critical
                {
                    status_printf(" Chip lengths differ in genotypes.txt vs chromosome.data: %d %d  for chip %d  animal %d\n",
                           p_chiplen[l_chipn - 1], l_loc, l_chipn, l_idnum);
                    exit(1);
                }
            }
            p_cache_eqns_nn[n - 1] = p_eqns[l_nn];
            if (p_eqns[l_nn] > 0) p_chip_arr[p_eqns[l_nn] - 1] = l_chipn;
            // Pad low density to max loci
            memset(expanded, missing, loci);
            p_cache_called_cnt[n - 1] = 0;
            const int8_t* raw_genotype;
#ifdef FINDHAP_LOW_MEMORY_PLUS
            try {
#endif
                raw_genotype = genotype_cache->row(n - 1, l_loc,
                    raw_scratch.get() + (size_t)omp_get_thread_num() * expansion_raw_stride);
#ifdef FINDHAP_LOW_MEMORY_PLUS
            } catch (...) {
                expansion_failure.capture();
                continue;
            }
#endif
            const auto copy_calls = [&](int* observed_by_chromosome, auto local_counts) {
                for (int ll = 1; ll <= l_loc; ll++) {
                    int cl = p_chiplist[CHIPIDX(ll, l_chipn, loci)];
                    if (cl < 1 || cl > loci) continue;
                    expanded[cl - 1] = raw_genotype[ll - 1];
                    if (raw_genotype[ll - 1] <= 2) {
                        p_cache_called_cnt[n - 1]++;
                        if constexpr (decltype(local_counts)::value)
                            ++observed_by_chromosome[locus_chromosome[cl - 1]];
                    } else {
                        expanded[cl - 1] = missing;
                    }
                }
            };
            const int equation = p_eqns[l_nn];
            if (count_chromosome_calls && equation > 0)
                copy_calls(chromosome_called.data() + size_t(equation - 1) * chromosome_count, std::true_type{});
            else
                copy_calls(nullptr, std::false_type{});
            if (p_eqns[l_nn] > 0) p_called[p_eqns[l_nn] - 1] = p_cache_called_cnt[n - 1];
            // Fill genotype from previous hap list
            if (step > 1) {
#if FINDHAP_SPLIT_TYPED_SNAPSHOT
                const int snapshot_animal = previous_row[p_cache_eqns_nn[n - 1]];
#else
                const int snapshot_animal = p_cache_eqns_nn[n - 1];
#endif
                int l_hap[2];
                for (int l_seg = 1; l_seg <= prevsegs; l_seg++) {
                    for (int l_pnt = 1; l_pnt <= 2; l_pnt++) {
                        l_hap[l_pnt - 1] = p_prevkeephap[KHIDX(l_pnt, snapshot_animal, l_seg, typed_snapshot_rows)];
                    }
                    if (l_hap[0] > 0 && l_hap[1] > 0) {
                        const int8_t* previous1 = p_prevhaplist + p_prevhap_offsets[l_seg - 1]
                            + (size_t)(l_hap[0] - 1) * prevmaxlen;
                        const int8_t* previous2 = p_prevhaplist + p_prevhap_offsets[l_seg - 1]
                            + (size_t)(l_hap[1] - 1) * prevmaxlen;
                        for (int ll = 1; ll <= p_prevlen[l_seg]; ll++) {
                            int l_locus = p_prevstartloc[l_seg] + ll - 1;
                            if (expanded[l_locus - 1] == missing) {
                                if (previous1[ll - 1] != 1 && previous2[ll - 1] != 1) {
                                    expanded[l_locus - 1] =
                                        (previous1[ll - 1] + previous2[ll - 1]) / 2;
                                }
                            }
                        }
                    }
                }
            }
            // Compute the unchanged first-iteration statistics while this
            // animal's expanded row is resident, then retain only packed state.
            nn = p_cache_eqns_nn[n - 1];
            if (nn != 0) {
                anmlmiss = 0;
                for (locus = 1; locus <= loci; locus++) {
                    if (expanded[locus - 1] != missing) {
                        p_sumfreq[locus - 1] += expanded[locus - 1];
                        p_nfreq[locus - 1]++;
                        if (p_birth[nn - 1] >= curryr) {
                            p_sumcurr[locus - 1] += expanded[locus - 1];
                            p_cfreq[locus - 1]++;
                        }
                        if (step == 1) p_ofreq[locus - 1]++;
                    } else {
                        anmlmiss++;
                    }
                }
                if (chromosome_processing) {
                    // Count while the expanded row is resident, including calls filled
                    // from the previous segment-length stage.
                    for (int c = chrome1; c <= chromes; ++c) {
                        const int first = loc1[c - 1], last = locN[c - 1];
                        if (first < 1 || first > last) continue;
                        const int64_t before = genfreq[missing];
                        for (int site = first; site <= last; ++site)
                            genfreq[expanded[site - 1]]++;
                        const int64_t missing_calls = genfreq[missing] - before;
                        const int chromosome_loci = last - first + 1;
                        auto& density = chromosome_high[size_t(c - chrome1) * maxped + nn - 1];
                        if (exist15 || missing_calls <= 0.6 * chromosome_loci) density = 1;
                        else if (missing_calls < 0.95 * chromosome_loci) density = 2;
                    }
                } else {
#if TURBOHAP_NATIVE_MILAN && defined(FINDHAP_LOW_MEMORY)
                    count_expanded_row(expanded, loci, genfreq);
#else
                    for (locus = 1; locus <= loci; locus++)
                        genfreq[expanded[locus - 1]]++;
#endif
                }
                if (!chromosome_processing) {
                    if (findhap::whole_genome_high_density || anmlmiss <= 0.6 * loci || exist15) {
                        expanded_nhigh++;
                        p_high[nn - 1] = 1;
                    } else if (anmlmiss < 0.95 * loci) {
                        p_high[nn - 1] = 2;
                    }
                }
                cache_expanded.store_row(n - 1, expanded);
            }
        }
#if defined(__APPLE__) && defined(__aarch64__)
        }
#endif
#ifdef FINDHAP_LOW_MEMORY_PLUS
        expansion_failure.rethrow("genotype expansion");
#endif
        cache_expanded.end_input();
        input_first += input_count;
        }
        cache_expanded.finish_input();
#if FINDHAP_SPLIT_TYPED_SNAPSHOT
        prev_typed_assignments.reset();
#endif
#ifdef FINDHAP_LOW_MEMORY_PLUS
        genotype_cache->release_batch();
#endif
        expanded_scratch.reset();
        raw_scratch.reset();
        // ---- End pre-expansion ----
        FINDHAP_END(expansion_trace);
        report_events.phase("Genotype expansion", "completed", step);

        for (iter = iter1; iter <= iters; iter++) {
            report_events.emit("iteration_start", step, iter);
            legacy_printf(" Step  %d   Haplotype iteration  %d\n", step, iter);
            if (iter >= iters / 2) switch_val = 0;
            notfound = 0;
            n13 = 0;
            nhigh = iter == iter1 ? expanded_nhigh : 0;
            nmed = 0;

            if (iter > iter1) {
                // Zero active hapfreq entries
#if defined(__APPLE__) && defined(__aarch64__)
                #pragma omp parallel num_threads(threads)
                {
                    findhap::macos::worker_enter();
                    #pragma omp for
#else
                #pragma omp parallel for num_threads(threads)
#endif
                for (seg = 1; seg <= maxseg; seg++) {
                    // i is shared outside this worksharing loop. A local
                    // counter prevents incomplete resets under concurrency.
                    for (int hap = 1; hap <= nhap_arr[(seg - 1) * FINDHAP_NHAP_STRIDE]; hap++) {
                        hapfreq_arr[IDX2(hap, seg, maxhap)] = 0;
                    }
                }
#if defined(__APPLE__) && defined(__aarch64__)
                }
#endif
            }

            // Serial pre-pass
            for (n = 1; n <= cache_ngen; n++) {
                nn = cache_eqns_nn[n - 1];
                if (nn == 0) {
                    if (iter == iter1 && step == 1)
                        status_printf(" no pedigree for genotyped animal  %d\n", cache_id[n - 1]);
                    continue;
                }
                n13++;
                listeqn[nn] = n13;
            }

            if (!chromosome_processing && iter == 2) {
#if defined(__APPLE__) && defined(__aarch64__)
                #pragma omp parallel private(n, nn) reduction(+: nmed) num_threads(threads)
                {
                    findhap::macos::worker_enter();
                    #pragma omp for schedule(static)
#else
                #pragma omp parallel for private(n, nn) reduction(+: nmed) \
                    schedule(static) num_threads(threads)
#endif
                for (n = 1; n <= cache_ngen; n++) {
                    nn = cache_eqns_nn[n - 1];
                    if (nn == 0) continue;
                    if (high[nn - 1] != 0) nmed++;
                }
#if defined(__APPLE__) && defined(__aarch64__)
                }
#endif
            }

            // ---- Parallel segment processing ----
            report_events.phase("Population matching", "started", step, iter);
            FINDHAP_PHASE(population_trace, "population", step, iter);
#ifdef FINDHAP_LOW_MEMORY_PLUS
            // In the partitioned workspace, each worker visits a row once per
            // segment. After the last population iteration there are no more
            // reads of these rows. Overlapping priors can still observe edits
            // made by other segments during the current pass.
            const bool persist_population_edits = iter < iters || !cache_expanded.packed();
#endif
            for (size_t tile_first = 0; tile_first < (size_t)maxseg;) {
            WorkerFailure population_failure;
#ifdef FINDHAP_LOW_MEMORY
            const size_t tile_end = maxseg;
            const int population_threads = cache_expanded.population_workers(threads);
            auto population_workers = cache_expanded.make_population_workers(population_threads);
#else
            const size_t tile_end = cache_expanded.tile_end(tile_first, threads);
#endif
            FINDHAP_PHASE(tile_trace, "population_tile", step, iter, tile_first + 1);
#ifndef FINDHAP_LOW_MEMORY
            FINDHAP_PHASE(tile_read_trace, "tile_read", step, iter, tile_first + 1);
            cache_expanded.begin_tile(tile_first, tile_end);
            FINDHAP_END(tile_read_trace);
            #pragma omp parallel num_threads(threads) firstprivate(switch_val)
#else
            #pragma omp parallel num_threads(population_threads) firstprivate(switch_val)
#endif
            {
                findhap::macos::worker_enter();
                int n, nn, j, k, l, pnt, pedx_temp, length, found_temp;
                int suggest[16] = {0}; // Thread-private initialized ONCE to zero
                int l_found[2];
                int l_pedx[2];
                std::vector<int8_t> segment_scratch;
                try {
                    segment_scratch.resize(maxlen);
                } catch (...) {
                    population_failure.capture();
                }
#ifdef FINDHAP_LOW_MEMORY
                auto& population_worker = *population_workers[omp_get_thread_num()];
#endif
                #pragma omp for schedule(dynamic, 1)
                for (seg = (int)tile_first + 1; seg <= (int)tile_end; seg++) {
                if (population_failure.failed()) continue;
                try {
#ifdef FINDHAP_LOW_MEMORY
                FINDHAP_PHASE(segment_read_trace, "segment_read", step, iter, seg);
                population_worker.begin_segment(seg - 1);
                FINDHAP_END(segment_read_trace);
#endif
                FINDHAP_PHASE(segment_trace, "population_segment", step, iter, seg,
                              chro[seg - 1], len_arr[seg], &nhap_arr[(seg - 1) * FINDHAP_NHAP_STRIDE]);
                // Own this index only during one population segment pass.
                // Rebuild after pedigree/prior mutations in later iterations.
                std::unique_ptr<CandidateIndex> candidate_index;
                const uint8_t* segment_high = chromosome_processing
                    ? chromosome_high.data() + size_t(chro[seg - 1] - chrome1) * maxped
                    : high.get();
                const bool x_segment = uses_x_context(seg);
                for (n = 1; n <= cache_ngen; n++) {
                    FINDHAP_COUNT(0);
                    nn = cache_eqns_nn[n - 1];
                    if (nn == 0) continue;
                    if (iter == iter1 && segment_high[nn - 1] != 1) continue;
                    if (iter == 2 && segment_high[nn - 1] == 0) continue;
                    FINDHAP_COUNT(1);
                    // listhap<1> starts after all 16 suggestion slots.
#if FINDHAP_POP_SKIP_UNUSED
                    if (switch_val == 0)
#endif
                    for (int s = 0; s < 16; s++) suggest[s] = 0;
                    for (pnt = 1; pnt <= 2; pnt++) {
                        l_pedx[pnt - 1] = ped[IDX2(pnt, nn, 2)];
                    }
                    if (sexv[nn - 1] == 'M' && x_segment)
                        l_pedx[0] = l_pedx[1];
                    for (pnt = 1; pnt <= 2; pnt++) {
                        if (l_pedx[pnt - 1] > 0) {
                            const auto code = listeqn[l_pedx[pnt - 1]] > 0 ? InheritanceCode::unmatched
                                : pnt == 1 ? InheritanceCode::sire_without_genotype
                                           : InheritanceCode::dam_without_genotype;
                            patmat[PMIDX(nn, seg, maxped)].set(pnt - 1, code);
                            // Inheritance initialisation above also runs when
                            // the population matcher skips relative suggestions.
#if FINDHAP_POP_SKIP_UNUSED
                            if (switch_val == 0)
#endif
                            for (j = 1; j <= 2; j++) {
                                suggest[(pnt - 1) * 8 + (j - 1) * 4] =
                                    keephap[KHIDX(j, l_pedx[pnt - 1], seg, maxped)];
                                if (keephap[KHIDX(j, l_pedx[pnt - 1], seg, maxped)] == 0) {
                                    if (ped[IDX2(j, l_pedx[pnt - 1], 2)] > 0) {
                                        for (k = 1; k <= 2; k++) {
                                            suggest[(pnt - 1) * 8 + (j - 1) * 4 + (k - 1) * 2] =
                                                keephap[KHIDX(k, ped[IDX2(j, l_pedx[pnt - 1], 2)], seg, maxped)];
                                            if (keephap[KHIDX(k, ped[IDX2(j, l_pedx[pnt - 1], 2)], seg, maxped)] == 0) {
                                                if (ped[IDX2(k, ped[IDX2(j, l_pedx[pnt - 1], 2)], 2)] > 0) {
                                                    for (l = 1; l <= 2; l++) {
                                                        suggest[(pnt - 1) * 8 + (j - 1) * 4 + (k - 1) * 2 + l - 1] =
                                                            keephap[KHIDX(l, ped[IDX2(k, ped[IDX2(j, l_pedx[pnt - 1], 2)], 2)], seg, maxped)];
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    } // end pnt loop

                    length = len_arr[seg];
#ifdef FINDHAP_LOW_MEMORY
                    int8_t* working_genotype = population_worker.segment(n - 1, segment_scratch.data());
#else
                    int8_t* working_genotype = cache_expanded.segment(n - 1, seg - 1, segment_scratch.data());
#endif
                    if (!candidate_index && length >= FINDHAP_POP_INDEX_LENGTH && nhap_arr[(seg - 1) * FINDHAP_NHAP_STRIDE] >= FINDHAP_POP_INDEX_HAPS)
                        candidate_index = std::make_unique<CandidateIndex>(length,
                            haplist_arr.get() + (size_t)(seg - 1) * maxhap * maxlen,
                            maxlen, nhap_arr[(seg - 1) * FINDHAP_NHAP_STRIDE]);
                    bool genotype_changed;
                    if (switch_val == 1) {
                        genotype_changed = listhap<1>(working_genotype,
                                haplist_arr.get(), hapfreq_arr.get(), order_arr.get(), suggest,
                                length, maxlen, nhap_arr.get(), maxhap, seg, maxseg, l_found, errate, candidate_index.get());
                    } else {
                        genotype_changed = listhap<0>(working_genotype,
                                haplist_arr.get(), hapfreq_arr.get(), order_arr.get(), suggest,
                                length, maxlen, nhap_arr.get(), maxhap, seg, maxseg, l_found, errate, candidate_index.get());
                    }
                    if (switch_val == 0) {
                        if (genotype_changed) {
#ifdef FINDHAP_LOW_MEMORY
#ifdef FINDHAP_LOW_MEMORY_PLUS
                            if (persist_population_edits)
#endif
                            population_worker.save_segment(n - 1, working_genotype);
#else
                            cache_expanded.save_segment(n - 1, seg - 1, working_genotype);
#endif
                        }
                        if (!(keephap[KHIDX(1, nn, seg, maxped)] == l_found[1] &&
                              keephap[KHIDX(2, nn, seg, maxped)] == l_found[0])) {
                            keephap[KHIDX(1, nn, seg, maxped)] = l_found[0];
                            keephap[KHIDX(2, nn, seg, maxped)] = l_found[1];
#ifdef FINDHAP_DEBUG
                            if (seg == 1) {
                                FILE* fp = fopen("cpp_keephap.txt", "a");
                                if (fp) {
                                    fprintf(fp, " n= %d  keephap1= %d  keephap2= %d\n", n, l_found[0], l_found[1]);
                                    fclose(fp);
                                }
                            }
#endif
                        }
                    }
                } // end n loop
#ifdef FINDHAP_LOW_MEMORY
                FINDHAP_END(segment_trace);
                FINDHAP_PHASE(segment_write_trace, "segment_write", step, iter, seg);
                population_worker.end_segment();
#endif
                } catch (...) {
                    population_failure.capture();
                }
            } // end seg loop
            } // Close omp parallel block
            population_failure.rethrow("population matching");
#ifdef FINDHAP_LOW_MEMORY
            cache_expanded.finish_population_workers(population_workers);
#else
            FINDHAP_PHASE(tile_write_trace, "tile_write", step, iter, tile_first + 1);
            cache_expanded.end_tile(tile_end);
            FINDHAP_END(tile_write_trace);
#endif
            tile_first = tile_end;
            }
            cache_expanded.finish_population();
            FINDHAP_END(population_trace);
            report_events.phase("Population matching", "completed", step, iter);

            // ---- Allele frequency report ----
            if (iter == iter1) {
                legacy_printf(" %d records read from file genotypes.txt\n", n13);
                if (!chromosome_processing && nhigh < n13) {
                    legacy_printf(" %d high  density genotypes used in iter 1\n", nhigh);
                    legacy_printf(" %d lower density genotypes used in later iters\n", n13 - nhigh);
                }
                legacy_printf("\n");
                legacy_printf(" Frequency of genotype codes input in step  %d\n", step);
                for (i = 0; i <= 9; i++) {
                    if (genfreq[i] > 0)
                        legacy_printf("%15ld genotypes with code %2d%12.5f%%\n",
                               (long)genfreq[i], i, 100.0 * genfreq[i] / n13 / loci);
                }
                for (locus = 1; locus <= loci; locus++) {
                    if (nfreq[locus - 1] > 0) {
                        freq[locus - 1] = 0.5f * sumfreq_arr[locus - 1] / nfreq[locus - 1];
                    } else {
                        freq[locus - 1] = 0.5f;
                    }
                }
                legacy_printf("\n");
                legacy_printf(" Allele frequencies estimated\n");
                for (locus = 1; locus <= std::min(10, loci); locus++) {
                    legacy_printf("%8.4f", freq[locus - 1]);
                }
                legacy_printf("\n\n");
            }
            if (!chromosome_processing && iter == 2 && nmed < n13 && nmed != nhigh) {
                legacy_printf(" %d medium and high density genotypes used in iter 2\n", nmed);
                legacy_printf(" %d low density genotypes used in iters 3+\n", n13 - nmed);
            }

            ncross = 0;
            if (iter == iters && step == steps) {
                constexpr const char* header = " animkey pntkey segment endloc beginloc new_hap pnthap1 pnthap2\n";
                if (chromosome_processing) {
                    for (const auto& result : chromosome_results)
                        if (result.crossovers) fputs(header, result.crossovers);
                } else fputs(header, fp27);
            }

            // Count genotyped progeny
            if (step == 1 && iter == iter1) {
                maxprog = 0;
                for (i = nped; i >= 1; i--) {
                    if (listeqn[i] > 0 || gprog[i] >= 3) {
                        for (pnt = 1; pnt <= 2; pnt++) {
                            if (ped[IDX2(pnt, i, 2)] > 0 && listeqn[ped[IDX2(3 - pnt, i, 2)]] > 0) {
                                gprog[ped[IDX2(pnt, i, 2)]]++;
                                if (gprog[ped[IDX2(pnt, i, 2)]] > maxprog) {
                                    maxprog = gprog[ped[IDX2(pnt, i, 2)]];
                                    maxpnt = animkey[ped[IDX2(pnt, i, 2)]];
                                }
                            }
                        }
                    }
                }
                if (maxprog > 0)
                    legacy_printf("%9d maximum genotyped progeny for parent id = %9d\n", maxprog, maxpnt);
            }

            // Set up parent location vector
            int progeny_slot = 1;
            for (i = 1; i <= nped; i++) {
                if (prog[i] > 0) {
                    if (!compact_progeny || listeqn[i] == 0) {
                        pntloc[i] = progeny_slot;
                        progeny_slot += prog[i];
                    } else {
                        // Role cursors still advance for every recorded link.
                        pntloc[i] = 0;
                    }
                    if (prog[i] >= maxprog) maxpnt = animkey[i];
                    maxprog = std::max(maxprog, prog[i]);
                }
            }
            if (step == 1 && iter == iter1) {
                legacy_printf("%9d maximum progeny for parent id = %9d\n", maxprog, maxpnt);
                legacy_printf("\n");
            }
#ifdef FINDHAP_DEBUG
            // Diagnostic dump of nhap
            {
                FILE* fp98 = fopen("cpp_diag.txt", "a");
                if (fp98) {
                    fprintf(fp98, "=== NHAP AFTER STEP %d ITER %d ===\n", step, iter);
                    for (seg = 1; seg <= maxseg; seg++) {
                        fprintf(fp98, "seg=%d nhap=%d\n", seg, nhap_arr[(seg - 1) * FINDHAP_NHAP_STRIDE]);
                    }
                    fclose(fp98);
                }
            }
#endif

            if (switch_val == 1) continue;
            report_events.phase("Pedigree matching", "started", step, iter);
            FINDHAP_PHASE(pedigree_trace, "pedigree", step, iter);
            FINDHAP_PHASE(pedigree_dams_trace, "pedigree_dams", step, iter);
            memset(suggest, 0, sizeof(suggest));

            // ===================================================================
            // Pedigree matching and inheritance correction
            // ===================================================================

            // Destination segments own disjoint mutable library state. Keep
            // the original animal order within each segment; do not parallelise
            // families or generations sharing a parent or haplotype library.
            WorkerFailure rematch_failure;
#if FINDHAP_SPLIT_TYPED_SNAPSHOT
            const int untyped_snapshot_rows = previous_untyped_rows;
            auto snapshot_animal = [&](int animal) noexcept { return -previous_row[animal]; };
#else
            const int untyped_snapshot_rows = maxped;
            auto snapshot_animal = [](int animal) noexcept { return animal; };
#endif
#if FINDHAP_PEDIGREE_REMATCH == 2
            if (step > 1 && !exist15 && rematch_plan.disjoint) {
                std::vector<int> rematch_animals;
                for (int animal = 1; animal <= nped; ++animal)
                    if (listeqn[animal] == 0) rematch_animals.push_back(animal);
                if (!rematch_animals.empty()) {
                    #pragma omp parallel num_threads(threads)
                    {
                        findhap::macos::worker_enter();
                        int local_suggest[16] = {};
                        std::vector<int8_t> reconstructed;
                        #pragma omp for schedule(dynamic, 1)
                        for (int current = 1; current <= maxseg; ++current) {
                            if (rematch_failure.failed()) continue;
                            try {
                                const int length = len_arr[current];
                                reconstructed.resize(length);
                                std::unique_ptr<CandidateIndex> candidate_index;
                                for (int animal : rematch_animals) {
                                    const int known = pedigree_rematch::reconstruct(
                                        reconstructed.data(), length, snapshot_animal(animal), untyped_snapshot_rows,
                                        rematch_plan.segments[current - 1], prevkeephap.get(),
                                        prevhaplist.get(), prevhap_offsets.data(), prevmaxlen, missing);
                                    if (known == 0) continue;
                                    if (!candidate_index && length >= FINDHAP_POP_INDEX_LENGTH) {
                                        const int count = nhap_arr[(current - 1) * FINDHAP_NHAP_STRIDE];
                                        if (count >= FINDHAP_POP_INDEX_HAPS)
                                            candidate_index = std::make_unique<CandidateIndex>(length,
                                                haplist_arr.get() + size_t(current - 1) * maxhap * maxlen,
                                                maxlen, count);
                                    }
                                    int local_found[2] = {};
                                    listhap<0>(reconstructed.data(), haplist_arr.get(), hapfreq_arr.get(),
                                               order_arr.get(), local_suggest, length, maxlen,
                                               nhap_arr.get(), maxhap, current, maxseg, local_found, errate,
                                               candidate_index.get());
                                    if (local_found[0] != local_found[1]) {
                                        keephap[KHIDX(1, animal, current, maxped)] = local_found[0];
                                        keephap[KHIDX(2, animal, current, maxped)] = local_found[1];
                                    }
                                }
                            } catch (...) {
                                rematch_failure.capture();
                            }
                        }
                    }
                    rematch_failure.rethrow("pedigree rematching");
                    // Preserve the legacy shared loop endpoint as well as the
                    // result state; later pedigree code observes this variable.
                    seg = prevsegs + 1;
                }
                i = nped + 1;
            } else
#endif
            {
            // Use previous haplotypes of imputed dams
            for (i = 1; i <= nped; i++) {
                if (step > 1 && listeqn[i] == 0 && !exist15) {
                    // Create genotypes from previous haplotypes
                    for (int ll = 0; ll < loci; ll++) genotype[ll] = 5;
                    for (seg = 1; seg <= prevsegs; seg++) {
                        for (pnt = 1; pnt <= 2; pnt++) {
                            hap[pnt - 1] = prevkeephap[KHIDX(pnt, snapshot_animal(i), seg, untyped_snapshot_rows)];
                        }
                        if (hap[0] > 0 && hap[1] > 0) {
                            const int8_t* previous1 = prevhaplist.get() + prevhap_offsets[seg - 1]
                                + (size_t)(hap[0] - 1) * prevmaxlen;
                            const int8_t* previous2 = prevhaplist.get() + prevhap_offsets[seg - 1]
                                + (size_t)(hap[1] - 1) * prevmaxlen;
                            for (l = 1; l <= prevlen[seg]; l++) {
                                if (previous1[l - 1] != 1 && previous2[l - 1] != 1) {
                                    locus = prevstartloc[seg] + l - 1;
                                    genotype[locus - 1] = (previous1[l - 1] + previous2[l - 1]) / 2;
                                }
                            }
                        }
                    }
#if defined(__APPLE__) && defined(__aarch64__)
                    #pragma omp parallel private(length, l, locus, found, known) num_threads(threads)
                    {
                        findhap::macos::worker_enter();
                        #pragma omp for
#else
                    #pragma omp parallel for private(length, l, locus, found, known) num_threads(threads)
#endif
                    for (seg = 1; seg <= maxseg; seg++) {
                        known = 0;
                        for (l = 1; l <= len_arr[seg]; l++) {
                            locus = startloc[seg] + l - 1;
                            if (genotype[locus - 1] != missing) known++;
                        }
                        int l_found[2] = {0, 0};
                        length = len_arr[seg];
                        if (known > 0) {
                            try {
                                if (switch_val == 1) {
                                    listhap<1>(&genotype[startloc[seg] - 1],
                                        haplist_arr.get(), hapfreq_arr.get(), order_arr.get(), suggest,
                                        length, maxlen, nhap_arr.get(), maxhap, seg, maxseg, l_found, errate);
                                } else {
                                    listhap<0>(&genotype[startloc[seg] - 1],
                                        haplist_arr.get(), hapfreq_arr.get(), order_arr.get(), suggest,
                                        length, maxlen, nhap_arr.get(), maxhap, seg, maxseg, l_found, errate);
                                }
                            } catch (...) {
                                rematch_failure.capture();
                                continue;
                            }
                            if (l_found[0] != l_found[1]) {
                                keephap[KHIDX(1, i, seg, maxped)] = l_found[0];
                                keephap[KHIDX(2, i, seg, maxped)] = l_found[1];
                            }
                        }
                    }
#if defined(__APPLE__) && defined(__aarch64__)
                    }
#endif
                    rematch_failure.rethrow("pedigree rematching");
                }
            }

            }

            FINDHAP_END(pedigree_dams_trace);
            FINDHAP_PHASE(pedigree_main_trace, "pedigree_main", step, iter);
            // Each chromosome owns disjoint segment/genotype/frequency state.
            // Preserve animal order within it, including parent mutations and
            // adjacent-segment orientation. Final-pass batches emit records in
            // the original animal/segment order; the fallback keeps that traversal.
            const bool final_pedigree = iter == iters && step == steps;
            const bool scoped_final = chromosome_processing && final_pedigree &&
                FINDHAP_PEDIGREE_CHROMOSOMES && chromes - chrome1 + 1 >= threads;
            const bool ordered_final = !scoped_final && FINDHAP_PEDIGREE_ORDERED_FINAL && final_pedigree &&
                FINDHAP_PEDIGREE_CHROMOSOMES && chromes - chrome1 + 1 >= threads &&
                findhap::OrderedCrossoverBatch::capacity(size_t(maxseg)) > 0;
            const bool chromosome_pass = FINDHAP_PEDIGREE_CHROMOSOMES &&
                chromes - chrome1 + 1 >= threads && (!final_pedigree || ordered_final || scoped_final);
            constexpr size_t half_crossover_budget = FINDHAP_CROSSOVER_BATCH_BYTES / 2;
            const bool overlap_crossovers = FINDHAP_OVERLAP_CROSSOVERS && ordered_final &&
                !chromosome_processing && threads > 1 &&
                !FINDHAP_PEDIGREE_SERIAL_FINAL &&
                findhap::OrderedCrossoverBatch::capacity(size_t(maxseg), half_crossover_budget) > 0;
            std::unique_ptr<findhap::OrderedCrossoverBatch> crossover_batches[2];
            if (ordered_final) {
                const size_t budget = overlap_crossovers ? half_crossover_budget : FINDHAP_CROSSOVER_BATCH_BYTES;
                crossover_batches[0] = std::make_unique<findhap::OrderedCrossoverBatch>(size_t(maxseg), budget);
                if (overlap_crossovers)
                    crossover_batches[1] = std::make_unique<findhap::OrderedCrossoverBatch>(size_t(maxseg), budget);
            }
            constexpr size_t scoped_crossover_bytes = 65536;
            std::vector<char> scoped_crossover_buffers(scoped_final ? size_t(threads) * scoped_crossover_bytes : 0);
            std::vector<int> parent_slots;
            if (chromosome_pass) {
                parent_slots.resize(2 * (size_t(maxped) + 1));
                for (int animal = 1; animal <= maxped; ++animal) {
                    // Snapshot both slots before incrementing either parent:
                    // this also preserves the case where both IDs coincide.
                    for (int parent = 1; parent <= 2; ++parent) {
                        const int id = ped[IDX2(parent, animal, 2)];
                        if (id > 0) parent_slots[2 * size_t(animal) + parent - 1] = pntloc[id];
                    }
                    for (int parent = 1; parent <= 2; ++parent) {
                        const int id = ped[IDX2(parent, animal, 2)];
                        if (id > 0) ++pntloc[id];
                    }
                }
            }
            const int pedigree_threads = FINDHAP_PEDIGREE_SERIAL_FINAL && iter == iters && step == steps ? 1 : threads;
#if FINDHAP_PEDIGREE_PLACEMENT
            std::vector<findhap::pedigree_placement::Worker> pedigree_team(
                !chromosome_pass && int(pedigree_cores.size()) == pedigree_threads ? pedigree_threads : 0);
            bool pedigree_pinned = false;
#endif
            WorkerFailure crossover_failure;
            bool crossover_batch_failed = false;
            #pragma omp parallel private(i, pnt, j, grdpnt, pnthap, mismatch, match_arr, matches, pedx, found, k, l, locus, hold) \
                reduction(+: ncross) num_threads(pedigree_threads)
            {
                findhap::macos::worker_enter();
                // These operations sit inside millions of animal/segment
                // visits. Outlining the lambdas hides loop-invariant state
                // and proved substantially slower in the qualification study.
                auto process_segment = [&](int seg, bool independent) __attribute__((always_inline)) {
                    const bool male_x = sexv[i - 1] == 'M' && uses_x_context(seg);
                    if (iter == iters && step == steps) num_cross[seg - 1] = 0;
                    int l_match[2][2] = {{0,0},{0,0}};
                    int l_matches = 0;
                    int l_pedx[2], l_found[2], l_pnthap[2];
                    int l_cross[2], l_where[2][2];

                    for (pnt = 1; pnt <= 2; pnt++) {
                        l_pedx[pnt - 1] = ped[IDX2(pnt, i, 2)];
                    }
                    if (male_x)
                        l_pedx[0] = l_pedx[1];

                    for (j = 1; j <= 2; j++) {
                        l_found[j - 1] = keephap[KHIDX(j, i, seg, maxped)];
                        if (l_found[j - 1] > 0) {
                            for (pnt = 1; pnt <= 2; pnt++) {
                                if (l_pedx[pnt - 1] > 0) {
                                    for (grdpnt = 1; grdpnt <= 2; grdpnt++) {
                                        l_pnthap[grdpnt - 1] = keephap[KHIDX(grdpnt, l_pedx[pnt - 1], seg, maxped)];
                                        if (l_pnthap[grdpnt - 1] == l_found[j - 1]) {
                                            l_match[j - 1][pnt - 1] = grdpnt;
                                            continue;
                                        }
                                        if (l_pnthap[grdpnt - 1] > 0) {
#if FINDHAP_PEDIGREE_EARLY_REJECT
                                            if (pedigree_single_conflict(
                                                    &haplist_arr[IDX3(1, l_found[j - 1], seg, maxlen, maxhap)],
                                                    &haplist_arr[IDX3(1, l_pnthap[grdpnt - 1], seg, maxlen, maxhap)],
                                                    len_arr[seg]))
                                                l_match[j - 1][pnt - 1] = 4 + grdpnt;
#else
                                            mismatch = 0;
                                            for (l = 1; l <= len_arr[seg]; l++) {
                                                if (std::abs(haplist_arr[IDX3(l, l_found[j - 1], seg, maxlen, maxhap)] -
                                                             haplist_arr[IDX3(l, l_pnthap[grdpnt - 1], seg, maxlen, maxhap)]) > 1)
                                                    mismatch++;
                                            }
                                            if (mismatch == 1) l_match[j - 1][pnt - 1] = 4 + grdpnt;
#endif
                                        }
                                    }
                                    if (l_pnthap[0] == l_pnthap[1] && l_match[j - 1][pnt - 1] > 0)
                                        l_match[j - 1][pnt - 1] = 7;
                                }
                            }
                        }
                        if (l_match[j - 1][0] > 0 || l_match[j - 1][1] > 0) l_matches++;
                    }

                    // Check for crossovers
                    for (j = 1; j <= 2; j++) {
                        if (l_found[j - 1] > 0 && l_matches < 2) {
                            for (pnt = 1; pnt <= 2; pnt++) {
                                if (l_pedx[pnt - 1] > 0) {
                                    for (grdpnt = 1; grdpnt <= 2; grdpnt++) {
                                        l_pnthap[grdpnt - 1] = keephap[KHIDX(grdpnt, l_pedx[pnt - 1], seg, maxped)];
                                    }
                                    if (l_pnthap[0] == 0 || l_pnthap[1] == 0) continue;
#if FINDHAP_PEDIGREE_MASK_BACKEND >= 0
                                    const auto crossover_scan = findhap_pedigree::scan_crossovers<FINDHAP_PEDIGREE_MASK_BACKEND>(
                                        &haplist_arr[IDX3(1, l_found[j - 1], seg, maxlen, maxhap)],
                                        &haplist_arr[IDX3(1, l_pnthap[0], seg, maxlen, maxhap)],
                                        &haplist_arr[IDX3(1, l_pnthap[1], seg, maxlen, maxhap)], len_arr[seg]);
                                    for (int start = 0; start < 2; ++start) {
                                        l_cross[start] = crossover_scan.count[start];
                                        l_where[0][start] = crossover_scan.first[start];
                                        l_where[1][start] = crossover_scan.last[start];
                                    }
#else
                                    for (grdpnt = 1; grdpnt <= 2; grdpnt++) {
                                        k = grdpnt;
                                        l_cross[grdpnt - 1] = 0;
                                        l_where[0][grdpnt - 1] = 0;
                                        l_where[1][grdpnt - 1] = len_arr[seg] + 1;
                                        for (l = 1; l <= len_arr[seg]; l++) {
                                            if (std::abs(haplist_arr[IDX3(l, l_found[j - 1], seg, maxlen, maxhap)] -
                                                         haplist_arr[IDX3(l, l_pnthap[k - 1], seg, maxlen, maxhap)]) > 1) {
                                                l_cross[grdpnt - 1]++;
                                                l_where[1][grdpnt - 1] = l;
                                                k = 3 - k;
                                                if (std::abs(haplist_arr[IDX3(l, l_found[j - 1], seg, maxlen, maxhap)] -
                                                             haplist_arr[IDX3(l, l_pnthap[k - 1], seg, maxlen, maxhap)]) > 1)
                                                    l_cross[grdpnt - 1]++;
                                            } else {
                                                if (std::abs(haplist_arr[IDX3(l, l_found[j - 1], seg, maxlen, maxhap)] -
                                                             haplist_arr[IDX3(l, l_pnthap[2 - k], seg, maxlen, maxhap)]) > 1 &&
                                                    l_cross[grdpnt - 1] == 0)
                                                    l_where[0][grdpnt - 1] = l;
                                            }
                                        }
                                    }
#endif
                                    // Output crossover list
                                    for (grdpnt = 1; grdpnt <= 2; grdpnt++) {
                                        if (l_cross[grdpnt - 1] == 1 && l_cross[2 - grdpnt] > 1) {
                                            ncross++;
                                            if (iter == iters && step == steps) {
                                                num_cross[seg - 1]++;
                                                if (num_cross[seg - 1] <= 10) { // legacy 80-byte, ten-record slot
                                                    char* destination = &cross_buf[(num_cross[seg - 1] - 1) * 80 + ((size_t)seg - 1) * 800];
                                                    const int reported_segment = chromosome_processing
                                                        ? seg - startseg[chro[seg - 1] - 1] + 1 : seg;
#if FINDHAP_CROSSOVER_FORMAT
                                                    turbohap_output::crossover_line(destination,
#else
                                                    snprintf(destination, 80, "%9d%9d%7d%7d%7d%7d%7d%7d",
#endif
                                                         animkey[i], animkey[ped[IDX2(pnt, i, 2)]],
                                                         reported_segment, l_where[0][grdpnt - 1], l_where[1][grdpnt - 1],
                                                         l_found[j - 1], l_pnthap[grdpnt - 1], l_pnthap[2 - grdpnt]);
                                                }
                                            }
                                            if (l_match[j - 1][pnt - 1] == 0)
                                                l_match[j - 1][pnt - 1] = 2 + grdpnt;
                                            // Fill missing from crossover haps — left segment
                                            for (l = 1; l <= l_where[0][grdpnt - 1]; l++) {
                                                if (haplist_arr[IDX3(l, l_found[j - 1], seg, maxlen, maxhap)] == 1 &&
                                                    haplist_arr[IDX3(l, l_pnthap[grdpnt - 1], seg, maxlen, maxhap)] != 1)
                                                    haplist_arr[IDX3(l, l_found[j - 1], seg, maxlen, maxhap)] =
                                                        haplist_arr[IDX3(l, l_pnthap[grdpnt - 1], seg, maxlen, maxhap)];
                                            }
                                            // Right segment
                                            for (l = l_where[1][grdpnt - 1]; l <= len_arr[seg]; l++) {
                                                if (haplist_arr[IDX3(l, l_found[j - 1], seg, maxlen, maxhap)] == 1 &&
                                                    haplist_arr[IDX3(l, l_pnthap[2 - grdpnt], seg, maxlen, maxhap)] != 1)
                                                    haplist_arr[IDX3(l, l_found[j - 1], seg, maxlen, maxhap)] =
                                                        haplist_arr[IDX3(l, l_pnthap[2 - grdpnt], seg, maxlen, maxhap)];
                                            }
                                            // Middle segment
                                            for (l = l_where[0][grdpnt - 1] + 1; l <= l_where[1][grdpnt - 1] - 1; l++) {
                                                if (haplist_arr[IDX3(l, l_found[j - 1], seg, maxlen, maxhap)] == 1 &&
                                                    haplist_arr[IDX3(l, l_pnthap[grdpnt - 1], seg, maxlen, maxhap)] != 1 &&
                                                    haplist_arr[IDX3(l, l_pnthap[0], seg, maxlen, maxhap)] ==
                                                    haplist_arr[IDX3(l, l_pnthap[1], seg, maxlen, maxhap)])
                                                    haplist_arr[IDX3(l, l_found[j - 1], seg, maxlen, maxhap)] =
                                                        haplist_arr[IDX3(l, l_pnthap[grdpnt - 1], seg, maxlen, maxhap)];
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }

                    // Delete double match for homozygotes
                    if (l_found[0] == l_found[1]) {
                        l_match[0][1] = 0;
                        l_match[1][0] = 0;
                    }
                    // Reverse haps if opposite parents match
                    if ((l_match[0][1] > 0 && l_match[0][0] == 0) ||
                        (l_match[1][0] > 0 && l_match[1][1] == 0)) {
                        keephap[KHIDX(1, i, seg, maxped)] = l_found[1];
                        keephap[KHIDX(2, i, seg, maxped)] = l_found[0];
                        std::swap(l_found[0], l_found[1]);
                        hold = l_match[0][0]; l_match[0][0] = l_match[1][0]; l_match[1][0] = hold;
                        hold = l_match[1][1]; l_match[1][1] = l_match[0][1]; l_match[0][1] = hold;
                    }
                    // Code paternal, maternal alleles
                    if ((l_match[0][0] > 0 && l_match[0][1] == 0) ||
                        (l_match[1][1] > 0 && l_match[1][0] == 0)) {
                        if (l_match[0][0] > 0) {
                            patmat[PMIDX(i, seg, maxped)].set(0, findhap::inheritance::match_codes[l_match[0][0] - 1]);
                        }
                        if (l_match[1][1] > 0) {
                            patmat[PMIDX(i, seg, maxped)].set(1, findhap::inheritance::match_codes[l_match[1][1] - 1]);
                        }
                        // Fix noninheritance (errate handling)
                        if (errate * maxlen >= 0.2f) {
                            if (l_match[0][0] == 5) {
                                if (hapfreq_arr[IDX2(l_found[0], seg, maxhap)] > hapfreq_arr[IDX2(keephap[KHIDX(1, l_pedx[0], seg, maxped)], seg, maxhap)]) {
                                    keephap[KHIDX(1, l_pedx[0], seg, maxped)] = l_found[0];
                                    hapfreq_arr[IDX2(keephap[KHIDX(1, l_pedx[0], seg, maxped)], seg, maxhap)]--;
                                    hapfreq_arr[IDX2(l_found[0], seg, maxhap)]++;
                                } else {
                                    l_found[0] = keephap[KHIDX(1, l_pedx[0], seg, maxped)];
                                    keephap[KHIDX(1, i, seg, maxped)] = keephap[KHIDX(1, l_pedx[0], seg, maxped)];
                                    hapfreq_arr[IDX2(l_found[0], seg, maxhap)]--;
                                    hapfreq_arr[IDX2(keephap[KHIDX(1, l_pedx[0], seg, maxped)], seg, maxhap)]++;
                                }
                            }
                            if (l_match[0][0] == 6) {
                                if (hapfreq_arr[IDX2(l_found[0], seg, maxhap)] > hapfreq_arr[IDX2(keephap[KHIDX(2, l_pedx[0], seg, maxped)], seg, maxhap)]) {
                                    keephap[KHIDX(2, l_pedx[0], seg, maxped)] = l_found[0];
                                    hapfreq_arr[IDX2(keephap[KHIDX(2, l_pedx[0], seg, maxped)], seg, maxhap)]--;
                                    hapfreq_arr[IDX2(l_found[0], seg, maxhap)]++;
                                } else {
                                    l_found[0] = keephap[KHIDX(2, l_pedx[0], seg, maxped)];
                                    keephap[KHIDX(1, i, seg, maxped)] = keephap[KHIDX(2, l_pedx[0], seg, maxped)];
                                    hapfreq_arr[IDX2(l_found[0], seg, maxhap)]--;
                                    hapfreq_arr[IDX2(keephap[KHIDX(2, l_pedx[0], seg, maxped)], seg, maxhap)]++;
                                }
                            }
                            if (l_match[1][1] == 5) {
                                if (hapfreq_arr[IDX2(l_found[1], seg, maxhap)] > hapfreq_arr[IDX2(keephap[KHIDX(1, l_pedx[1], seg, maxped)], seg, maxhap)]) {
                                    keephap[KHIDX(1, l_pedx[1], seg, maxped)] = l_found[1];
                                    hapfreq_arr[IDX2(keephap[KHIDX(1, l_pedx[1], seg, maxped)], seg, maxhap)]--;
                                    hapfreq_arr[IDX2(l_found[1], seg, maxhap)]++;
                                } else {
                                    l_found[1] = keephap[KHIDX(1, l_pedx[1], seg, maxped)];
                                    keephap[KHIDX(2, i, seg, maxped)] = keephap[KHIDX(1, l_pedx[1], seg, maxped)];
                                    hapfreq_arr[IDX2(l_found[1], seg, maxhap)]--;
                                    hapfreq_arr[IDX2(keephap[KHIDX(1, l_pedx[1], seg, maxped)], seg, maxhap)]++;
                                }
                            }
                            if (l_match[1][1] == 6) {
                                if (hapfreq_arr[IDX2(l_found[1], seg, maxhap)] > hapfreq_arr[IDX2(keephap[KHIDX(2, l_pedx[1], seg, maxped)], seg, maxhap)]) {
                                    keephap[KHIDX(2, l_pedx[1], seg, maxped)] = l_found[1];
                                    hapfreq_arr[IDX2(keephap[KHIDX(2, l_pedx[1], seg, maxped)], seg, maxhap)]--;
                                    hapfreq_arr[IDX2(l_found[1], seg, maxhap)]++;
                                } else {
                                    l_found[1] = keephap[KHIDX(2, l_pedx[1], seg, maxped)];
                                    keephap[KHIDX(2, i, seg, maxped)] = keephap[KHIDX(2, l_pedx[1], seg, maxped)];
                                    hapfreq_arr[IDX2(l_found[1], seg, maxhap)]--;
                                    hapfreq_arr[IDX2(keephap[KHIDX(2, l_pedx[1], seg, maxped)], seg, maxhap)]++;
                                }
                            }
                        }
                    }
                    // Both parents have both haps
                    if (l_match[0][0] > 0 && l_match[0][1] > 0 &&
                        l_match[1][0] > 0 && l_match[1][1] > 0 &&
                        l_match[0][0] < 3 && l_match[0][1] < 3 &&
                        l_match[1][0] < 3 && l_match[1][1] < 3 &&
                        l_found[0] != l_found[1]) {
                        patmat[PMIDX(i, seg, maxped)] = InheritancePair::both(InheritanceCode::ambiguous);
                    }

                    // Fill missing hap of mate + base frequency + progeny imputation
                    for (pnt = 1; pnt <= 2; pnt++) {
                        if (l_pedx[pnt - 1] > 0 &&
                            (listeqn[l_pedx[2 - pnt]] > 0 || gprog[l_pedx[2 - pnt]] > 3)) {
                            const int parent = l_pedx[pnt - 1];
                            const bool save_evidence = !compact_progeny || listeqn[parent] == 0;
                            for (j = 1; j <= 2; j++) {
                                if ((l_match[j - 1][2 - pnt] > 0 && l_match[2 - j][2 - pnt] == 0) ||
                                    l_match[j - 1][2 - pnt] == 7) {
                                    if (save_evidence) {
                                        const int role = male_x ? 1 : pnt - 1;
                                        const int slot = independent ? parent_slots[2 * size_t(i) + role] : pntloc[parent];
                                        fromprog[IDX2(slot, seg, totprog)] = l_found[2 - j];
                                    }
                                    if (keephap[KHIDX(1, l_pedx[pnt - 1], seg, maxped)] == 0 &&
                                        keephap[KHIDX(2, l_pedx[pnt - 1], seg, maxped)] != l_found[2 - j]) {
                                        patmat[PMIDX(i, seg, maxped)].set(pnt - 1, InheritanceCode::first_match);
                                    } else {
                                        if (keephap[KHIDX(2, l_pedx[pnt - 1], seg, maxped)] == 0 &&
                                            keephap[KHIDX(1, l_pedx[pnt - 1], seg, maxped)] != l_found[2 - j]) {
                                            patmat[PMIDX(i, seg, maxped)].set(pnt - 1, InheritanceCode::second_match);
                                        }
                                    }
                                }
                            }
                            // Impute dam from son's X
                            if (male_x) {
                                if (save_evidence) {
                                    // Male X uses the dam for either role, so
                                    // save_evidence and the cursor both refer to her.
                                    const int slot = independent ? parent_slots[2 * size_t(i) + 1] : pntloc[l_pedx[1]];
                                    fromprog[IDX2(slot, seg, totprog)] = l_found[0];
                                }
                                if (keephap[KHIDX(1, l_pedx[1], seg, maxped)] == 0) {
                                    keephap[KHIDX(1, l_pedx[1], seg, maxped)] = l_found[0];
                                    patmat[PMIDX(i, seg, maxped)].set(0, InheritanceCode::first_match);
                                }
                                if (keephap[KHIDX(2, l_pedx[1], seg, maxped)] == 0 &&
                                    keephap[KHIDX(1, l_pedx[1], seg, maxped)] != l_found[0]) {
                                    keephap[KHIDX(2, l_pedx[1], seg, maxped)] = l_found[0];
                                    patmat[PMIDX(i, seg, maxped)].set(0, InheritanceCode::second_match);
                                }
                                patmat[PMIDX(i, seg, maxped)].set(1, InheritanceCode::maternal_x);
                            }
                        }
                        // Base frequency estimation
                        if (birth[i - 1] < baseyr && listeqn[l_pedx[pnt - 1]] == 0 && l_found[pnt - 1] > 0) {
#if FINDHAP_PEDIGREE_VECTOR_BASE
                            if (independent) {
                                findhap_pedigree::add_base_frequency(
                                    &haplist_arr[IDX3(1,l_found[pnt-1],seg,maxlen,maxhap)],
                                    sumbase.get()+startloc[seg]-1,bfreq.get()+startloc[seg]-1,len_arr[seg]);
                            } else
#endif
                            for (l = 1; l <= len_arr[seg]; l++) {
                                locus = startloc[seg] + l - 1;
                                if (haplist_arr[IDX3(l, l_found[pnt - 1], seg, maxlen, maxhap)] != 1) {
                                    sumbase[locus - 1] += haplist_arr[IDX3(l, l_found[pnt - 1], seg, maxlen, maxhap)];
                                    bfreq[locus - 1]++;
                                }
                            }
                        }
                    }
                };
                auto orient_chromosome = [&](int seg, int segs) __attribute__((always_inline)) {
                    int segfirst = 0, seglast = 0, hapswap = 0;
                    for (j = seg; j <= seg + segs - 1; j++) {
                        if (patmat[PMIDX(i, j, maxped)].get(0) == InheritanceCode::ambiguous) {
                            if (segfirst == 0) { segfirst = j; seglast = j; }
                            if (j == seglast + 1) seglast = j;
                        }
                    }
                    if (segfirst == 0) return;
                    // Neighbor segment consistency check
                    int orientation[2];
                    for (pnt = 1; pnt <= 2; pnt++) {
                        const auto preceding = segfirst > seg
                            ? patmat[PMIDX(i, segfirst - 1, maxped)].get(pnt - 1) : InheritanceCode::no_parent;
                        const auto following = seglast < seg + segs - 1
                            ? patmat[PMIDX(i, seglast + 1, maxped)].get(pnt - 1) : InheritanceCode::no_parent;
                        orientation[pnt - 1] = findhap::inheritance::orientation(preceding, following);
                    }
                    // Switch heterozygotes
                    for (j = segfirst; j <= seglast; j++) {
                        hapswap = 0;
                        for (pnt = 1; pnt <= 2; pnt++) {
                            const int parent_haplotype = orientation[pnt - 1];
                            if (parent_haplotype == 0) continue;
                            if (keephap[KHIDX(3 - pnt, i, j, maxped)] ==
                                keephap[KHIDX(parent_haplotype, ped[IDX2(pnt, i, 2)], j, maxped)])
                                hapswap = 1;
                            break;
                        }
                        if (hapswap == 1) {
                            hold = keephap[KHIDX(2, i, j, maxped)];
                            keephap[KHIDX(2, i, j, maxped)] = keephap[KHIDX(1, i, j, maxped)];
                            keephap[KHIDX(1, i, j, maxped)] = hold;
                        }
                        for (pnt = 1; pnt <= 2; pnt++) {
                            if (keephap[KHIDX(pnt, i, j, maxped)] ==
                                keephap[KHIDX(1, ped[IDX2(pnt, i, 2)], j, maxped)]) {
                                patmat[PMIDX(i, j, maxped)].set(pnt - 1, InheritanceCode::resolved_first);
                            } else {
                                patmat[PMIDX(i, j, maxped)].set(pnt - 1, InheritanceCode::resolved_second);
                            }
                        }
                    }
                };
                if (scoped_final) {
                    // Separate chromosome files need order only within each chromosome.
                    // Preserve the full animal/segment traversal without global batch barriers.
                    #pragma omp for schedule(dynamic, 1)
                    for (int chromosome = chrome1; chromosome <= chromes; ++chromosome) {
                        try {
                            FILE* destination = chromosome_results[chromosome - chrome1].crossovers;
                            if (!destination) continue;
                            char* buffer = scoped_crossover_buffers.data() + size_t(omp_get_thread_num()) * scoped_crossover_bytes;
                            size_t used = 0;
                            const auto flush = [&] {
                                if (used && fwrite(buffer, 1, used, destination) != used)
                                    throw std::runtime_error("cannot write chromosome crossovers");
                                used = 0;
                            };
                            const int first = startseg[chromosome - 1], last = stopseg[chromosome - 1];
                            for (i = 1; i <= maxped; ++i) {
                                for (int segment = first; segment <= last; ++segment) {
                                    process_segment(segment, true);
                                    const int count = num_cross[segment - 1];
                                    if (count < 0 || count > 10)
                                        throw std::out_of_range("invalid chromosome crossover count");
                                    for (int record = 0; record < count; ++record) {
                                        const char* text = cross_buf.get() + size_t(segment - 1) * 800 + size_t(record) * 80;
                                        const size_t bytes = strnlen(text, 80);
                                        if (bytes == 80) throw std::runtime_error("unterminated chromosome crossover record");
                                        if (used + bytes + 1 > scoped_crossover_bytes) flush();
                                        memcpy(buffer + used, text, bytes); used += bytes; buffer[used++] = '\n';
                                    }
                                }
                                orient_chromosome(first, last - first + 1);
                            }
                            flush();
                        } catch (...) { crossover_failure.capture(); }
                    }
                } else if (ordered_final) {
                    // Independent chromosome state keeps the original animal
                    // traversal. Batch boundaries replace per-animal barriers;
                    // crossover emission still uses the exact historical order.
                    const int batch_rows = int(crossover_batches[0]->rows());
                    for (int begin = 1, slot = 0; begin <= maxped; begin += batch_rows) {
                        const int end = std::min(maxped + 1, begin + batch_rows);
                        auto& batch = *crossover_batches[slot];
                        #pragma omp for schedule(dynamic, 1)
                        for (int chromosome = chrome1; chromosome <= chromes; ++chromosome) {
                            if (crossover_failure.failed()) continue;
                            try {
                                const int first = startseg[chromosome - 1];
                                const int last = stopseg[chromosome - 1];
                                for (i = begin; i < end; ++i) {
                                    for (int segment = first; segment <= last; ++segment) {
                                        process_segment(segment, true);
                                        batch.capture(size_t(i - begin), size_t(segment - 1),
                                            num_cross[segment - 1], cross_buf.get() + size_t(segment - 1) * 800);
                                    }
                                    orient_chromosome(first, last - first + 1);
                                }
                            } catch (...) {
                                crossover_failure.capture();
                            }
                        }
                        // The loop barrier publishes the completed slot and waits for
                        // the preceding writer before a third batch can reuse it.
                        // One member of this same team writes in animal/segment order.
                        #pragma omp single nowait
                        {
                            if (!crossover_failure.failed()) {
                                try {
                                    const size_t rows = size_t(end - begin);
                                    if (!(chromosome_processing
                                              ? batch.emit_scoped(crossover_destinations.data(), rows)
                                              : batch.emit(fp27, rows)))
                                        throw std::runtime_error("cannot write crossover output");
                                } catch (...) {
                                    crossover_failure.capture();
                                }
                            }
                            if (!overlap_crossovers) crossover_batch_failed = crossover_failure.failed();
                        }
                        if (overlap_crossovers) {
                            slot ^= 1;
                        } else {
                            #pragma omp barrier
                            if (crossover_batch_failed) break;
                        }
                    }
                } else if (chromosome_pass) {
                    // One scheduling operation per chromosome, not per animal.
                    #pragma omp for schedule(dynamic, 1)
                    for (int chromosome = chrome1; chromosome <= chromes; ++chromosome) {
                        const int first = startseg[chromosome - 1];
                        const int last = stopseg[chromosome - 1];
                        for (i = 1; i <= maxped; ++i) {
                            for (int segment = first; segment <= last; ++segment)
                                process_segment(segment, true);
                            orient_chromosome(first, last - first + 1);
                        }
                    }
                } else {
#if FINDHAP_PEDIGREE_COORDINATION >= 2
                    // The same contiguous partition on every animal; no scheduling
                    // call in the inner pedigree loop. Empty partitions still barrier.
                    const int tid = omp_get_thread_num(), team = omp_get_num_threads();
                    const int width = maxseg / team, extra = maxseg % team;
                    const int equal_first = 1 + tid * width + std::min(tid, extra);
                    const int equal_past = equal_first + width + (tid < extra);
#if FINDHAP_PEDIGREE_PLACEMENT
                    findhap::pedigree_placement::ScopedCpu pedigree_cpu;
                    if (int(pedigree_team.size()) == team) {
                        const int cpu = pedigree_cores[tid].id;
                        if (pedigree_cpu.bind(cpu)) pedigree_team[tid].before = cpu;
                        #pragma omp barrier
                        #pragma omp single
                        {
                            pedigree_pinned = std::all_of(pedigree_team.begin(), pedigree_team.end(),
                                [](const auto& worker) { return worker.before >= 0; });
                        }
                        if (!pedigree_pinned) pedigree_team[tid].restored = pedigree_cpu.restore();
                    }
                    const bool use_weighted_ranges = pedigree_pinned && !pedigree_cuts.empty();
                    const int first_segment = use_weighted_ranges ? pedigree_cuts[tid] : equal_first;
                    const int past_segment = use_weighted_ranges ? pedigree_cuts[tid + 1] : equal_past;
#else
                    const int first_segment = equal_first;
                    const int past_segment = equal_past;
#endif
#endif
                    for (i = 1; i <= maxped; i++) {
#if FINDHAP_PEDIGREE_COORDINATION >= 2
                        for (int seg = first_segment; seg < past_segment; seg++) {
#else
                        #pragma omp for
                        for (seg = 1; seg <= maxseg; seg++) {
#endif
                            process_segment(seg, false);
                        } // end parallel seg loop
#if FINDHAP_PEDIGREE_COORDINATION >= 2
                        #pragma omp barrier
#endif

                        // The worksharing barrier completes all segment writes. The
                        // single-stage barrier publishes orientation/cursor updates
                        // and keeps crossover records in animal then segment order.
#if FINDHAP_PEDIGREE_COORDINATION >= 1
                        #pragma omp master
#else
                        #pragma omp single
#endif
                        {
                        // Cross.overs output
                        if (iter == iters && step == steps) {
                            for (seg = 1; seg <= maxseg; seg++) {
                                for (k = 0; k < num_cross[seg - 1]; k++) {
                                    FILE* destination = chromosome_processing ? crossover_destinations[seg - 1] : fp27;
                                    fprintf(destination, "%s\n", &cross_buf[k * 80 + ((size_t)seg - 1) * 80 * 10]);
                                }
                            }
                        }

                        // Resolve ambiguous parental assignments across adjacent segments.
                        seg = 1;
                        for (chrome = chrome1; chrome <= chromes; chrome++) {
                            segs = 1 + (locN[chrome - 1] - loc1[chrome - 1]) / maxlen;
                            orient_chromosome(seg, segs);
                            seg += segs;
                        }

                        // Update parent locations
                        for (pnt = 1; pnt <= 2; pnt++) {
                            if (ped[IDX2(pnt, i, 2)] > 0) pntloc[ped[IDX2(pnt, i, 2)]]++;
                        }
                        } // end single animal finalisation
#if FINDHAP_PEDIGREE_COORDINATION >= 1
                        // master has no implicit barrier. Publish all orientation and
                        // parent-cursor writes before processing the next animal.
                        #pragma omp barrier
#endif
                    } // end i (all animals in ped)
#if FINDHAP_PEDIGREE_PLACEMENT
                    if (pedigree_pinned) {
                        pedigree_team[tid].after = sched_getcpu();
                        pedigree_team[tid].restored = pedigree_cpu.restore();
                    }
#endif
                } // end animal-major fallback
            } // end persistent pedigree team
            crossover_failure.rethrow("crossover output");
            // The legacy progeny stage observes this shared loop endpoint.
            if (chromosome_pass) seg = maxseg + 1;
            FINDHAP_END(pedigree_main_trace);
#if FINDHAP_PEDIGREE_PLACEMENT
            if (!pedigree_team.empty())
                findhap::pedigree_placement::report(pedigree_team, pedigree_cores, step, iter, maxseg,
                                                   pedigree_pinned, pedigree_cuts);
#endif
            FINDHAP_PHASE(pedigree_progeny_trace, "pedigree_progeny", step, iter);

            // Process progeny by parent
            nhigh = 0;
            progeny_slot = 1;
            for (i = 1; i <= nped; i++) {
                if (prog[i] > 0 && (!compact_progeny || listeqn[i] == 0)) {
                    pntloc[i] = progeny_slot;
                    progeny_slot += prog[i];
                }
            }

            nhigh = 0;
            const bool fix_x_progeny = findhap::progeny::fix_x_progeny;
            // A configured X can only be the last mapped chromosome. When
            // X is absent, the legacy Xseg sentinel can be a real segment index.
            const int after_last_segment = maxseg + 1;
            const int first_x_progeny_segment = XChrom == chromes ? Xseg : after_last_segment;
#if FINDHAP_PROGENY_SEGMENT_OWNED
            // All dependencies in this stage remain within one segment. Each
            // worker keeps the original parent/offspring traversal while owning
            // a complete segment, removing one team barrier per untyped parent.
            std::vector<int> progeny_parents;
            for (int parent=1;parent<=nped;++parent)
                if (prog[parent]>0 && listeqn[parent]==0) progeny_parents.push_back(parent);
            // Legacy scoring uses the retained segment from the previous pass.
            const int maternal_evidence_start = fix_x_progeny ? first_x_progeny_segment
                : (seg >= Xseg ? 1 : after_last_segment);
            #pragma omp parallel private(i, pnt, pedx, mosthap, mosthaps, j, agree, grdpnt, search, proghaps) num_threads(threads)
            {
                findhap::macos::worker_enter();
                int* l_proghaps = &l_proghaps_all[omp_get_thread_num() * progeny_stride];
                memset(l_proghaps, 0, progeny_stride * sizeof(int));
                #pragma omp for schedule(dynamic,1)
                for (int owned_segment=1;owned_segment<=maxseg;++owned_segment) {
                    const int seg=owned_segment;
                    const bool maternal_evidence = chromosome_processing && !fix_x_progeny
                        ? local_legacy_maternal[chro[seg - 1] - chrome1] != 0 : seg >= maternal_evidence_start;
                    const bool x_segment = uses_x_context(seg);
                    for (int parent : progeny_parents) {
                        i=parent;
                        int l_pedx[2];
                        for(pnt=1;pnt<=2;++pnt)l_pedx[pnt-1]=ped[IDX2(pnt,i,2)];
                        if (maternal_evidence && sexv[i - 1] == 'M') l_pedx[0] = l_pedx[1];
                        int l_mosthap[4] = {0}, l_mosthaps[4] = {0};
                        for (j = 1; j <= prog[i]; j++) {
                            agree = 0;
                            for (pnt = 1; pnt <= 2; pnt++) {
                                if (l_pedx[pnt - 1] > 0) {
                                    for (grdpnt = 1; grdpnt <= 2; grdpnt++) {
                                        if (keephap[KHIDX(grdpnt, l_pedx[pnt - 1], seg, maxped)] ==
                                            fromprog[IDX2(pntloc[i] + j - 1, seg, totprog)])
                                            agree++;
                                    }
                                }
                            }
                            l_proghaps[fromprog[IDX2(pntloc[i] + j - 1, seg, totprog)]] += 1 + agree;
                        }
                        for (search = 1; search <= 2; search++) {
                            for (j = 1; j <= prog[i]; j++) {
                                int fp = fromprog[IDX2(pntloc[i] + j - 1, seg, totprog)];
                                if (fp != l_mosthap[search - 1] &&   // Exclude previous winner: mosthap(search-1)
                                    l_proghaps[fp] > l_mosthaps[search] && fp > 0) {  // Compare: mosthaps(search)
                                    l_mosthap[search] = fp;          // Store: mosthap(search)
                                    l_mosthaps[search] = l_proghaps[fp]; // Store: mosthaps(search)
                                }
                            }
                        }
                        if (keephap[KHIDX(1, i, seg, maxped)] != l_mosthap[2] &&
                            keephap[KHIDX(2, i, seg, maxped)] != l_mosthap[1]) {
                            if (step == 1 || keephap[KHIDX(1, i, seg, maxped)] == 0 ||
                                keephap[KHIDX(2, i, seg, maxped)] == 0) {
                                keephap[KHIDX(1, i, seg, maxped)] = l_mosthap[1];
                                keephap[KHIDX(2, i, seg, maxped)] = l_mosthap[2];
                            }
                        }
                        if ((l_mosthaps[1] >= 8 && l_mosthaps[2] == 0) ||
                            (sexv[i - 1] == 'M' && x_segment))
                            keephap[KHIDX(2, i, seg, maxped)] = keephap[KHIDX(1, i, seg, maxped)];
                        for (j = 1; j <= prog[i]; j++) {
                            l_proghaps[fromprog[IDX2(pntloc[i] + j - 1, seg, totprog)]] = 0;
                        }
                    }
                }
            }
#else
            #pragma omp parallel private(i, pnt, pedx, mosthap, mosthaps, j, agree, grdpnt, search, proghaps) \
                num_threads(threads)
            {
                findhap::macos::worker_enter();
                int tid = omp_get_thread_num();
                int* l_proghaps = &l_proghaps_all[tid * progeny_stride];
                memset(l_proghaps, 0, progeny_stride * sizeof(int));

                for (i = 1; i <= nped; i++) {
                    if (prog[i] > 0) {
                        if (listeqn[i] == 0) {
                            const int sire = ped[IDX2(1, i, 2)];
                            const int dam = ped[IDX2(2, i, 2)];
                            int maternal_evidence_start = after_last_segment;
                            if (sexv[i - 1] == 'M') {
                                maternal_evidence_start = fix_x_progeny ? first_x_progeny_segment
                                    : (seg >= Xseg ? 1 : after_last_segment);
                            }

                            #pragma omp for
                            for (seg = 1; seg <= maxseg; seg++) {
                                const bool maternal_evidence = chromosome_processing && !fix_x_progeny
                                    ? sexv[i - 1] == 'M' && local_legacy_maternal[chro[seg - 1] - chrome1] != 0
                                    : seg >= maternal_evidence_start;
                                const int l_pedx[2] = {maternal_evidence ? dam : sire, dam};
                                int l_mosthap[4] = {0}, l_mosthaps[4] = {0};
                                for (j = 1; j <= prog[i]; j++) {
                                    agree = 0;
                                    for (pnt = 1; pnt <= 2; pnt++) {
                                        if (l_pedx[pnt - 1] > 0) {
                                            for (grdpnt = 1; grdpnt <= 2; grdpnt++) {
                                                if (keephap[KHIDX(grdpnt, l_pedx[pnt - 1], seg, maxped)] ==
                                                    fromprog[IDX2(pntloc[i] + j - 1, seg, totprog)])
                                                    agree++;
                                            }
                                        }
                                    }
                                    l_proghaps[fromprog[IDX2(pntloc[i] + j - 1, seg, totprog)]] += 1 + agree;
                                }
                                for (search = 1; search <= 2; search++) {
                                    for (j = 1; j <= prog[i]; j++) {
                                        int fp = fromprog[IDX2(pntloc[i] + j - 1, seg, totprog)];
                                        if (fp != l_mosthap[search - 1] &&   // Exclude previous winner: mosthap(search-1)
                                            l_proghaps[fp] > l_mosthaps[search] && fp > 0) {  // Compare: mosthaps(search)
                                            l_mosthap[search] = fp;          // Store: mosthap(search)
                                            l_mosthaps[search] = l_proghaps[fp]; // Store: mosthaps(search)
                                        }
                                    }
                                }
                                if (keephap[KHIDX(1, i, seg, maxped)] != l_mosthap[2] &&
                                    keephap[KHIDX(2, i, seg, maxped)] != l_mosthap[1]) {
                                    if (step == 1 || keephap[KHIDX(1, i, seg, maxped)] == 0 ||
                                        keephap[KHIDX(2, i, seg, maxped)] == 0) {
                                        keephap[KHIDX(1, i, seg, maxped)] = l_mosthap[1];
                                        keephap[KHIDX(2, i, seg, maxped)] = l_mosthap[2];
                                    }
                                }
                                if ((l_mosthaps[1] >= 8 && l_mosthaps[2] == 0) ||
                                    (sexv[i - 1] == 'M' && uses_x_context(seg)))
                                    keephap[KHIDX(2, i, seg, maxped)] = keephap[KHIDX(1, i, seg, maxped)];
                                for (j = 1; j <= prog[i]; j++) {
                                    l_proghaps[fromprog[IDX2(pntloc[i] + j - 1, seg, totprog)]] = 0;
                                }
                            }
                        }
                    }
                }
            }
#endif
            // Every worker walks the pedigree loop above. Count once, outside
            // the parallel region, to avoid duplicate counts and shared writes.
            if (!chromosome_processing)
                for (i = 1; i <= nped; ++i)
                    if (called[i - 1] > 0.4f * loci) ++nhigh;

            report_events.phase("Pedigree matching", "completed", step, iter);
            report_events.emit("iteration_end", step, iter, maxseg, nhap_arr.get(),
                FINDHAP_NHAP_STRIDE, threads, n13, nped, loci,
                chromosome_processing ? -1 : nhigh, chromosome_processing ? -1 : nmed
                , chro.get(), maxhap
                );
        } // end iter loop
        report_events.phase("Result output", "started", step);

#ifdef FINDHAP_LOW_MEMORY
        legacy_printf(" TurboHap LM working storage: %zu bytes, %llu read, %llu written, %s\n",
               cache_expanded.bytes(), (unsigned long long)cache_expanded.read_bytes(),
               (unsigned long long)cache_expanded.write_bytes(),
#ifdef FINDHAP_LOW_MEMORY_PLUS
               cache_expanded.packed() ? "bounded animal batches" : "overlapping-prior fallback");
#else
               cache_expanded.packed() ? "chromosome windows" : "overlapping-prior fallback");
#endif
#endif

        // ===================================================================
        // OUTPUT PHASE (step == steps only)
        // ===================================================================
#if FINDHAP_PARALLEL_ASSIGNMENT_COUNTS
        std::vector<uint8_t> count_assignment(nped);
        for (int animal = 1; animal <= nped; ++animal)
            count_assignment[animal - 1] = listeqn[animal] > 0;
#endif
        if (step == steps) {
            FINDHAP_PHASE(output_trace, "output", step);
            legacy_printf("\n");
            const auto write_quality_headers = [](const ResultFiles& result, int x_segment) {
                fprintf(result.reference, "%12d  high-density genotypes in reference\n", result.reference_count);
                fprintf(result.reference, "%12d  segment where X-specific loci begin\n", x_segment);
                fprintf(result.quality, "                   total     markers   total SNP\n");
                fprintf(result.quality, "    animal  chip  progeny    observed    filled  corr2(est,true)\n");
            };
            if (chromosome_processing) {
                chromosome_retained.resize(checked_array_count<uint8_t>(
                    "chromosome output eligibility", maxped, chromosome_count));
                for (int c = chrome1; c <= chromes; ++c) {
                    auto& result = chromosome_results[c - chrome1];
                    if (!result.quality) continue;
                    const int markers = locN[c - 1] - loc1[c - 1] + 1;
                    int references = 0;
                    for (int animal = 0; animal < nped; ++animal)
                        if (chromosome_called[size_t(animal) * chromosome_count + c - chrome1] > 0.4f * markers)
                            ++references;
                    result.reference_count = std::max(prior_high1, references);
                    write_quality_headers(result, c == XChrom ? 1 : markers);
                }
            } else {
                high1 = std::max(high1, nhigh);
                legacy_results.reference_count = high1;
                write_quality_headers(legacy_results, Xseg);
                legacy_printf(" First 10 genotypes created\n\n");
            }

#if FINDHAP_LIBRARY_METADATA
            for(int ss=0;ss<maxseg;++ss)
                std::memset(hapfreq_arr.get()+size_t(ss)*maxhap,0,size_t(nhap_arr[size_t(ss)*FINDHAP_NHAP_STRIDE])*sizeof(int));
#else
            memset(hapfreq_arr.get(), 0, (size_t)maxhap * maxseg * sizeof(int));
#endif
            n13a = 0;
            n31 = 0;
            memset(missall.get(), 0, loci * sizeof(int));
            memset(missadd.get(), 0, loci * sizeof(int));
            nadd = 0;
            nmale = 0;
            ndrop = 0;
            memset(genfreq, 0, sizeof(genfreq));
            nped1 = nped;

            // Copy animkey to keyvec and sort
            for (i = 0; i < nped; i++) keyvec[i] = animkey[i + 1];
            std::sort(keyvec.get(), keyvec.get() + nped);

            int* p_missall = missall.get();
            int* p_missadd = missadd.get();

            // Output storage is needed only in the last step. Bound each batch
            // by bytes as well as animals; a single wider record still fits.
            constexpr size_t output_buffer_budget = FINDHAP_OUTPUT_BUFFER_BYTES;
            const size_t record_headers = chromosome_processing ? size_t(chromosome_count) * 32 : 32;
            const size_t geno_record_bytes = (size_t)loci + record_headers;
            const size_t hap_record_bytes = (size_t)2 * loci + record_headers;
            const size_t bytes_per_animal =
                (genout > 0 ? geno_record_bytes : 0) +
                (hapout == 1 ? hap_record_bytes : 0) +
                3 * sizeof(int) + sizeof(float) + 50 * sizeof(int8_t) +
                (chromosome_processing ? size_t(chromosome_count) * (sizeof(AnimalQuality) + sizeof(int)) : 0);
#if FINDHAP_ASYNC_DENSE_OUTPUT
            const bool async_dense = threads > 1 && (genout > 0 || hapout == 1) &&
                                     bytes_per_animal <= output_buffer_budget / 2;
            const int slots = async_dense ? 2 : 1;
            const int output_threads = threads - (async_dense ? 1 : 0);
#else
            constexpr int slots = 1;
            const int output_threads = threads;
#endif
            const int output_chunk_size = static_cast<int>(std::max<size_t>(1,
                std::min({(size_t)1000, (size_t)nped, output_buffer_budget / slots / bytes_per_animal})));
            findhap::PhaseBuffer<char> chunk_buffer_geno, chunk_buffer_hap;
            // Every emitted byte is filled before the serial writer reads it.
            if (genout > 0)
                chunk_buffer_geno = findhap::make_phase_buffer<char>(slots * output_chunk_size * geno_record_bytes);
            if (hapout == 1)
                chunk_buffer_hap = findhap::make_phase_buffer<char>(slots * output_chunk_size * hap_record_bytes);
            const size_t output_loci_stride = worker_stride(loci), output_raw_stride = worker_stride(maxl);
            auto thread_genotype = std::make_unique<int8_t[]>(output_loci_stride * threads);
            auto thread_raw_genotypes = std::make_unique<int8_t[]>(output_raw_stride * threads);
            auto usable_slots = std::make_unique<int[]>(slots * output_chunk_size);
            std::vector<int> scoped_usable(chromosome_processing ? size_t(slots) * output_chunk_size * chromosome_count : 0);
            std::vector<turbohap_output::ChromosomeDenseLayout> dense_layout(chromosome_processing ? chromosome_count : 0);
            if (chromosome_processing) {
                size_t go = 0, ho = 0;
                for (int c = chrome1; c <= chromes; ++c) {
                    const auto& result = chromosome_results[c - chrome1];
                    if (!result.quality) continue;
                    const size_t markers = locN[c - 1] - loc1[c - 1] + 1;
                    dense_layout[c - chrome1] = {result.genotypes, result.haplotypes, go, ho,
                                                 markers + 32, 2 * markers + 32};
                    go += output_chunk_size * (markers + 32);
                    ho += output_chunk_size * (2 * markers + 32);
                }
            }
            auto imputed_chunk = std::make_unique<int[]>(output_chunk_size);
            const bool defer_ancestor_filter = findhap::defer_ancestor_quality_filter;
            const bool defer_genotyped_filter = findhap::defer_genotyped_quality_filter;
            FILE* ancestor_filter = nullptr;
            if (defer_ancestor_filter) {
                ancestor_filter = outputs.open(ancestor_filter_filename);
                fputs("# turbohap-ancestor-filter/v1\nanimal_id\teligible_progeny\tresolved_alleles\n",
                      ancestor_filter);
            }
            std::vector<int> resolved_chunk(defer_ancestor_filter ? output_chunk_size : 0);
            std::vector<AnimalQuality> chromosome_quality(chromosome_processing
                ? size_t(output_chunk_size) * chromosome_count : 0);
            auto corr2_chunk = std::make_unique<float[]>(output_chunk_size);
            auto i_chunk = std::make_unique<int[]>(output_chunk_size);
            auto preview_chunk = std::make_unique<int8_t[]>((size_t)output_chunk_size * 50);
            WorkerFailure output_failure;
#ifdef FINDHAP_LOW_MEMORY_PLUS
            std::vector<size_t> output_raw_rows(output_chunk_size);
            int prepared_output_count = 0;
            std::exception_ptr output_cache_error;
#endif

#if FINDHAP_BUFFER_INHERIT
            std::vector<char> inherit_payload(2 * size_t(maxseg) +
                                             (FINDHAP_INHERIT_RECORD_OUTPUT ? 64 : 1));
#endif
            // Different chromosome files have one owner each during this batch.
            // Only the small inheritance scratch buffer is replicated per worker.
            const size_t scoped_inherit_stride = 2 * size_t(maxseg) + 64;
            std::vector<char> scoped_inheritance(chromosome_processing ? scoped_inherit_stride * output_threads : 0);
            const auto write_inheritance = [&](FILE* output, int animal, int c, char* scoped_buffer = nullptr) {
#if FINDHAP_BUFFER_INHERIT
                char* payload = scoped_buffer ? scoped_buffer : inherit_payload.data();
#endif
                for (int parent = 1; parent <= 2; ++parent) {
#if FINDHAP_INHERIT_RECORD_OUTPUT && FINDHAP_BUFFER_INHERIT
                    char* end = payload;
                    end = turbohap_output::append_integer(end, animkey[animal], 10);
                    end = turbohap_output::append_integer(end, c, 4);
                    end = turbohap_output::append_integer(end, parent, 2);
                    *end++ = ' ';
                    *end++ = ' ';
                    size_t bytes = static_cast<size_t>(end - payload);
#else
                    fprintf(output, "%10d%4d%2d  ", animkey[animal], c, parent);
#if FINDHAP_BUFFER_INHERIT
                    size_t bytes = 0;
#endif
#endif
                    for (int s = startseg[c - 1]; s <= stopseg[c - 1]; ++s) {
                        const auto code = patmat[PMIDX(animal, s, maxped)].get(parent - 1);
                        const auto& text = findhap::inheritance::definition(code).text;
#if FINDHAP_BUFFER_INHERIT
                        std::memcpy(payload + bytes, text, 2);
                        bytes += 2;
#else
                        fprintf(output, "%c%c", text[0], text[1]);
#endif
                    }
#if FINDHAP_BUFFER_INHERIT
                    payload[bytes++] = '\n';
                    fwrite(payload, 1, bytes, output);
#else
                    fputc('\n', output);
#endif
                }
            };
#if FINDHAP_ASYNC_DENSE_OUTPUT
            // Declared after the buffers, so the writer joins before their
            // destruction on both normal completion and an exception.
            std::unique_ptr<turbohap_output::ChromosomeOutputWriter> chromosome_writer;
            if (async_dense && chromosome_processing)
                chromosome_writer = std::make_unique<turbohap_output::ChromosomeOutputWriter>(dense_layout, output_chunk_size);
            std::unique_ptr<turbohap_output::DenseOutputWriter> dense_writer;
            if (async_dense && !chromosome_processing) {
                dense_writer = std::make_unique<turbohap_output::DenseOutputWriter>(
                    genout > 0 ? fp31 : nullptr, geno_record_bytes,
                    hapout == 1 ? fp29 : nullptr, hap_record_bytes);
            }
#endif

            // Reuse one team and its reduction arrays across all output batches.
            #pragma omp parallel private(ii, i, imputed, seg, pnt, loc, locus, corr2, \
                usable, chrome, n, l, m, k) \
                reduction(+: nmale, ndrop, nadd, n13a, p_missall[:loci], p_missadd[:loci], genfreq[:10]) \
                num_threads(output_threads)
            {
                findhap::macos::worker_enter();
                for (int chunk_start = 1, slot = 0; chunk_start <= nped;) {
                    const size_t slot_start = size_t(slot) * output_chunk_size;
                    int* usable_chunk = usable_slots.get() + slot_start;
                    int* scoped_mask = chromosome_processing ? scoped_usable.data() + slot_start * chromosome_count : nullptr;
                    char* batch_geno = genout > 0 ? chunk_buffer_geno.get() + slot_start * geno_record_bytes : nullptr;
                    char* batch_hap = hapout == 1 ? chunk_buffer_hap.get() + slot_start * hap_record_bytes : nullptr;
                    FINDHAP_PHASE(reconstruction_trace, "output_reconstruction", step, 0, 0, chunk_start);
#ifdef FINDHAP_LOW_MEMORY_PLUS
                    // Prepare immutable raw bytes before workers format this
                    // output batch. Disk reads can be sorted independently of
                    // the unchanged ID order used by the serial writer.
                    #pragma omp single
                    {
                        try {
                            const int requested = std::min(output_chunk_size, nped - chunk_start + 1);
                            for (int item = 0; item < requested; ++item) {
                                const int eqn = eqns[nhash(keyvec[chunk_start + item - 1], 0,
                                                          ids.get(), maxped2, maxn)];
                                const int record = cache_anml_idx[eqn];
                                output_raw_rows[item] = record > 0 ? size_t(record - 1) : ExternalGenotypeCache::absent;
                            }
                            prepared_output_count = genotype_cache->prepare_gather(output_raw_rows.data(), requested);
                        } catch (...) { output_cache_error = std::current_exception(); }
                    }
                    if (output_cache_error) break;
                    const int chunk_count = prepared_output_count;
#else
                    const int chunk_count = std::min(output_chunk_size, nped - chunk_start + 1);
#endif
                    const int chunk_end = chunk_start + chunk_count - 1;

                    #pragma omp for schedule(FINDHAP_ROW_SCHEDULE)
                    for (ii = chunk_start; ii <= chunk_end; ii++) {
                        int tid = omp_get_thread_num();
                        int chunk_idx = ii - chunk_start;
                        i = eqns[nhash(keyvec[ii - 1], 0, ids.get(), maxped2, maxn)];
                        i_chunk[chunk_idx] = i;
                        const auto resolved_alleles = [&](int first_segment, int last_segment) {
                            int count = 0;
                            for (int s = first_segment; s <= last_segment; ++s) {
                                for (int parent = 1; parent <= 2; ++parent) {
                                    const auto id = keephap[KHIDX(parent, i, s, maxped)];
                                    if (id > 0) {
                                        const int8_t* alleles = haplist_arr.get() + IDX3(1, id, s, maxlen, maxhap);
                                        for (int marker = 0; marker < len_arr[s]; ++marker)
                                            count += alleles[marker] != 1;
                                    }
                                }
                            }
                            return count;
                        };
                        usable = 0;
                        if (chromosome_processing) {
                            const bool genotyped = listeqn[i] > 0;
                            const bool allowed = genotyped || (damout >= 1 && sexv[i - 1] == 'F') ||
                                                 (damout == 2 && sexv[i - 1] == 'M');
                            for (int c = chrome1; c <= chromes; ++c) {
                                const int column = c - chrome1;
                                const auto& result = chromosome_results[column];
                                if (!result.quality) continue;
                                const size_t animal_offset = size_t(column) * maxped + i - 1;
                                const int observed = chromosome_called[size_t(i - 1) * chromosome_count + column];
                                int filled = resolved_alleles(startseg[c - 1], stopseg[c - 1]) / 2;
                                if (genotyped && chromosome_high[animal_offset] == 1)
                                    filled = std::max(filled, observed);
                                const int markers = locN[c - 1] - loc1[c - 1] + 1;
                                const int64_t physical_length = int64_t(locateN[c - 1]) - locate1[c - 1] + 1;
                                const float score = estimate_output_quality(observed, filled, result.reference_count,
                                                                           gprog[i], physical_length, markers);
                                const bool reported = allowed && score > 0.84f * 0.84f;
                                chromosome_quality[size_t(chunk_idx) * chromosome_count + column] =
                                    {observed, filled, score, reported};
                                chromosome_retained[animal_offset] = reported;
                                scoped_mask[size_t(column) * output_chunk_size + chunk_idx] = reported;
                                usable |= reported;
                            }
                        } else {
                            imputed = resolved_alleles(1, maxseg);
                            bool retain_for_merge = false;
                            if (defer_ancestor_filter) {
                                const bool allowed_sex = (damout >= 1 && sexv[i - 1] == 'F') ||
                                                         (damout == 2 && sexv[i - 1] == 'M');
                                // Two or fewer eligible progeny cannot clear the cutoff even at full coverage.
                                retain_for_merge = allowed_sex && listeqn[i] == 0 && called[i - 1] == 0 &&
                                                   gprog[i] >= 3;
                                // Divide by two only after summing raw allele counts across buckets.
                                resolved_chunk[chunk_idx] = retain_for_merge ? imputed : -1;
                            }
                            imputed = imputed / 2;
                            if (listeqn[i] > 0 && high[i - 1] == 1)
                                imputed = std::max(imputed, called[i - 1]);
                            corr2 = estimate_output_quality(called[i - 1], imputed, high1, gprog[i], totlngth, loci);
                            if (corr2 > 0.84f * 0.84f) {
                                if (listeqn[i] > 0) usable = 1;
                                if (damout >= 1 && sexv[i - 1] == 'F') usable = 1;
                                if (damout == 2 && sexv[i - 1] == 'M') usable = 1;
                                if (listeqn[i] == 0 && sexv[i - 1] == 'M') nmale++;
                            } else if (retain_for_merge) {
                                usable = 1;
                                if (sexv[i - 1] == 'M') nmale++;
                            } else if (listeqn[i] > 0) {
                                if (defer_genotyped_filter) usable = 1;
                                else ndrop++;
                            }
                            imputed_chunk[chunk_idx] = imputed;
                            corr2_chunk[chunk_idx] = corr2;
                        }
                        usable_chunk[chunk_idx] = usable;

                        // Create genotypes.filled from haplotypes
                        if (usable == 1) {
                            int l_pnthap[2];
                            for (seg = 1; seg <= maxseg; seg++) {
                                const bool male_x = sexv[i - 1] == 'M' && uses_x_context(seg);
#if FINDHAP_OUTPUT_STREAMS
                                const auto first = keephap[KHIDX(1, i, seg, maxped)];
                                const auto second = keephap[KHIDX(2, i, seg, maxped)];
                                const int8_t* a = first > 0 ? &haplist_arr[IDX3(1, first, seg, maxlen, maxhap)] : nullptr;
                                const int8_t* b = second > 0 ? &haplist_arr[IDX3(1, second, seg, maxlen, maxhap)] : nullptr;
                                turbohap_output::reconstruct(a, b, thread_genotype.get() + size_t(tid)*output_loci_stride + startloc[seg]-1,
                                    len_arr[seg], gencode, output_gencode, male_x);
#else
                                for (loc = 1; loc <= len_arr[seg]; loc++) {
                                    locus = startloc[seg] + loc - 1;
                                    for (pnt = 1; pnt <= 2; pnt++) {
                                        if (keephap[KHIDX(pnt, i, seg, maxped)] > 0) {
                                            l_pnthap[pnt - 1] = haplist_arr[IDX3(loc, keephap[KHIDX(pnt, i, seg, maxped)], seg, maxlen, maxhap)];
                                        } else {
                                            l_pnthap[pnt - 1] = missing;
                                        }
                                    }
                                    thread_genotype[size_t(tid) * output_loci_stride + locus - 1] = gencode[l_pnthap[0]][l_pnthap[1]];
                                    if (male_x) {
                                        if (thread_genotype[size_t(tid) * output_loci_stride + locus - 1] == 1 ||
                                            thread_genotype[size_t(tid) * output_loci_stride + locus - 1] > 2)
                                            thread_genotype[size_t(tid) * output_loci_stride + locus - 1] = 5;
                                    }
                                }
#endif
                            }
                        }

                        n = cache_anml_idx[i];
                        if (n > 0) {
                            const int chipn = cache_chipn[n - 1];
                            loc = cache_loc[n - 1];
                            const int8_t* raw_genotype;
#ifdef FINDHAP_LOW_MEMORY_PLUS
                            try {
#endif
                                raw_genotype = genotype_cache->row(n - 1, loc,
                                    thread_raw_genotypes.get() + (size_t)tid * output_raw_stride);
#ifdef FINDHAP_LOW_MEMORY_PLUS
                            } catch (...) {
                                output_failure.capture();
                                continue;
                            }
#endif
                            // Each chip ordinal maps to a different genome locus.
                            // A missing or non-observed call leaves its imputation intact.
                            const int* chip_loci = chiplist_arr.get() + CHIPIDX(1, chipn, loci);
                            int8_t* output_genotype = thread_genotype.get() + size_t(tid) * output_loci_stride;
                            for (int marker = 0; marker < loc; ++marker) {
                                const int locus = chip_loci[marker];
                                const int8_t call = raw_genotype[marker];
                                if (locus >= 1 && locus <= loci && call >= 0 && call <= 2)
                                    output_genotype[locus - 1] = call;
                            }
                            n13a++;
                        }

                        if (usable == 1) {
                            if (!chromosome_processing) {
                                if (listeqn[i] == 0) nadd++;
                                memcpy(&preview_chunk[chunk_idx * 50],
                                       &thread_genotype[size_t(tid) * output_loci_stride], std::min(50, loci));
                            }
                            char* geno_record = genout > 0 ? batch_geno + chunk_idx * geno_record_bytes : nullptr;
                            if (geno_record && !chromosome_processing)
                                snprintf(geno_record, 32, "%10d%10d%10d ", animkey[i], chip_arr[i - 1], loci);
                            int8_t* output_genotype = thread_genotype.get() + size_t(tid) * output_loci_stride;
                            const auto encode_genotypes = [&](int first, int last, char* record, int record_first) {
                                for (int marker = first; marker <= last; ++marker) {
                                    // Count codes before genout transforms them.
                                    auto& call = output_genotype[marker - 1];
                                    genfreq[call]++;
                                    if (genout == 5 && call > 2) call = 5;
                                    if (genout == 2) {
                                        if (call == 3) call = static_cast<int8_t>(freq[marker - 1] + 0.5f);
                                        if (call == 4) call = static_cast<int8_t>(freq[marker - 1] + 1.5f);
                                        if (call == 5) {
                                            call = 1;
                                            if (freq[marker - 1] < 0.3333f) call = 0;
                                            if (freq[marker - 1] > 0.6666f) call = 2;
                                        }
                                    }
                                    if (call == missing) {
                                        p_missall[marker - 1]++;
                                        if (listeqn[i] == 0) p_missadd[marker - 1]++;
                                    }
                                    if (record) record[31 + marker - record_first] = static_cast<char>(call + '0');
                                }
                            };
                            if (chromosome_processing) {
                                const auto* quality = chromosome_quality.data() + size_t(chunk_idx) * chromosome_count;
                                for (int c = chrome1; c <= chromes; ++c) {
                                    if (!quality[c - chrome1].reported) continue;
                                    const auto& layout = dense_layout[c - chrome1];
                                    char* record = batch_geno ? batch_geno + layout.genotype_offset + size_t(chunk_idx) * layout.genotype_width : nullptr;
                                    if (record) {
                                        snprintf(record, 32, "%10d%10d%10d ", animkey[i], chip_arr[i - 1], locN[c - 1] - loc1[c - 1] + 1);
                                        record[layout.genotype_width - 1] = '\n';
                                    }
                                    encode_genotypes(loc1[c - 1], locN[c - 1], record, loc1[c - 1]);
                                }
                            } else encode_genotypes(1, loci, geno_record, 1);
                        }

                        // Write haplotypes.txt
                        if (hapout == 1 && usable == 1) {
                            char* hap_record = batch_hap + chunk_idx * hap_record_bytes;
                            if (!chromosome_processing)
                                snprintf(hap_record, 32, "%10d%10d%10d ", animkey[i], 1, loci);
                            m = 0;
                            for (seg = 1; seg <= maxseg; seg++) {
                                if (chromosome_processing) {
                                    const int c = chro[seg - 1], column = c - chrome1;
                                    if (!chromosome_quality[size_t(chunk_idx) * chromosome_count + column].reported) continue;
                                    const auto& layout = dense_layout[column];
                                    hap_record = batch_hap + layout.haplotype_offset + size_t(chunk_idx) * layout.haplotype_width;
                                    m = 2 * (startloc[seg] - loc1[c - 1]);
                                    if (seg == startseg[c - 1]) {
                                        snprintf(hap_record, 32, "%10d%10d%10d ", animkey[i], 1, locN[c - 1] - loc1[c - 1] + 1);
                                        hap_record[layout.haplotype_width - 1] = '\n';
                                    }
                                }
#if FINDHAP_OUTPUT_STREAMS
                                if (listeqn[i] > 0) {
                                    const auto first = keephap[KHIDX(1, i, seg, maxped)];
                                    const auto second = keephap[KHIDX(2, i, seg, maxped)];
                                    const int8_t* a = first > 0 ? &haplist_arr[IDX3(1, first, seg, maxlen, maxhap)] : nullptr;
                                    const int8_t* b = second > 0 ? &haplist_arr[IDX3(1, second, seg, maxlen, maxhap)] : nullptr;
                                    turbohap_output::encode_haplotypes(a, b, hap_record + 31 + m, len_arr[seg]);
                                    m += 2 * len_arr[seg];
                                    continue;
                                }
#endif
                                for (l = 1; l <= len_arr[seg]; l++) {
                                    for (k = 1; k <= 2; k++) {
                                        int8_t hap_value;
                                        if (keephap[KHIDX(k, i, seg, maxped)] > 0) {
                                            hap_value =
                                                haplist_arr[IDX3(l, keephap[KHIDX(k, i, seg, maxped)], seg, maxlen, maxhap)];
                                        } else {
                                            hap_value = 1;
                                        }
                                        if (hap_value == 1) {
                                            hap_value = k;
                                            if (listeqn[i] == 0) {
                                                if (freq[startloc[seg] + l - 2] < 0.33333f)
                                                    hap_value = 1;
                                                if (freq[startloc[seg] + l - 2] > 0.66666f)
                                                    hap_value = 2;
                                            }
                                        } else {
                                            hap_value = hap_value / 2 + 1;
                                        }
                                        hap_record[31 + (size_t)m] = static_cast<char>(hap_value + '0');
                                        m++;
                                    }
                                }
                            }
                        }
                    } // end parallel for
                    FINDHAP_END(reconstruction_trace);

                    if (chromosome_processing) {
                        #pragma omp for schedule(dynamic, 1)
                        for (int c = chrome1; c <= chromes; ++c) {
                            try {
                                auto& result = chromosome_results[c - chrome1];
                                if (!result.quality) continue;
                                char* payload = scoped_inheritance.data() + size_t(omp_get_thread_num()) * scoped_inherit_stride;
                                for (int item = 0; item < chunk_count; ++item) {
                                    const int animal = i_chunk[item];
                                    const bool genotyped = listeqn[animal] > 0;
                                    const auto& record = chromosome_quality[size_t(item) * chromosome_count + c - chrome1];
                                    if (!record.reported && !genotyped) continue;
                                    if (fprintf(record.reported ? result.quality : result.rejected,
                                            "%10d%5d%8d%12d%12d%8.4f\n", animkey[animal], chip_arr[animal - 1],
                                            gprog[animal], record.observed, record.filled, record.score) < 0)
                                        throw std::runtime_error("cannot write chromosome quality record");
                                    if (!record.reported) { ++result.excluded; continue; }
                                    write_inheritance(result.inheritance, animal, c, payload);
                                    ++result.reported;
                                    if (!genotyped) {
                                        ++result.ancestors;
                                        if (sexv[animal - 1] == 'M') ++result.male_ancestors;
                                    }
                                }
                            } catch (...) { output_failure.capture(); }
                        }
                    }

                    // One worker writes the completed batch in ID order. Both
                    // worksharing barriers protect the buffer before it is reused.
                    #pragma omp single
                    {
                        if (!output_failure.failed()) try {
                        FINDHAP_PHASE(submission_trace, "output_submission", step, 0, 0, chunk_start);
                        for (int chunk_idx = 0; chunk_idx < chunk_count; chunk_idx++) {
                            i = i_chunk[chunk_idx];
                            if (chromosome_processing) {
                                n31 += usable_chunk[chunk_idx] != 0;
                                const bool genotyped = listeqn[i] > 0;
                                if (genotyped) continue;
                                const auto* quality = chromosome_quality.data() + size_t(chunk_idx) * chromosome_count;
                                for (int c = chrome1; c <= chromes; ++c) {
                                    auto& result = chromosome_results[c - chrome1];
                                    if (!result.quality || !quality[c - chrome1].reported) continue;
                                    const int id = animkey[i];
                                    if (!genotyped) {
                                        if (!result.assignments)
                                            result.assignments = outputs.open((result.directory / "fort.26").string());
                                        const int first = startseg[c - 1], last = stopseg[c - 1];
                                        const HaplotypeId* assignments = keephap.get() + KHIDX(1, 1, first, maxped);
                                        for (int parent = 1; parent <= 2; ++parent)
                                            fast_write_hapfound_line(result.assignments, text_buffers, id, parent, steps,
                                                last - first + 1, assignments, parent, i, maxped, last - first + 1);
                                    }
                                }
                                continue;
                            }
                            imputed = imputed_chunk[chunk_idx];
                            corr2 = corr2_chunk[chunk_idx];
                            usable = usable_chunk[chunk_idx];
                            if (ancestor_filter && resolved_chunk[chunk_idx] >= 0) {
                                fprintf(ancestor_filter, "%d\t%d\t%d\n",
                                        animkey[i], gprog[i], resolved_chunk[chunk_idx]);
                            }

                            if (usable == 1) {
#if FINDHAP_PARALLEL_ASSIGNMENT_COUNTS
                                count_assignment[i - 1] = 1;
#endif
                                for (pnt = 1; pnt <= 2; pnt++) {
                                    if (listeqn[i] == 0) {
                                        // Fortran implicitly opens unit 26 even when listout=0.
                                        if (!fp26) fp26 = outputs.open("fort.26");
                                        fast_write_hapfound_line(fp26, text_buffers, animkey[i], pnt, steps,
                                            maxseg, keephap.get(), pnt, i, maxped, maxseg);
#if !FINDHAP_PARALLEL_ASSIGNMENT_COUNTS
                                        for (seg = 1; seg <= maxseg; seg++) {
                                            if (keephap[KHIDX(pnt, i, seg, maxped)] > 0)
                                                hapfreq_arr[IDX2(keephap[KHIDX(pnt, i, seg, maxped)], seg, maxhap)]++;
                                        }
#endif
                                    }
                                }
                                fprintf(fp24, "%10d%5d%8d%12d%12d%8.4f\n",
                                        animkey[i], chip_arr[i - 1], gprog[i], called[i - 1], imputed, corr2);
                            } else {
                                if (listeqn[i] > 0)
                                    fprintf(fp23, "%10d%5d%8d%12d%12d%8.4f\n",
                                            animkey[i], chip_arr[i - 1], gprog[i], called[i - 1], imputed, corr2);
                            }

                            // hap.inherit
                            if (usable == 1)
                                for (int c = chrome1; c <= chromes; ++c) write_inheritance(fp28, i, c);

                            if (usable == 1) {
                                n31++;
                                if (n31 <= 10) {
                                    legacy_printf("%10d  ", animkey[i]);
                                    for (locus = 1; locus <= std::min(50, loci); locus++) {
                                        legacy_printf("%d", preview_chunk[chunk_idx * 50 + locus - 1]);
                                    }
                                    legacy_printf("\n");
                                }
                                if (genout > 0) {
                                    char* geno_ptr = batch_geno + chunk_idx * geno_record_bytes;
                                    geno_ptr[geno_record_bytes - 1] = '\n';
#if !FINDHAP_OUTPUT_BATCH_WRITE
#if FINDHAP_ASYNC_DENSE_OUTPUT
                                    if (!dense_writer)
#endif
                                    fwrite(geno_ptr, 1, geno_record_bytes, fp31);
#endif
                                }
                            }
                            if (hapout == 1 && usable == 1) {
                                char* hap_ptr = batch_hap + chunk_idx * hap_record_bytes;
                                hap_ptr[hap_record_bytes - 1] = '\n';
#if !FINDHAP_OUTPUT_BATCH_WRITE
#if FINDHAP_ASYNC_DENSE_OUTPUT
                                if (!dense_writer)
#endif
                                fwrite(hap_ptr, 1, hap_record_bytes, fp29);
#endif
                            }
                        }
#if FINDHAP_ASYNC_DENSE_OUTPUT
                        if (dense_writer) {
                            dense_writer->submit({batch_geno, batch_hap, usable_chunk, chunk_count});
                        }
                        if (chromosome_writer)
                            chromosome_writer->submit({batch_geno, batch_hap, scoped_mask, chunk_count});
                        else
#endif
                        if (chromosome_processing) {
                            if (const char* error = turbohap_output::write_chromosome_batch(
                                    {batch_geno, batch_hap, scoped_mask, chunk_count}, dense_layout, output_chunk_size))
                                throw std::runtime_error(error);
                        }
#if FINDHAP_OUTPUT_BATCH_WRITE
                        if (!chromosome_processing
#if FINDHAP_ASYNC_DENSE_OUTPUT
                            && !dense_writer
#endif
                        ) {
                        // The records are already contiguous. Coalesce only
                        // consecutive usable runs; never write an unfilled row.
                        for (int first = 0; first < chunk_count;) {
                            while (first < chunk_count && usable_chunk[first] != 1) ++first;
                            int last = first;
                            while (last < chunk_count && usable_chunk[last] == 1) ++last;
                            if (last > first) {
                                if (genout > 0) {
                                    const size_t bytes = size_t(last-first) * geno_record_bytes;
                                    if (fwrite(batch_geno + size_t(first) * geno_record_bytes, 1, bytes, fp31) != bytes)
                                        throw std::runtime_error("cannot write genotype output batch");
                                }
                                if (hapout == 1) {
                                    const size_t bytes = size_t(last-first) * hap_record_bytes;
                                    if (fwrite(batch_hap + size_t(first) * hap_record_bytes, 1, bytes, fp29) != bytes)
                                        throw std::runtime_error("cannot write haplotype output batch");
                                }
                            }
                            first = last;
                        }
                        }
#endif
                        } catch (...) {
                            output_failure.capture();
                        }
                    } // end single writer
                    if (output_failure.failed() || chunk_end == nped) break;
                    slot ^= slots - 1;
                    chunk_start = chunk_end + 1;
                } // end chunk loop
            } // end output team
            output_failure.rethrow("result output");
#if FINDHAP_ASYNC_DENSE_OUTPUT
            if (dense_writer) dense_writer->finish();
            if (chromosome_writer) chromosome_writer->finish();
#endif
#ifdef FINDHAP_LOW_MEMORY_PLUS
            if (output_cache_error) std::rethrow_exception(output_cache_error);
            genotype_cache->release_batch();
            legacy_printf(" Raw disk cache: %zu bytes, %llu read, %llu written\n", genotype_cache->bytes(),
                   (unsigned long long)genotype_cache->read_bytes(),
                   (unsigned long long)genotype_cache->write_bytes());
#endif

            legacy_printf(" %d genotype records rematched to output\n", n13a);
            if (n13a != n13) status_printf(" %d genotype records expected - MUST SORT BY ID\n", n13);
        } // end if step == steps

        // ---- Write haplotype list ----
        legacy_printf("\n");
        if (step == steps) legacy_printf("    hap   freq  haplotype\n");
        if (listout != 0 && step == 1) {
            const auto write_library_headers = [&](FILE* library, FILE* assignments) {
                fprintf(library, "%31s%3d %s\n", "", steps, "steps");
                fputs("  hap_freq  tot_freq    hap_# chro step seg   locus  lngth  haplotypes\n", library);
                fprintf(assignments, "%10s%3d %s\n", "", steps, "steps");
                fputs("  animal  pnt step segments haplotypes\n", assignments);
            };
            if (chromosome_processing) {
                for (const auto& result : chromosome_results)
                    if (result.library) write_library_headers(result.library, result.assignments);
            } else write_library_headers(fp25, fp26);
        }

        // Write hap.found
        FINDHAP_PHASE(library_output_trace, "library_output", step);
        FINDHAP_LIBRARY_CALIBRATE();
        FINDHAP_LIBRARY_ENABLE(true);
        FINDHAP_PHASE(library_found_trace, "library_found", step);
        if (chromosome_processing) {
            if (listout == 1 || (listout == -1 && step == steps)) {
#if FINDHAP_PARALLEL_ASSIGNMENT_OUTPUT
                const bool written = write_chromosome_assignments(chromosome_results,
                    startseg.get(), stopseg.get(), chrome1, chromes, keephap.get(), animkey.get(),
                    listeqn.get(), nped, maxped, step, threads);
                if (!written)
#endif
                for (int c = chrome1; c <= chromes; ++c) {
                    FILE* output = chromosome_results[c - chrome1].assignments;
                    if (!output) continue;
                    const int first = startseg[c - 1], segments = stopseg[c - 1] - first + 1;
                    const auto* assignments = keephap.get() + KHIDX(1, 1, first, maxped);
#if FINDHAP_PARALLEL_ASSIGNMENT_OUTPUT
                    const size_t assignment_stride = 2 * (25 + size_t(11) * segments);
                    if (threads > 1 && nped >= 256 && assignment_stride <= assignment_buffer_bytes) {
                        write_typed_assignments(output, assignments, animkey.get(), listeqn.get(),
                                                nped, maxped, segments, step, threads, assignment_stride);
                        continue;
                    }
#endif
                    for (int animal = 1; animal <= nped; ++animal) {
                        if (listeqn[animal] == 0) continue;
                        for (int parent = 1; parent <= 2; ++parent)
                            fast_write_hapfound_line(output, text_buffers, animkey[animal], parent, step,
                                segments, assignments, parent, animal, maxped, segments);
                    }
                }
            }
            #pragma omp parallel for schedule(static) num_threads(threads)
            for (int s = 1; s <= maxseg; ++s) {
                const auto* assignments = keephap.get() + KHIDX(1, 1, s, maxped);
                int* counts = hapfreq_arr.get() + (size_t(s) - 1) * maxhap;
                const auto* retained = step == steps
                    ? chromosome_retained.data() + size_t(chro[s - 1] - chrome1) * maxped : nullptr;
                for (int animal = 0; animal < nped; ++animal, assignments += 2) {
                    if (listeqn[animal + 1] == 0 && (!retained || !retained[animal])) continue;
                    if (assignments[0]) ++counts[assignments[0] - 1];
                    if (assignments[1]) ++counts[assignments[1] - 1];
                }
            }
        } else {
#if FINDHAP_PARALLEL_ASSIGNMENT_COUNTS
        if (listout == 1 || (listout == -1 && step == steps)) {
#if FINDHAP_PARALLEL_ASSIGNMENT_OUTPUT
            const size_t assignment_stride = 2 * (25 + size_t(11) * maxseg);
            if (threads > 1 && nped >= 256 && assignment_stride <= assignment_buffer_bytes) {
                write_typed_assignments<FINDHAP_GROUP_HAPFOUND>(fp26, keephap.get(), animkey.get(),
                    listeqn.get(), nped, maxped, maxseg, step, threads, assignment_stride);
            } else
#endif
            {
                for (int animal = 1; animal <= nped; ++animal) {
                    if (listeqn[animal] == 0) continue;
                    for (int parent = 1; parent <= 2; ++parent)
                        fast_write_hapfound_line(fp26, text_buffers, animkey[animal], parent, step,
                            maxseg, keephap.get(), parent, animal, maxped, maxseg);
                }
            }
        }
        // Each segment owns a contiguous assignment slice and independent
        // integer counts. Existing prior counts and the library order survive.
        {
            FINDHAP_PHASE(count_trace, "library_assignment_counts", step);
            #pragma omp parallel for schedule(static) num_threads(threads)
            for (int segment = 1; segment <= maxseg; ++segment) {
                const HaplotypeId* assignments = keephap.get() + KHIDX(1, 1, segment, maxped);
                int* counts = hapfreq_arr.get() + (size_t(segment) - 1) * maxhap;
                for (int animal = 0; animal < nped; ++animal, assignments += 2) {
                    if (!count_assignment[animal]) continue;
                    const auto first = assignments[0], second = assignments[1];
                    if (first) ++counts[first - 1];
                    if (second) ++counts[second - 1];
                }
            }
        }
#else
        for (i = 1; i <= nped; i++) {
            if (listeqn[i] > 0) {
                for (pnt = 1; pnt <= 2; pnt++) {
                    if (listout == 1 || (listout == -1 && step == steps))
                        fast_write_hapfound_line(fp26, text_buffers, animkey[i], pnt, step,
                            maxseg, keephap.get(), pnt, i, maxped, maxseg);
                    FINDHAP_LIBRARY_SAMPLE(count_sample, 2);
                    for (seg = 1; seg <= maxseg; seg++) {
                        if (keephap[KHIDX(pnt, i, seg, maxped)] > 0)
                            hapfreq_arr[IDX2(keephap[KHIDX(pnt, i, seg, maxped)], seg, maxhap)]++;
                    }
                }
            }
        }
#endif
        }

        FINDHAP_END(library_found_trace);
        FINDHAP_PHASE(library_list_trace, "library_list", step);
        n25 = 0;
        for (seg = 1; seg <= maxseg; seg++) {
            totfreq = 0.0;
            haptot = 0;
#if FINDHAP_LIBRARY_METADATA
            const int metadata_published = nhap_arr[(seg - 1) * FINDHAP_NHAP_STRIDE];
#endif
#if FINDHAP_LIBRARY_ACTIVE_BOUND
            // IDs beyond the published prefix have zero frequency and retain
            // identity order positions. Keep all arithmetic for published IDs,
            // including the original REAL(4)-then-REAL(8) accumulation order.
            const int published=nhap_arr[(seg-1)*FINDHAP_NHAP_STRIDE];
#if FINDHAP_LIBRARY_METADATA
            // Preserve the legacy full-capacity empty-library fallback without
            // reading an unpublished zero order slot as a haplotype ID.
            if(!published)for(int position=1;position<=maxhap;++position)
                order_arr[ORDIDX(position,seg,maxhap+1)]=position;
#endif
            const int library_limit=published>0 ? published : maxhap;
            for (i = 1; i <= library_limit; i++) {
#else
            for (i = 1; i <= maxhap; i++) {
#endif
                haptot += std::max(hapfreq_arr[IDX2(i, seg, maxhap)], 0);
            }
            n = 0;
#if FINDHAP_LIBRARY_ACTIVE_BOUND
            for (i = 1; i <= library_limit; i++) {
#else
            for (i = 1; i <= maxhap; i++) {
#endif
#if FINDHAP_LIBRARY_METADATA
                const int ordered_haplotype = findhap_haplotype_metadata::output_order_id(
                    order_arr[ORDIDX(i, seg, maxhap + 1)], i, metadata_published);
#else
                const int ordered_haplotype = order_arr[ORDIDX(i, seg, maxhap + 1)];
#endif
                // Fortran rounds each REAL(4) fraction before adding to REAL(8) totfreq.
                totfreq += static_cast<double>(1.0f * hapfreq_arr[IDX2(ordered_haplotype, seg, maxhap)] / haptot);
                if (hapfreq_arr[IDX2(ordered_haplotype, seg, maxhap)] > 0) {
                    n++;
                    if (listout == 1 || (listout == -1 && step == steps)) {
                        const int c = chro[seg - 1];
                        FILE* output = chromosome_processing ? chromosome_results[c - chrome1].library : fp25;
                        if (output) fast_write_haplist_line(output, text_buffers.list(),
                            100.0f * hapfreq_arr[IDX2(ordered_haplotype, seg, maxhap)] / haptot,
                            100.0 * totfreq,
                            ordered_haplotype, c, step,
                            chromosome_processing ? seg - startseg[c - 1] + 1 : seg,
                            chromosome_processing ? startloc[seg] - loc1[c - 1] + 1 : startloc[seg], len_arr[seg],
                            &haplist_arr[IDX3(1, ordered_haplotype, seg, maxlen, maxhap)],
                            len_arr[seg]);
                    }
                    n25++;
                    if (n <= 20 && seg == 1 && step == steps) {
                        legacy_printf("%7d%7d  ", i, hapfreq_arr[IDX2(ordered_haplotype, seg, maxhap)]);
                        for (l = 1; l <= std::min(len_arr[seg], 100); l++) {
                            legacy_printf("%d", haplist_arr[IDX3(l, ordered_haplotype, seg, maxlen, maxhap)]);
                        }
                        legacy_printf("\n");
                    }
                    maxnhap = std::max(ordered_haplotype, maxnhap);
                }
            }
            nhap_arr[(seg - 1) * FINDHAP_NHAP_STRIDE] = n;
        }
        FINDHAP_END(library_list_trace);
        FINDHAP_LIBRARY_ENABLE(false);

        // End of step
        if (step < steps && listout != 0) {
            const auto write_next_step = [&](FILE* library, FILE* assignments) {
                fprintf(library, "%10.5f%10.5f%10d%3d%3d%7d%8d%6d\n", 0.0, 0.0, 0, 0, step + 1, 0, 0, 0);
                fprintf(assignments, "%10d%2d%3d%6d\n", 0, 0, step + 1, 0);
            };
            if (chromosome_processing) {
                for (const auto& result : chromosome_results)
                    if (result.library) write_next_step(result.library, result.assignments);
            } else write_next_step(fp25, fp26);
        }
        report_events.phase("Result output", "completed", step);
        report_events.emit("step_end", step, 0, maxseg, nhap_arr.get(),
            FINDHAP_NHAP_STRIDE, threads, n13, nped, loci
            , -1, -1, chro.get(), maxhap
            );
    } // end step loop
    FINDHAP_LIBRARY_SAVE();
    report_events.phase("Final output and file close", "started");

    // -------------------------------------------------------- Genotype frequency
    legacy_printf("\n");
    legacy_printf(" Frequency of codes in genotypes.filled\n");
    int64_t reported_marker_count = 0;
    if (chromosome_processing)
        for (int c = chrome1; c <= chromes; ++c)
            reported_marker_count += int64_t(chromosome_results[c - chrome1].reported) *
                                     (locN[c - 1] - loc1[c - 1] + 1);
    for (i = 0; i <= 9; i++) {
        if (genfreq[i] > 0)
            legacy_printf("%15ld genotypes with code %2d%12.5f%%\n",
                   (long)genfreq[i], i, chromosome_processing
                       ? 100.0 * genfreq[i] / reported_marker_count : 100.0 * genfreq[i] / n31 / loci);
    }
    if (genout == 5) legacy_printf(" Codes 3 and 4 written as code 5 with option genout = 5\n");
    if (genout == 2) legacy_printf(" Codes 3, 4, 5 written as 0, 1, 2 by best call with option genout = 5\n");
    legacy_printf("\n");
    legacy_printf(" %d maximum haplotype number within segment\n", maxnhap);
    legacy_printf("\n");
    legacy_printf("          Starting marker within each segment\n");
    segs = 1 + (locN[0] - loc1[0]) / maxlen;
    legacy_printf("   ");
    for (i = 1; i <= segs; i++) legacy_printf("%7d", i);
    legacy_printf("\n");
    legacy_printf(" Chrome\n");
    seg = 1;
    for (chrome = chrome1; chrome <= chromes; chrome++) {
        segs = 1 + (locN[chrome - 1] - loc1[chrome - 1]) / maxlen;
        legacy_printf("%3d", chrome);
        for (i = seg; i <= seg + segs - 1; i++) legacy_printf("%7d", startloc[i]);
        legacy_printf("\n");
        seg += segs;
    }
    legacy_printf("\n");
    legacy_printf("          Number of haplotypes found within each segment\n");
    segs = 1 + (locN[0] - loc1[0]) / maxlen;
    legacy_printf("   ");
    for (i = 1; i <= segs; i++) legacy_printf("%7d", i);
    legacy_printf("\n");
    legacy_printf(" Chrome\n");
    seg = 1;
    for (chrome = chrome1; chrome <= chromes; chrome++) {
        segs = 1 + (locN[chrome - 1] - loc1[chrome - 1]) / maxlen;
        legacy_printf("%3d", chrome);
        for (i = seg; i <= seg + segs - 1; i++) legacy_printf("%7d", nhap_arr[(i - 1) * FINDHAP_NHAP_STRIDE]);
        legacy_printf("\n");
        seg += segs;
    }
    legacy_printf("\n");
    if (chromosome_processing) {
        for (int c = chrome1; c <= chromes; ++c) {
            const auto& result = chromosome_results[c - chrome1];
            if (result.quality)
                status_printf(" Chromosome %d: %d animals reported, including %d inferred ancestors; "
                              "%d genotyped animals excluded\n", c, result.reported, result.ancestors, result.excluded);
        }
    } else {
        if (genout > 0) legacy_printf(" %d records written to genotypes.filled\n", n31);
        if (damout == 2) nadd -= nmale;
        if (damout >= 1) legacy_printf(" %d female ancestor genotypes imputed from progeny\n", nadd);
        if (damout == 2) legacy_printf(" %d male ancestor genotypes imputed from progeny\n", nmale);
        if (nmale > 0) legacy_printf(" %d male ancestor genotypes that could be imputed\n", nmale);
        legacy_printf(" %d genotyped animals with poor imputation not distributed\n", ndrop);
        if (turbohap_reporting::modern_stdout())
            turbohap_reporting::stdout_clock().print("Results: %d genotype records written; %d female and %d male ancestor genotypes; %d poor-imputation animals excluded\n",
                                                    n31, nadd, nmale, ndrop);
    }

    // -------------------------------------------------------- Allele frequency output
    const auto write_allele_frequencies = [&](FILE* output, int first, int last, int reported, int ancestors) {
        if (ancestors == 0) ancestors = 1;
        fputs("           allele_frequency_estimates    missing_genotype_frequency\n", output);
        fputs("    locus   simple     base  current    input   output  imputed      all\n", output);
        for (int locus = first; locus <= last; ++locus) {
            if (cfreq[locus - 1] > 0) {
                curfreq[locus - 1] = 0.5f * sumcurr[locus - 1] / cfreq[locus - 1];
            } else {
                curfreq[locus - 1] = 0.5f;
            }
            if (bfreq[locus - 1] >= 10) {
                basefreq[locus - 1] = 0.5f * sumbase[locus - 1] / bfreq[locus - 1];
            } else {
                basefreq[locus - 1] = curfreq[locus - 1];
            }
            freq[locus - 1] = std::min(freq[locus - 1], 0.99999f);
            freq[locus - 1] = std::max(freq[locus - 1], 0.00001f);
            curfreq[locus - 1] = std::min(curfreq[locus - 1], 0.99999f);
            curfreq[locus - 1] = std::max(curfreq[locus - 1], 0.00001f);
            basefreq[locus - 1] = std::min(basefreq[locus - 1], 0.99999f);
            basefreq[locus - 1] = std::max(basefreq[locus - 1], 0.00001f);
            const float input_missing = n13 > 0 ? 1.0f - 1.0f * ofreq[locus - 1] / n13 : 0.0f;
            fprintf(output, "%9d%9.6f%9.6f%9.6f%9.6f%9.6f%9.6f%9.6f\n",
                locus - first + 1, freq[locus - 1], basefreq[locus - 1], curfreq[locus - 1],
                input_missing,
                1.0f * (missall[locus - 1] - missadd[locus - 1]) / std::max(reported - ancestors, 1),
                1.0f * missadd[locus - 1] / ancestors,
                1.0f * missall[locus - 1] / std::max(reported, 1));
        }
    };
    if (chromosome_processing) {
        for (int c = chrome1; c <= chromes; ++c) {
            const auto& result = chromosome_results[c - chrome1];
            if (result.allele_frequency)
                write_allele_frequencies(result.allele_frequency, loc1[c - 1], locN[c - 1],
                    result.reported, result.ancestors - (damout == 2 ? result.male_ancestors : 0));
        }
    } else {
        write_allele_frequencies(fp30, 1, loci, n31, nadd);
    }
    legacy_printf("\n");
    // Close files
    pedigree_file.reset();
    genotype_file.reset();
    map_file.reset();
    hap_list_priors.reset();
    hap_found_priors.reset();
    FINDHAP_PHASE(close_trace, "output_close");
    outputs.finish();
    if (result_directory) result_directory->publish();
    FINDHAP_END(close_trace);
    if (findhap::defer_ancestor_quality_filter && turbohap_reporting::modern_stdout())
        status_printf("Ancestor quality filtering deferred. Provisional ancestor counts written to %s\n",
                      ancestor_filter_filename);
    if (findhap::defer_genotyped_quality_filter)
        status_printf("Genotyped quality filtering deferred\n");
    if (findhap::whole_genome_high_density)
        status_printf("Whole-genome high-density reference classification applied\n");
    report_events.phase("Final output and file close", "completed");
    status_printf(" Program findhap completed normally\n");
    report_events.emit("run_complete", steps, iters, maxseg, nhap_arr.get(),
        FINDHAP_NHAP_STRIDE, threads, n31, nped, loci
        , -1, -1, chro.get(), maxhap
        );

    return 0;
}

int FINDHAP_ENTRY_POINT(int argc, char* argv[]) {
    if (turbohap_reporting::modern_stdout())
#ifdef FINDHAP_LOW_MEMORY
        turbohap_reporting::stdout_clock().print("TurboHap LM started\n");
#else
        turbohap_reporting::stdout_clock().print("TurboHap standard started\n");
#endif
    legacy_printf(" Output from findhap.cpp (C++ port)\n");
    legacy_printf("\n");
    if (!std::filesystem::exists("findhap.options")) {
        status_printf(" file findhap.options is missing\n"); exit(8);
    }
    if (!std::filesystem::exists("pedigree.file")) {
        status_printf(" file pedigree.file is missing\n"); exit(10);
    }
    if (!std::filesystem::exists("genotypes.txt")) {
        status_printf(" file genotypes.txt is missing\n"); exit(13);
    }
    if (!std::filesystem::exists("chromosome.data")) {
        status_printf(" file chromosome.data is missing\n"); exit(14);
    }
    findhap::Options options;
    try {
        std::ifstream options_file("findhap.options");
        options = findhap::read_options(options_file);
    } catch (const std::exception& error) { return report_input_error(error.what()); }

    // Select once from the user's unchanged capacity limit. No width branch
    // is needed in the hot loops, and larger libraries retain full-width IDs.
    try {
        if (options.controls[5] <= std::numeric_limits<uint16_t>::max())
            return run_findhap<uint16_t>(argc, argv, options);
        return run_findhap<int>(argc, argv, options);
    } catch (const HaplotypeCapacityError& error) {
        legacy_printf("\n");
        status_printf(" More haplotypes than maxhap = %d in segment %d\n", error.limit, error.segment);
        if (turbohap_reporting::modern_stdout())
            status_printf(" TurboHap capacity: segment %d requires at least %lld slots; maxhap %d\n",
                          error.segment, static_cast<long long>(error.limit) + 1, error.limit);
        status_printf(" Increase maxhap in findhap.options\n");
        return 100;
    } catch (const std::exception& error) {
        return report_input_error(error.what());
    } catch (...) {
        return report_input_error("unknown TurboHap failure");
    }
}

#ifdef FINDHAP_ENGINE_NAMESPACE
} // namespace FINDHAP_ENGINE_NAMESPACE
#endif
