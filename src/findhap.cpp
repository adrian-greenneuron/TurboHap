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
#include <cmath>
#include <algorithm>
#include <limits>
#include <memory>
#include <filesystem>
#include <string>
#include <sstream>
#include <vector>
#include <fstream>
#ifdef FINDHAP_LOW_MEMORY
#include <atomic>
#include <exception>
#endif
#include "options_reader.hpp"
#include "thread_request.hpp"
#include "genotype_workspace.hpp"
#include "genotype_cache.hpp"
#ifdef FINDHAP_LOW_MEMORY_PLUS
#include "external_genotype_cache.hpp"
#include "bounded_genotype_workspace.hpp"
using ActiveGenotypeCache = ExternalGenotypeCache;
#else
using ActiveGenotypeCache = GenotypeCache;
#endif
#include "candidate_index.hpp"
#ifndef FINDHAP_POP_SKIP_UNUSED
#define FINDHAP_POP_SKIP_UNUSED 0
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
#include "pedigree_conflict.hpp"
#include "pedigree_rematch.hpp"
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
// PMIDX(i, seg, mped)      — base byte offset for patmat entry (4 chars each)
// ============================================================================
#define KHIDX(pnt, i, seg, mped) \
    (((size_t)(seg)-1)*(size_t)(mped)*2 + ((size_t)(i)-1)*2 + ((size_t)(pnt)-1))
#define PMIDX(i, seg, mped) \
    ((((size_t)(seg)-1)*(size_t)(mped) + ((size_t)(i)-1))*4)

// ============================================================================
// Buffered input and output
// ============================================================================

static char* read_buf = nullptr;
static char* haplist_buf_io = nullptr;
static char* hapfound_buf_io = nullptr;
static size_t hapfound_buf_capacity = 0;

[[noreturn]] static void input_error(const char* message) {
    fprintf(stderr, "Input error: %s\n", message);
    exit(2);
}

// Fortran OPEN without STATUS='REPLACE' preserves an existing file if no
// records are written. Otherwise CLOSE truncates it after the last record.
static std::vector<std::pair<FILE*, std::string>> output_files;

static FILE* open_output(const char* path) {
    FILE* file = fopen(path, "r+");
    if (!file && errno == ENOENT) file = fopen(path, "w+");
    if (!file) input_error("cannot open output file");
    output_files.emplace_back(file, path);
    return file;
}

static void close_outputs() {
    for (auto& output : output_files) {
        FILE* file = output.first;
        long position = ftell(file);
        // A prior short/failed write can leave the error indicator set even
        // when a later flush succeeds. Do not report such a file as complete.
        if (ferror(file)) input_error("cannot write output file");
        if (fflush(file) != 0 || position < 0) input_error("cannot flush output file");
        if (position > 0) {
            std::error_code error;
            std::filesystem::resize_file(output.second, static_cast<uintmax_t>(position), error);
            if (error) input_error("cannot truncate output file");
        }
        if (fclose(file) != 0) input_error("cannot close output file");
    }
    output_files.clear();
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

static void fast_io_init(int max_loci) {
    delete[] read_buf;
    delete[] haplist_buf_io;
    delete[] hapfound_buf_io;
    read_buf = new char[(size_t)2 * max_loci + 33];
    haplist_buf_io = new char[(size_t)(max_loci + 61)];
    hapfound_buf_capacity = (size_t)max_loci + 31;
    hapfound_buf_io = new char[hapfound_buf_capacity];
}

// Read one genotype line: 3i10,1x,<loc>i1
static int fast_read_genoline(FILE* fp, int& idnum, int& chipn, int& loc,
                              int8_t* genovec, int maxloci) {
    if (!fgets(read_buf, 2 * maxloci + 32, fp)) return -1;

    idnum = 0;
    for (int i = 0; i < 10; i++) {
        if (read_buf[i] >= '0' && read_buf[i] <= '9') {
            idnum = idnum * 10 + (read_buf[i] - '0');
        }
    }
    chipn = 0;
    for (int i = 10; i < 20; i++) {
        if (read_buf[i] >= '0' && read_buf[i] <= '9') {
            chipn = chipn * 10 + (read_buf[i] - '0');
        }
    }
    loc = 0;
    for (int i = 20; i < 30; i++) {
        if (read_buf[i] >= '0' && read_buf[i] <= '9') {
            loc = loc * 10 + (read_buf[i] - '0');
        }
    }

    // Position 30 is space, digits start at position 31 (0-based)
    for (int l = 0; l < loc; l++) {
        // Fortran I1 input treats an all-blank field as zero. Do not skip
        // it: it occupies a locus, including when followed by extra digits.
        const char field = read_buf[31 + l];
        genovec[l] = field == ' ' ? 0 : static_cast<int8_t>(field - '0');
    }
    return 0;
}

// Write one hap.list line: 2f10.5,i10,2i3,i7,i8,i6,1x,<nhapdata>i1
static void fast_write_haplist_line(FILE* fp, float hap_pct, double tot_pct,
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
static void fast_write_hapfound_line(FILE* fp, int id, int pnt_val, int istep,
    int nsegs, const HaplotypeId* keephap_arr, int pnt_idx, int anml_idx,
    int maxanml, int maxseg_dim) {
    FINDHAP_LIBRARY_SAMPLE(format_sample, 0);
    // One separator plus up to ten decimal digits per segment, and the header.
    size_t required = 25 + (size_t)11 * nsegs;
    if (required > hapfound_buf_capacity) {
        delete[] hapfound_buf_io;
        hapfound_buf_capacity = required;
        hapfound_buf_io = new char[required];
    }
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
    FINDHAP_LIBRARY_SAMPLE_END(format_sample);
    FINDHAP_LIBRARY_SAMPLE(submit_sample, 1);
    fwrite(hapfound_buf_io, 1, pos, fp);
}

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
                    printf(" hash table size %d   is too small, increase it\n", isize);
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
    printf(" Function NHASH dropped out of the loop\n");
    exit(41);
    return 0;
}



// ============================================================================
// listhap — Find haplotype from list (port of subroutine listhap)
// ============================================================================

template <int SWITCH_VAL>
static bool listhap(int8_t* __restrict__ storeseg, int8_t* __restrict__ haplist_arr, int* __restrict__ hapfreq_arr,
    int* __restrict__ order_arr, int* __restrict__ suggest, int len, int maxlen, int* __restrict__ nhap_arr,
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
    const size_t seg_base = ((size_t)seg - 1) * maxhap * maxlen;
    // For hapfreq(hap, seg) → (seg-1)*maxhap + (hap-1)
    const size_t freq_base = ((size_t)seg - 1) * maxhap;
    // For order(i, seg) → (seg-1)*(maxhap+1) + i
    const size_t ord_base = ((size_t)seg - 1) * (maxhap + 1);

    // Set missing SNP to 1
    for (int l = 0; l < len; l++) {
        segment[l] = storeseg[l];
        if (segment[l] == 5) segment[l] = 1;
        if (segment[l] != 1) check++;
    }
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
            stopi = nhap_arr[seg - 1];
        }

        minfreq = 99999999;
#if FINDHAP_USE_ORDERED
        ActiveOrderedLookup* ordered = nullptr;
        if (candidate_index && j > sugnum &&
            (SWITCH_VAL == 1 || (errate * check >= 0 && errate * check < 1)) &&
            __builtin_popcountll(signature.zero | signature.two) >= 4) {
            ordered = candidate_index->ordered(order_arr + ord_base, hapfreq_arr + freq_base,
                                               nhap_arr[seg - 1], SWITCH_VAL == 1);
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

            const int freq_idx = freq_base + (ii - 1);
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
            // Pre-compute pointer to haplist(:, ii, seg)
            const int8_t* __restrict__ hap_ptr = &haplist_arr[seg_base + (ii - 1) * maxlen];

            // Keep minfreq/minhap bookkeeping even for filtered candidates.
            // This only rejects a proven conflict excess; survivors still use
            // the unchanged full scan and position-dependent acceptance rule.
            if (candidate_index) {
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
                int8_t* __restrict__ hap_wr = &haplist_arr[seg_base + (ii - 1) * maxlen];
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
            for (int l = 0; l < len; l++) {
                segment[l] = 2 * segment[l] - hap_ptr[l];
                if (storeseg[l] == 5) segment[l] = 1;
                if (segment[l] != 1) check++;
            }
            if (candidate_index && fnd < 2) signature = candidate_index->encode(segment);
            // Switch order if more frequent
            if constexpr (SWITCH_VAL == 1) {
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
            nhap_arr[seg - 1]++;
            found[fnd - 1] = nhap_arr[seg - 1];
            minhap = nhap_arr[seg - 1];
            if (nhap_arr[seg - 1] > maxhap) {
                printf("\n");
                printf(" More haplotypes than maxhap = %d in segment %d\n", maxhap, seg);
                printf(" Increase maxhap in findhap.options\n");
                exit(100);
            }
            hapfreq_arr[freq_base + nhap_arr[seg - 1] - 1] = 1;
            // Store new haplotype
            int8_t* __restrict__ new_hap = &haplist_arr[seg_base + (nhap_arr[seg - 1] - 1) * maxlen];
            for (int l = 0; l < len; l++) {
                new_hap[l] = segment[l];
            }
            for (int l = len; l < maxlen; l++) {
                new_hap[l] = 0;
            }
            if (candidate_index) candidate_index->refresh(nhap_arr[seg - 1], new_hap);
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
        const int8_t* __restrict__ hap2 = &haplist_arr[seg_base + (found[1] - 1) * maxlen];
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
        const int8_t* __restrict__ hap1 = &haplist_arr[seg_base + (found[0] - 1) * maxlen];
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
        // Fill missing values in 1st haplotype
        if (miss > 0) {
            int8_t* __restrict__ hap1_wr = &haplist_arr[seg_base + (found[0] - 1) * maxlen];
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
    // ---- Allocatable arrays (will be allocated after sizes are known) ----
    std::unique_ptr<float[]> freq, basefreq, curfreq;
    std::unique_ptr<float[]> linkREL, patREL;
    std::unique_ptr<int8_t[]> genotype, genovec, genovec2;
    std::unique_ptr<int8_t[]> haplotype;
    std::unique_ptr<int[]> loc1, locN, prog, gprog, pntloc;
    std::unique_ptr<int[]> locate1, locateN, phaps;
    std::unique_ptr<int[]> ped, chiplist_arr;
    std::unique_ptr<int[]> loclist, chiplen, called;
    std::unique_ptr<int[]> animkey, nhap_arr, missall, keyvec;
    std::unique_ptr<int[]> listeqn, ids, eqns, missadd;
    std::unique_ptr<int[]> startloc, prevstartloc;
    std::unique_ptr<int[]> len_arr, prevlen, chro, chip_arr, high;
    std::unique_ptr<int[]> startseg, stopseg, proghaps;
    std::unique_ptr<int[]> nfreq, ofreq, sumfreq_arr, birth;
    std::unique_ptr<int[]> bfreq, cfreq, sumbase, sumcurr;
    std::unique_ptr<char[]> sexv;
    // patmat: 2-char strings stored as char[2] per entry; 3D(2, maxped, maxseg)
    std::unique_ptr<char[]> patmat;  // each entry is 2 chars
    // These arrays contain only zero or segment-local IDs bounded by maxhap.
    // Animal indices, counts, frequency arithmetic and search remain int.
    std::unique_ptr<HaplotypeId[]> keephap, prevkeephap, fromprog;
    std::unique_ptr<int8_t[]> haplist_arr, prevhaplist;
    std::vector<size_t> prevhap_offsets;
    std::unique_ptr<int8_t[]> hapseg_arr;
    std::unique_ptr<int[]> hapfreq_arr, order_arr;
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
    char newpatmat[2][3];
    const char* matchcode[7] = {" 1", " 2", "X2", "X1", "-1", "-2", " H"};
    char sex;
    char markername[51];
    int64_t location, totlngth, maxlngth;
    int mosthap[4], mosthaps[4], search, usable;
    int maxped, loci, nloci, maxped2, mismatch, haps, maxhap;
    int chrome, chrome1, chromes, idnum;
    int prevseg, prevsegs;
    int n13, n13a, hold, cross_arr[2], ncross, pnthap[2], where_arr[2][2];
    int maxl, maxloci, missing, anmlmiss, high1, nhigh, nmed, pedx[2];
    int loc, locus, nped, iter, iter1, iters, suggest[16], agree;
    int pstep, psegs, haplen, haptot, totlen;
    int64_t nped1;
    int nn, pnt, pnt1, pnt2, pnts, grdpnt, bday, nadd, nmale, ndrop;
    int XChrom, match_arr[2][2], matches;
    int totprog, maxprog, maxpnt, hap[2];
    float simplfreq, corr2, correct, lenratio, errate;
    float haptotal;
    double totfreq, hapfrq;
    int notfound;
    bool exist, exist15, exist16, exist22;
    int gencode[6][6];
    int64_t genfreq[10];
    int threads, nthr;
    FINDHAP_PHASE(setup_trace, "setup");

    int* fields[] = {&iters, &XChrom, &maxlen, &minlen, &steps, &maxhap,
                     &hapout, &genout, &damout, &listout};
    for (size_t field = 0; field < options.controls.size(); ++field)
        *fields[field] = options.controls[field];
    errate = options.errate;
    if (maxlen < 1 || minlen < 1 || maxhap < 1 || steps < 1)
        input_error("segment lengths, maxhap and steps must be positive");
    printf("    iters,XChrom,maxlen,minlen,steps,maxhap,hapout,genout,damout,listout\n");
    printf("%7d%7d%7d%7d%7d%7d%7d%7d%7d%7d\n",
           iters, XChrom, maxlen, minlen, steps, maxhap, hapout, genout, damout, listout);
    printf("\n");
    char line[256];
    printf("%9.6f\n", errate);
    printf("    errate\n");
    printf("\n");

    // ------------------------------------------------------ Input data files
    FILE* fp10 = fopen("pedigree.file", "r");
    FILE* fp13 = fopen("genotypes.txt", "r");
    FILE* fp14 = fopen("chromosome.data", "r");

    // Haplotype priors
    exist15 = std::filesystem::exists("hap.listpriors");
    exist16 = std::filesystem::exists("hap.foundpriors");
    exist22 = std::filesystem::exists("hap.Xseg");
    FILE* fp15 = exist15 ? fopen("hap.listpriors", "r") : nullptr;
    FILE* fp16 = exist16 ? fopen("hap.foundpriors", "r") : nullptr;

    if (exist15 && (!fp15 || !fp16 || !exist22))
        input_error("hap.listpriors requires readable hap.foundpriors and hap.Xseg");
    int prior_high1 = 0;
    if (exist15) {
        // Read the prior count before opening hap.Xseg as an output.
        FILE* previous = fopen("hap.Xseg", "r");
        if (!previous || fscanf(previous, "%d", &prior_high1) != 1)
            input_error("invalid prior hap.Xseg");
        fclose(previous);
    }

    // ----------------------------------------------------- Output data files
    FILE* fp22 = open_output("hap.Xseg");
    FILE* fp23 = open_output("do_not_distrib");
    FILE* fp24 = open_output("hap.filled");
    FILE* fp25 = (listout != 0) ? open_output("hap.list") : nullptr;
    FILE* fp26 = (listout != 0) ? open_output("hap.found") : nullptr;
    FILE* fp27 = open_output("cross.overs");
    FILE* fp28 = open_output("hap.inherit");
    FILE* fp30 = open_output("allele.frequency");
    FILE* fp29 = (hapout > 0) ? open_output("haplotypes.txt") : nullptr;
    FILE* fp31 = (genout > 0) ? open_output("genotypes.filled") : nullptr;

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

    // match codes header lines for hap.inherit
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

    // Default number of threads
    // Resolve once in the launcher before changing affinity. Standalone
    // engines retain the original OpenMP/argument semantics.
    try {
        const auto request = findhap::engine_thread_request(argc, argv);
        nthr = request.openmp_limit;
        threads = request.effective;
    } catch (const std::exception& error) { input_error(error.what()); }
    if (argc > 1) printf("\n");

    printf(" Maximum number of processors set to    %d\n", threads);
    printf(" Or set threads by command line argument: findhap 8\n");
    printf("\n");
    printf(" Number of processors available =       %d\n", nthr);
    printf(" Can be controlled using:  export OMP_NUM_THREADS=8\n");
    printf("\n");

    // ------------------------------------------- Get loci, chromes from chromosome.data
    auto skip_line = [](FILE* f) {
        int ch;
        while ((ch = fgetc(f)) != '\n' && ch != EOF);
    };

    loci = 0;
    chromes = 0;
    chrome1 = 99999;
    skip_line(fp14); // skip header
    while (fscanf(fp14, "%50s %d %d %d %ld %d", markername, &chrome, &nloci, &loc, &location, &chips) == 6) {
        loci++;
        chrome1 = std::min(chrome, chrome1);
        chromes = std::max(chrome, chromes);
        // consume rest of line
        skip_line(fp14);
    }
    printf(" %d loci on %d chromosomes read from chromosome.data\n", loci, chromes);
    rewind(fp14);
    if (XChrom == 0) XChrom = chromes + 1;
    if (XChrom < chromes) {
        printf(" %d X must be last chromosome %d\n", XChrom, chromes);
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
    memset(chiplist_arr.get(), 0, loci * (chips + 2) * sizeof(int));
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
    skip_line(fp14); // skip header
    while (true) {
        int nread = fscanf(fp14, "%50s %d %d %d %ld %d", markername, &chrome, &nloci, &loc, &location, &chips);
        if (nread < 6) break;
        loci++;
        for (chipn = 1; chipn <= chips; chipn++) {
            int val;
            fscanf(fp14, "%d", &val);
            loclist[chipn - 1] = val;
        }
        skip_line(fp14); // consume rest of line
        for (chipn = 1; chipn <= chips; chipn++) {
            if (loclist[chipn - 1] > 0) {
                // chiplist(loclist(chipn), chipn) = loci
                chiplist_arr[CHIPIDX(loclist[chipn - 1], chipn, total_loci)] = loci;
                chiplen[chipn - 1]++;
                if (chiplen[chipn - 1] != loclist[chipn - 1]) {
                    printf(" %d %d marker numbers disagree in chromosome.data, chip %d\n",
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
    printf("\n");
    printf(" %d missing genotype code\n", missing);

    // ----------------------------------------- Get maxped from pedigree.file
    maxped = 0;
    rewind(fp10);
    while (fgets(line, 256, fp10)) maxped++;
    rewind(fp10);
    printf("\n");
    printf(" %d animals in pedigree.file (maxped)\n", maxped);

    ngen = 0;
    size_t genotype_entries = 0;
    size_t genotype_cache_bytes = 0;
    fast_io_init(maxl);
    {
        // Count genotype records using fast_read_genoline (handles long lines correctly)
        int tmp_id, tmp_chipn, tmp_loc;
        auto tmp_geno = std::make_unique<int8_t[]>(maxl);
        while (fast_read_genoline(fp13, tmp_id, tmp_chipn, tmp_loc, tmp_geno.get(), maxl) == 0) {
            ngen++;
            if ((size_t)tmp_loc > std::numeric_limits<size_t>::max() - genotype_entries)
                input_error("genotype cache is too large");
            genotype_entries += (size_t)tmp_loc;
            const size_t bytes = GenotypeCache::encoded_bytes(tmp_geno.get(), tmp_loc);
            if (bytes > std::numeric_limits<size_t>::max() - genotype_cache_bytes)
                input_error("genotype cache is too large");
            genotype_cache_bytes += bytes;
        }
    }
    printf(" %d animals in genotypes.txt (ngen)\n", ngen);
    rewind(fp13);
    // fast_io_init already called before count

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
    genovec2.reset(new int8_t[(size_t)(loci)]);
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
    high.reset(new int[(size_t)(maxped)]);
    chip_arr.reset(new int[(size_t)(maxped)]);

    memset(ofreq.get(), 0, loci * sizeof(int));
    memset(gprog.get(), 0, (maxped + 1) * sizeof(int));

    // ----------------------------------------------------- Read pedigree data
    memset(eqns.get(), 0, (maxped2 + 1) * sizeof(int));
    memset(listeqn.get(), 0, (maxped2 + 1) * sizeof(int));
    nhash(0, -1, ids.get(), maxped2, maxn);
    animkey[0] = 0;  // animkey(0) = 0
    nped = 0;
    char ped_line[256];
    while (fgets(ped_line, 256, fp10)) {
        if (sscanf(ped_line, " %c %d %d %d %d", &sex, &idnum, &pnt1, &pnt2, &bday) != 5) continue;
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
            printf(" Duplicate IDs in pedigree.file !\n");
            exit(1);
        }
        eqns[nn] = nped;
    }
    printf("\n");

    // Current animals = last 5 years
    curryr = today - 50000;
    if (nbase < 10) baseyr = today;
    printf(" latest birthday in pedigree.file (today)     =  %d\n", today);
    printf(" current frequency includes animals born after   %d\n", curryr);
    printf("    base frequency includes animals born before  %d\n", baseyr);
    printf("\n");
    printf("      animal       sire         dam\n");

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
        if (i <= 10) printf(" %d %d %d\n", animkey[i], animkey[ped[IDX2(1, i, 2)]], animkey[ped[IDX2(2, i, 2)]]);
    }
    memset(high.get(), 0, maxped * sizeof(int));
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
        printf(" %d animals with data in hap.foundpriors\n", nprior / 2);
        rewind(fp16);
        printf("  %d high-density animals in prior data\n", high1);
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
    while (fast_read_genoline(fp13, idnum, chipn, loc, genovec.get(), maxl) == 0) {
        if (cache_ngen >= ngen || (size_t)loc > genotype_entries - cache_next)
            input_error("genotypes.txt changed while loading the cache");
        cache_ngen++;
        cache_id[cache_ngen - 1] = idnum;
        cache_chipn[cache_ngen - 1] = chipn;
        cache_loc[cache_ngen - 1] = loc;
        genotype_cache->append(genovec.get(), loc);
        cache_next += (size_t)loc;
        nn = nhash(idnum, 0, ids.get(), maxped2, maxn);
        if (nn > 0 && eqns[nn] > 0) cache_anml_idx[eqns[nn]] = cache_ngen;
    }
    if (cache_ngen != ngen || cache_next != genotype_entries || !genotype_cache->complete())
        input_error("genotypes.txt changed while loading the cache");
#ifdef FINDHAP_LOW_MEMORY_PLUS
    genotype_cache->finish_append();
    printf("  %d genotype records cached on disk (bounded input batches)\n", cache_ngen);
#else
    printf("  %d genotype records cached in memory\n", cache_ngen);
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
    int shortest_length = *std::min_element(step_lengths.begin(), step_lengths.end());
    int largest_length = *std::max_element(step_lengths.begin(), step_lengths.end());
    int abs_maxseg = 0;
    for (chrome = chrome1; chrome <= chromes; chrome++) {
        int l_loc1 = loc1[chrome - 1];
        if (l_loc1 > locN[chrome - 1]) l_loc1 = locN[chrome - 1];
        abs_maxseg += 1 + (locN[chrome - 1] - l_loc1) / shortest_length;
    }

    // Globally allocate step-reusable buffers to prevent repeated page faults
    haplist_arr.reset(new int8_t[(size_t)largest_length * maxhap * abs_maxseg]);
    hapfreq_arr.reset(new int[(size_t)maxhap * abs_maxseg]);
    nhap_arr.reset(new int[(size_t)abs_maxseg]);
    keephap.reset(new HaplotypeId[(size_t)2 * maxped * abs_maxseg]);
    hapseg_arr.reset(new int8_t[(size_t)2 * loci]);
    proghaps.reset(new int[(size_t)(maxhap + 1)]);
    patmat.reset(new char[(size_t)2 * 2 * maxped * abs_maxseg]);
    order_arr.reset(new int[(size_t)(maxhap + 1) * abs_maxseg]);
    phaps.reset(new int[(size_t)abs_maxseg]);
    auto cross_buf = std::make_unique<char[]>(80 * 10 * abs_maxseg);
    auto num_cross = std::make_unique<int[]>(abs_maxseg);

    // Reused by the progeny phase in every step.
    auto l_proghaps_all = std::make_unique<int[]>((maxhap + 1) * threads);

    totprog = 1;
    for (i = 1; i <= nped; i++) {
        if (prog[i] > 0) totprog += prog[i];
    }
    fromprog.reset(new HaplotypeId[(size_t)totprog * abs_maxseg]);

    // ------------------------------------- Start with maxlen, end with minlen
    FINDHAP_END(setup_trace);
    for (step = 1; step <= steps; step++) {
        FINDHAP_PHASE(step_trace, "step", step);
        if (step == 1) {
            printf("\n");
            printf(" Begin with maxlen  %d\n", maxlen);
        } else {
            // Report previous maxhap
            maxnhap = 0;
            for (seg = 1; seg <= maxseg; seg++) {
                if (nhap_arr[seg - 1] > maxnhap) {
                    maxnhap = nhap_arr[seg - 1];
                    maxnseg = seg;
                }
            }
            printf("\n");
            printf("%8d most haplotypes found in segment %8d on chromosome %5d\n",
                   maxnhap, maxnseg, chro[maxnseg - 1]);
            prevmaxlen = maxlen;
            maxlen = step_lengths[step - 1];
            printf("\n");
            printf(" Step  %d change maxlen to  %d\n", step, maxlen);
            // Store previous haplotypes
            prevsegs = maxseg;
            prevkeephap.reset(new HaplotypeId[(size_t)2 * maxped * prevsegs]);
            // The output pass changes nhap_arr to a count of used haplotypes,
            // but their IDs can have gaps. Copy through the largest referenced
            // ID in each segment, preserving those IDs and all required alleles.
            prevhap_offsets.resize(prevsegs);
            std::vector<size_t> previous_sizes(prevsegs);
            size_t previous_bytes = 0;
            for (int ss = 0; ss < prevsegs; ++ss) {
                int highest_id = 0;
                size_t first = (size_t)ss * 2 * maxped;
                for (size_t entry = 0; entry < (size_t)2 * maxped; ++entry) {
                    int id = keephap[first + entry];
                    prevkeephap[first + entry] = id;
                    highest_id = std::max(highest_id, id);
                }
                prevhap_offsets[ss] = previous_bytes;
                size_t bytes = (size_t)prevmaxlen * highest_id;
                previous_sizes[ss] = bytes;
                if (bytes > std::numeric_limits<size_t>::max() - previous_bytes)
                    input_error("previous haplotype storage is too large");
                previous_bytes += bytes;
            }
            prevhaplist.reset();
            prevhaplist.reset(new int8_t[previous_bytes]);
            memcpy(prevlen.get(), len_arr.get(), (loci + 1) * sizeof(int));
            for (int ss = 0; ss < prevsegs; ++ss) {
                memcpy(prevhaplist.get() + prevhap_offsets[ss],
                       haplist_arr.get() + (size_t)ss * maxhap * prevmaxlen,
                       previous_sizes[ss]);
            }
            memcpy(prevstartloc.get(), startloc.get(), (loci + 1) * sizeof(int));
        }

        // Divide chromosomes into segments
        printf("\n");
        printf("  chromosome      length     markers    segments\n");
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
            printf("%12d%12ld%12d%12d\n", chrome,
                   (long)(locateN[chrome - 1] - locate1[chrome - 1] + 1),
                   locN[chrome - 1] - loc1[chrome - 1] + 1, segs);
            totlngth += locateN[chrome - 1] - locate1[chrome - 1] + 1;
        }
        printf("\n");
        printf(" %d total segments across chromosomes\n", maxseg);
        if (step == 1) {
            printf(" %d maximum loci on individual chromosome\n", maxloci);
            printf(" %ld physical length of longest chromosome\n", (long)maxlngth);
            printf(" %ld total length of genome processed\n", (long)totlngth);
        }
        printf("\n");

        // Initialize arrays in parallel to avoid single-thread memory bandwidth bottleneck
        #pragma omp parallel for schedule(static) num_threads(threads)
        for (seg = 1; seg <= maxseg; seg++) {
            memset(&haplist_arr[((size_t)seg - 1) * maxhap * maxlen], 0, (size_t)maxhap * maxlen * sizeof(int8_t));
            memset(&hapfreq_arr[((size_t)seg - 1) * maxhap], 0, (size_t)maxhap * sizeof(int));
        }
        memset(nhap_arr.get(), 0, maxseg * sizeof(int));
        memset(keephap.get(), 0, (size_t)2 * maxped * maxseg * sizeof(HaplotypeId));
        memset(hapseg_arr.get(), 0, 2 * loci * sizeof(int8_t));
        memset(phaps.get(), 0, maxseg * sizeof(int));

        #pragma omp parallel for schedule(static) num_threads(threads)
        for (seg = 1; seg <= maxseg; seg++) {
            for (int jj = 1; jj <= maxped; jj++) {
                // patmat init: ". "
                size_t base = PMIDX(jj, seg, maxped);
                patmat[base] = '.'; patmat[base + 1] = ' ';
                patmat[base + 2] = '.'; patmat[base + 3] = ' ';
            }
            for (int jj = 0; jj <= maxhap; jj++) {
                order_arr[ORDIDX(jj, seg, maxhap + 1)] = 0;
            }
        }

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
                for (int ll = 0; ll < prior_length; ++ll) {
                    char value = record[58 + ll];
                    if (value < '0' || value > '2') input_error("invalid haplotype digit in priors");
                    haplist_arr[IDX3(ll + 1, slot, prior_segment, maxlen, maxhap)] = value - '0';
                }
                hapfreq_arr[IDX2(slot, prior_segment, maxhap)] = std::lround(prior_frequency * nprior / 100.0);
                order_arr[ORDIDX(original_hap, prior_segment, maxhap + 1)] = slot;
                nhap_arr[prior_segment - 1] = std::max(slot, nhap_arr[prior_segment - 1]);
                ++loaded;
            }
            printf(" %d records read from hap.listpriors (unit 15)\n", loaded);
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
            printf(" %d records read from hap.foundpriors (unit 16)\n", loaded);
        }

        // Initialize sort order
        for (seg = 1; seg <= maxseg; seg++) {
            for (i = 1; i <= maxhap; i++) {
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
            printf("    iters set to minimum of 3 (was%4d)\n", iters);
            iters = 3;
        }
        printf("\n");
        iter1 = 1;
        if (exist15) iter1 = iters;

#if FINDHAP_PEDIGREE_REMATCH == 2
        pedigree_rematch::Plan rematch_plan;
        if (step > 1 && !exist15)
            rematch_plan = pedigree_rematch::make_plan(startloc.get(), len_arr.get(), maxseg,
                                                      prevstartloc.get(), prevlen.get(), prevsegs);
#endif

        // ---- Pre-expand genotypes ----
        FINDHAP_PHASE(expansion_trace, "expansion", step);
        memset(called.get(), 0, maxped * sizeof(int));
#ifdef FINDHAP_LOW_MEMORY_PLUS
        BoundedGenotypeWorkspace cache_expanded(cache_ngen, loci, startloc.get(), len_arr.get(), maxseg, chro.get());
#elif defined(FINDHAP_LOW_MEMORY)
        ChromosomeWorkspace cache_expanded(cache_ngen, loci, startloc.get(), len_arr.get(), maxseg, chro.get());
#else
        GenotypeWorkspace cache_expanded(cache_ngen, loci, startloc.get(), len_arr.get(), maxseg);
#endif
        auto expanded_scratch = std::make_unique<int8_t[]>((size_t)loci * threads);
        auto raw_scratch = std::make_unique<int8_t[]>((size_t)maxl * threads);
        int expanded_nhigh = 0;
        int* p_sumfreq = sumfreq_arr.get();
        int* p_nfreq = nfreq.get();
        int* p_sumcurr = sumcurr.get();
        int* p_cfreq = cfreq.get();
        int* p_ofreq = ofreq.get();
        int* p_birth = birth.get();
        int* p_high = high.get();
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
        const HaplotypeId* __restrict__ p_prevkeephap = prevkeephap.get();
        const int8_t* __restrict__ p_prevhaplist = prevhaplist.get();
        const size_t* __restrict__ p_prevhap_offsets = prevhap_offsets.data();
        const int* __restrict__ p_prevlen = prevlen.get();
        const int* __restrict__ p_prevstartloc = prevstartloc.get();

        for (size_t input_first = 0; input_first < (size_t)cache_ngen;) {
#ifdef FINDHAP_LOW_MEMORY_PLUS
        const size_t input_count = genotype_cache->prepare_sequential(input_first,
            std::min(cache_expanded.input_batch_size(), (size_t)cache_ngen - input_first));
#else
        const size_t input_count = std::min(cache_expanded.input_batch_size(), (size_t)cache_ngen - input_first);
#endif
        cache_expanded.begin_input(input_first, input_count);
        #pragma omp parallel for private(n, idnum, chipn, loc, nn, locus, seg, pnt, l, hap, anmlmiss) \
                reduction(+: expanded_nhigh, p_sumfreq[:loci], p_nfreq[:loci], p_sumcurr[:loci], p_cfreq[:loci], p_ofreq[:loci], genfreq[:6]) \
                schedule(static) num_threads(threads)
        for (n = (int)input_first + 1; n <= (int)(input_first + input_count); n++) {
            int8_t* expanded = expanded_scratch.get() + (size_t)omp_get_thread_num() * loci;
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
                    printf(" Chip lengths differ in genotypes.txt vs chromosome.data: %d %d  for chip %d  animal %d\n",
                           p_chiplen[l_chipn - 1], l_loc, l_chipn, l_idnum);
                    exit(1);
                }
            }
            p_cache_eqns_nn[n - 1] = p_eqns[l_nn];
            if (p_eqns[l_nn] > 0) p_chip_arr[p_eqns[l_nn] - 1] = l_chipn;
            // Pad low density to max loci
            memset(expanded, missing, loci);
            p_cache_called_cnt[n - 1] = 0;
            const int8_t* raw_genotype = genotype_cache->row(n - 1, l_loc,
                raw_scratch.get() + (size_t)omp_get_thread_num() * maxl);
            for (int ll = 1; ll <= l_loc; ll++) {
                int cl = p_chiplist[CHIPIDX(ll, l_chipn, loci)];
                if (cl < 1 || cl > loci) continue;
                expanded[cl - 1] = raw_genotype[ll - 1];
                if (raw_genotype[ll - 1] <= 2) {
                    p_cache_called_cnt[n - 1]++;
                } else {
                    expanded[cl - 1] = missing;
                }
            }
            if (p_eqns[l_nn] > 0) p_called[p_eqns[l_nn] - 1] = p_cache_called_cnt[n - 1];
            // Fill genotype from previous hap list
            if (step > 1) {
                int l_hap[2];
                for (int l_seg = 1; l_seg <= prevsegs; l_seg++) {
                    for (int l_pnt = 1; l_pnt <= 2; l_pnt++) {
                        l_hap[l_pnt - 1] = p_prevkeephap[KHIDX(l_pnt, p_cache_eqns_nn[n - 1], l_seg, maxped)];
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
                for (locus = 1; locus <= loci; locus++)
                    genfreq[expanded[locus - 1]]++;
                if (anmlmiss <= 0.6 * loci || exist15) {
                    expanded_nhigh++;
                    p_high[nn - 1] = 1;
                } else {
                    if (anmlmiss < 0.95 * loci) p_high[nn - 1] = 2;
                }
                cache_expanded.store_row(n - 1, expanded);
            }
        }
        cache_expanded.end_input();
        input_first += input_count;
        }
        cache_expanded.finish_input();
#ifdef FINDHAP_LOW_MEMORY_PLUS
        genotype_cache->release_batch();
#endif
        expanded_scratch.reset();
        raw_scratch.reset();
        // ---- End pre-expansion ----
        FINDHAP_END(expansion_trace);

        for (iter = iter1; iter <= iters; iter++) {
            printf(" Step  %d   Haplotype iteration  %d\n", step, iter);
            if (iter >= iters / 2) switch_val = 0;
            notfound = 0;
            n13 = 0;
            nhigh = iter == iter1 ? expanded_nhigh : 0;
            nmed = 0;

            if (iter > iter1) {
                // Zero active hapfreq entries
                #pragma omp parallel for num_threads(threads)
                for (seg = 1; seg <= maxseg; seg++) {
                    // i is shared outside this worksharing loop. A local
                    // counter prevents incomplete resets under concurrency.
                    for (int hap = 1; hap <= nhap_arr[seg - 1]; hap++) {
                        hapfreq_arr[IDX2(hap, seg, maxhap)] = 0;
                    }
                }
            }

            // Serial pre-pass
            for (n = 1; n <= cache_ngen; n++) {
                nn = cache_eqns_nn[n - 1];
                if (nn == 0) {
                    if (iter == iter1 && step == 1)
                        printf(" no pedigree for genotyped animal  %d\n", cache_id[n - 1]);
                    continue;
                }
                n13++;
                listeqn[nn] = n13;
            }

            if (iter == 2) {
                #pragma omp parallel for private(n, nn) reduction(+: nmed) \
                    schedule(static) num_threads(threads)
                for (n = 1; n <= cache_ngen; n++) {
                    nn = cache_eqns_nn[n - 1];
                    if (nn == 0) continue;
                    if (high[nn - 1] != 0) nmed++;
                }
            }

            // ---- Parallel segment processing ----
            FINDHAP_PHASE(population_trace, "population", step, iter);
            for (size_t tile_first = 0; tile_first < (size_t)maxseg;) {
#ifdef FINDHAP_LOW_MEMORY
            const size_t tile_end = maxseg;
            const int population_threads = cache_expanded.population_workers(threads);
            auto population_workers = cache_expanded.make_population_workers(population_threads);
            std::atomic<bool> population_failed{false};
            std::exception_ptr population_error;
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
                int n, nn, j, k, l, pnt, pedx_temp, length, found_temp;
                int suggest[16] = {0}; // Thread-private initialized ONCE to zero
                int l_found[2];
                int l_pedx[2];
                std::vector<int8_t> segment_scratch(maxlen);
#ifdef FINDHAP_LOW_MEMORY
                auto& population_worker = *population_workers[omp_get_thread_num()];
#endif
                #pragma omp for schedule(dynamic, 1)
                for (seg = (int)tile_first + 1; seg <= (int)tile_end; seg++) {
#ifdef FINDHAP_LOW_MEMORY
                if (population_failed.load(std::memory_order_relaxed)) continue;
                try {
                FINDHAP_PHASE(segment_read_trace, "segment_read", step, iter, seg);
                population_worker.begin_segment(seg - 1);
                FINDHAP_END(segment_read_trace);
#endif
                FINDHAP_PHASE(segment_trace, "population_segment", step, iter, seg,
                              chro[seg - 1], len_arr[seg], &nhap_arr[seg - 1]);
                // Own this index only during one population segment pass.
                // Rebuild after pedigree/prior mutations in later iterations.
                std::unique_ptr<CandidateIndex> candidate_index;
                for (n = 1; n <= cache_ngen; n++) {
                    nn = cache_eqns_nn[n - 1];
                    if (nn == 0) continue;
                    if (iter == iter1 && high[nn - 1] != 1) continue;
                    if (iter == 2 && high[nn - 1] == 0) continue;
#if FINDHAP_POP_SKIP_UNUSED
                    if (switch_val == 0)
#endif
                    for (int s = 0; s < 16; s++) suggest[s] = 0;
                    for (pnt = 1; pnt <= 2; pnt++) {
                        l_pedx[pnt - 1] = ped[IDX2(pnt, nn, 2)];
                    }
                    if (sexv[nn - 1] == 'M' && seg >= Xseg)
                        l_pedx[0] = l_pedx[1];
                    for (pnt = 1; pnt <= 2; pnt++) {
                        if (l_pedx[pnt - 1] > 0) {
                            if (pnt == 1) {
                                size_t base = PMIDX(nn, seg, maxped);
                                patmat[base] = 'p'; patmat[base + 1] = ' ';
                            }
                            if (pnt == 2) {
                                size_t base = PMIDX(nn, seg, maxped) + 2;
                                patmat[base] = 'm'; patmat[base + 1] = ' ';
                            }
                            if (listeqn[l_pedx[pnt - 1]] > 0) {
                                if (pnt == 1) {
                                    size_t base = PMIDX(nn, seg, maxped);
                                    patmat[base] = ' '; patmat[base + 1] = '0';
                                }
                                if (pnt == 2) {
                                    size_t base = PMIDX(nn, seg, maxped) + 2;
                                    patmat[base] = ' '; patmat[base + 1] = '0';
                                }
                            }
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
                    if (!candidate_index && length >= FINDHAP_POP_INDEX_LENGTH && nhap_arr[seg - 1] >= FINDHAP_POP_INDEX_HAPS)
                        candidate_index = std::make_unique<CandidateIndex>(length,
                            haplist_arr.get() + (size_t)(seg - 1) * maxhap * maxlen,
                            maxlen, nhap_arr[seg - 1]);
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
                } catch (...) {
                    population_failed.store(true, std::memory_order_relaxed);
                    #pragma omp critical(findhap_population_error)
                    { if (!population_error) population_error = std::current_exception(); }
                }
#endif
            } // end seg loop
            } // Close omp parallel block
#ifdef FINDHAP_LOW_MEMORY
            cache_expanded.finish_population_workers(population_workers);
            if (population_error) std::rethrow_exception(population_error);
#else
            FINDHAP_PHASE(tile_write_trace, "tile_write", step, iter, tile_first + 1);
            cache_expanded.end_tile(tile_end);
            FINDHAP_END(tile_write_trace);
#endif
            tile_first = tile_end;
            }
            cache_expanded.finish_population();
            FINDHAP_END(population_trace);

            // ---- Allele frequency report ----
            if (iter == iter1) {
                printf(" %d records read from file genotypes.txt\n", n13);
                if (nhigh < n13) {
                    printf(" %d high  density genotypes used in iter 1\n", nhigh);
                    printf(" %d lower density genotypes used in later iters\n", n13 - nhigh);
                }
                printf("\n");
                printf(" Frequency of genotype codes input in step  %d\n", step);
                for (i = 0; i <= 9; i++) {
                    if (genfreq[i] > 0)
                        printf("%15ld genotypes with code %2d%12.5f%%\n",
                               (long)genfreq[i], i, 100.0 * genfreq[i] / n13 / loci);
                }
                for (locus = 1; locus <= loci; locus++) {
                    if (nfreq[locus - 1] > 0) {
                        freq[locus - 1] = 0.5f * sumfreq_arr[locus - 1] / nfreq[locus - 1];
                    } else {
                        freq[locus - 1] = 0.5f;
                    }
                }
                printf("\n");
                printf(" Allele frequencies estimated\n");
                for (locus = 1; locus <= std::min(10, loci); locus++) {
                    printf("%8.4f", freq[locus - 1]);
                }
                printf("\n\n");
            }
            if (iter == 2 && nmed < n13 && nmed != nhigh) {
                printf(" %d medium and high density genotypes used in iter 2\n", nmed);
                printf(" %d low density genotypes used in iters 3+\n", n13 - nmed);
            }

            ncross = 0;
            if (iter == iters && step == steps)
                fprintf(fp27, " animkey pntkey segment endloc beginloc new_hap pnthap1 pnthap2\n");

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
                    printf("%9d maximum genotyped progeny for parent id = %9d\n", maxprog, maxpnt);
            }

            // Set up parent location vector
            totprog = 1;
            for (i = 1; i <= nped; i++) {
                if (prog[i] > 0) {
                    pntloc[i] = totprog;
                    totprog += prog[i];
                    if (prog[i] >= maxprog) maxpnt = animkey[i];
                    maxprog = std::max(maxprog, prog[i]);
                }
            }
            if (step == 1 && iter == iter1) {
                printf("%9d maximum progeny for parent id = %9d\n", maxprog, maxpnt);
                printf("\n");
            }
#ifdef FINDHAP_DEBUG
            // Diagnostic dump of nhap
            {
                FILE* fp98 = fopen("cpp_diag.txt", "a");
                if (fp98) {
                    fprintf(fp98, "=== NHAP AFTER STEP %d ITER %d ===\n", step, iter);
                    for (seg = 1; seg <= maxseg; seg++) {
                        fprintf(fp98, "seg=%d nhap=%d\n", seg, nhap_arr[seg - 1]);
                    }
                    fclose(fp98);
                }
            }
#endif

            if (switch_val == 1) continue;
            FINDHAP_PHASE(pedigree_trace, "pedigree", step, iter);
            FINDHAP_PHASE(pedigree_dams_trace, "pedigree_dams", step, iter);
            memset(suggest, 0, sizeof(suggest));

            // ===================================================================
            // Pedigree matching and inheritance correction
            // ===================================================================

            // Destination segments own disjoint mutable library state. Keep
            // the original animal order within each segment; do not parallelise
            // families or generations sharing a parent or haplotype library.
#if FINDHAP_PEDIGREE_REMATCH == 2
            if (step > 1 && !exist15 && rematch_plan.disjoint) {
                std::vector<int> rematch_animals;
                for (int animal = 1; animal <= nped; ++animal)
                    if (listeqn[animal] == 0) rematch_animals.push_back(animal);
                if (!rematch_animals.empty()) {
                    #pragma omp parallel num_threads(threads)
                    {
                        int local_suggest[16] = {};
                        std::vector<int8_t> reconstructed;
                        #pragma omp for schedule(dynamic, 1)
                        for (int current = 1; current <= maxseg; ++current) {
                            const int length = len_arr[current];
                            reconstructed.resize(length);
                            for (int animal : rematch_animals) {
                                const int known = pedigree_rematch::reconstruct(
                                    reconstructed.data(), length, animal, maxped,
                                    rematch_plan.segments[current - 1], prevkeephap.get(),
                                    prevhaplist.get(), prevhap_offsets.data(), prevmaxlen, missing);
                                if (known == 0) continue;
                                int local_found[2] = {};
                                listhap<0>(reconstructed.data(), haplist_arr.get(), hapfreq_arr.get(),
                                           order_arr.get(), local_suggest, length, maxlen,
                                           nhap_arr.get(), maxhap, current, maxseg, local_found, errate);
                                if (local_found[0] != local_found[1]) {
                                    keephap[KHIDX(1, animal, current, maxped)] = local_found[0];
                                    keephap[KHIDX(2, animal, current, maxped)] = local_found[1];
                                }
                            }
                        }
                    }
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
                            hap[pnt - 1] = prevkeephap[KHIDX(pnt, i, seg, maxped)];
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
                    #pragma omp parallel for private(length, l, locus, found, known) num_threads(threads)
                    for (seg = 1; seg <= maxseg; seg++) {
                        known = 0;
                        for (l = 1; l <= len_arr[seg]; l++) {
                            locus = startloc[seg] + l - 1;
                            if (genotype[locus - 1] != missing) known++;
                        }
                        int l_found[2] = {0, 0};
                        length = len_arr[seg];
                        if (known > 0) {
                            if (switch_val == 1) {
                                listhap<1>(&genotype[startloc[seg] - 1],
                                        haplist_arr.get(), hapfreq_arr.get(), order_arr.get(), suggest,
                                        length, maxlen, nhap_arr.get(), maxhap, seg, maxseg, l_found, errate);
                            } else {
                                listhap<0>(&genotype[startloc[seg] - 1],
                                        haplist_arr.get(), hapfreq_arr.get(), order_arr.get(), suggest,
                                        length, maxlen, nhap_arr.get(), maxhap, seg, maxseg, l_found, errate);
                            }
                            if (l_found[0] != l_found[1]) {
                                keephap[KHIDX(1, i, seg, maxped)] = l_found[0];
                                keephap[KHIDX(2, i, seg, maxped)] = l_found[1];
                            }
                        }
                    }
                }
            }

            }

            FINDHAP_END(pedigree_dams_trace);
            FINDHAP_PHASE(pedigree_main_trace, "pedigree_main", step, iter);
            // Each chromosome owns disjoint segment/genotype/frequency state.
            // Preserve animal order within it, including parent mutations and
            // adjacent-segment orientation. The final pass retains the original
            // animal-major traversal so crossover records need no reordering.
            const bool chromosome_pass = FINDHAP_PEDIGREE_CHROMOSOMES &&
                chromes - chrome1 + 1 >= threads &&
                !(iter == iters && step == steps);
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
            #pragma omp parallel private(i, pnt, j, grdpnt, pnthap, mismatch, match_arr, matches, pedx, found, k, l, locus, hold) \
                reduction(+: ncross) num_threads(pedigree_threads)
            {
                // These operations sit inside millions of animal/segment
                // visits. Outlining the lambdas hides loop-invariant state
                // and proved substantially slower in the qualification study.
                auto process_segment = [&](int seg, bool independent) __attribute__((always_inline)) {
                    if (iter == iters && step == steps) num_cross[seg - 1] = 0;
                    int l_match[2][2] = {{0,0},{0,0}};
                    int l_matches = 0;
                    int l_pedx[2], l_found[2], l_pnthap[2];
                    int l_cross[2], l_where[2][2];

                    for (pnt = 1; pnt <= 2; pnt++) {
                        l_pedx[pnt - 1] = ped[IDX2(pnt, i, 2)];
                    }
                    if (sexv[i - 1] == 'M' && seg >= Xseg)
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
                                                if (num_cross[seg - 1] <= 10)  // guard: max 10 per seg (matches Fortran 10*maxseg global limit)
                                                    snprintf(&cross_buf[(num_cross[seg - 1] - 1) * 80 + ((size_t)seg - 1) * 80 * 10], 80,
                                                         "%9d%9d%7d%7d%7d%7d%7d%7d",
                                                         animkey[i], animkey[ped[IDX2(pnt, i, 2)]],
                                                         seg, l_where[0][grdpnt - 1], l_where[1][grdpnt - 1],
                                                         l_found[j - 1], l_pnthap[grdpnt - 1], l_pnthap[2 - grdpnt]);
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
                            size_t base = PMIDX(i, seg, maxped);
                            patmat[base] = matchcode[l_match[0][0] - 1][0];
                            patmat[base + 1] = matchcode[l_match[0][0] - 1][1];
                        }
                        if (l_match[1][1] > 0) {
                            size_t base = PMIDX(i, seg, maxped) + 2;
                            patmat[base] = matchcode[l_match[1][1] - 1][0];
                            patmat[base + 1] = matchcode[l_match[1][1] - 1][1];
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
                        size_t base = PMIDX(i, seg, maxped);
                        patmat[base] = '?'; patmat[base + 1] = '?';
                        patmat[base + 2] = '?'; patmat[base + 3] = '?';
                    }

                    // Fill missing hap of mate + base frequency + progeny imputation
                    for (pnt = 1; pnt <= 2; pnt++) {
                        if (l_pedx[pnt - 1] > 0 &&
                            (listeqn[l_pedx[2 - pnt]] > 0 || gprog[l_pedx[2 - pnt]] > 3)) {
                            for (j = 1; j <= 2; j++) {
                                if ((l_match[j - 1][2 - pnt] > 0 && l_match[2 - j][2 - pnt] == 0) ||
                                    l_match[j - 1][2 - pnt] == 7) {
                                    fromprog[IDX2((independent ? parent_slots[2 * size_t(i) + ((sexv[i - 1] == 'M' && seg >= Xseg) ? 1 : pnt - 1)] : pntloc[l_pedx[pnt - 1]]), seg, totprog) ] = l_found[2 - j];
                                    if (keephap[KHIDX(1, l_pedx[pnt - 1], seg, maxped)] == 0 &&
                                        keephap[KHIDX(2, l_pedx[pnt - 1], seg, maxped)] != l_found[2 - j]) {
                                        size_t base = PMIDX(i, seg, maxped) + (pnt - 1) * 2;
                                        patmat[base] = ' '; patmat[base + 1] = '1';
                                    } else {
                                        if (keephap[KHIDX(2, l_pedx[pnt - 1], seg, maxped)] == 0 &&
                                            keephap[KHIDX(1, l_pedx[pnt - 1], seg, maxped)] != l_found[2 - j]) {
                                            size_t base = PMIDX(i, seg, maxped) + (pnt - 1) * 2;
                                            patmat[base] = ' '; patmat[base + 1] = '2';
                                        }
                                    }
                                }
                            }
                            // Impute dam from son's X
                            if (sexv[i - 1] == 'M' && seg >= Xseg) {
                                fromprog[IDX2((independent ? parent_slots[2 * size_t(i) + 1] : pntloc[l_pedx[1]]), seg, totprog)] = l_found[0];
                                if (keephap[KHIDX(1, l_pedx[1], seg, maxped)] == 0) {
                                    keephap[KHIDX(1, l_pedx[1], seg, maxped)] = l_found[0];
                                    size_t base = PMIDX(i, seg, maxped);
                                    patmat[base] = ' '; patmat[base + 1] = '1';
                                }
                                if (keephap[KHIDX(2, l_pedx[1], seg, maxped)] == 0 &&
                                    keephap[KHIDX(1, l_pedx[1], seg, maxped)] != l_found[0]) {
                                    keephap[KHIDX(2, l_pedx[1], seg, maxped)] = l_found[0];
                                    size_t base = PMIDX(i, seg, maxped);
                                    patmat[base] = ' '; patmat[base + 1] = '2';
                                }
                                size_t base = PMIDX(i, seg, maxped) + 2;
                                patmat[base] = ' '; patmat[base + 1] = 'Y';
                            }
                        }
                        // Base frequency estimation
                        if (birth[i - 1] < baseyr && listeqn[l_pedx[pnt - 1]] == 0 && l_found[pnt - 1] > 0) {
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
                        size_t base = PMIDX(i, j, maxped);
                        if (patmat[base] == '?' && patmat[base + 1] == '?') {
                            if (segfirst == 0) { segfirst = j; seglast = j; }
                            if (j == seglast + 1) seglast = j;
                        }
                    }
                    if (segfirst == 0) return;
                    // Neighbor segment consistency check
                    char l_newpatmat[2][3] = {"??", "??"};
                    for (pnt = 1; pnt <= 2; pnt++) {
                        if (segfirst > seg) {
                            size_t base = PMIDX(i, segfirst - 1, maxped) + (pnt - 1) * 2;
                            if ((patmat[base] == ' ' && patmat[base + 1] == '1') ||
                                (patmat[base] == '-' && patmat[base + 1] == '1') ||
                                (patmat[base] == 'X' && patmat[base + 1] == '1'))
                                { l_newpatmat[pnt - 1][0] = '?'; l_newpatmat[pnt - 1][1] = '1'; }
                            if ((patmat[base] == ' ' && patmat[base + 1] == '2') ||
                                (patmat[base] == '-' && patmat[base + 1] == '2') ||
                                (patmat[base] == 'X' && patmat[base + 1] == '2'))
                                { l_newpatmat[pnt - 1][0] = '?'; l_newpatmat[pnt - 1][1] = '2'; }
                        }
                        if (seglast < seg + segs - 1) {
                            size_t base = PMIDX(i, seglast + 1, maxped) + (pnt - 1) * 2;
                            if ((patmat[base] == ' ' && patmat[base + 1] == '1') ||
                                (patmat[base] == '-' && patmat[base + 1] == '1') ||
                                (patmat[base] == 'X' && patmat[base + 1] == '2')) {
                                if (l_newpatmat[pnt - 1][1] != '2') {
                                    l_newpatmat[pnt - 1][0] = '?'; l_newpatmat[pnt - 1][1] = '1';
                                } else {
                                    l_newpatmat[pnt - 1][0] = '?'; l_newpatmat[pnt - 1][1] = '?';
                                }
                            }
                            if ((patmat[base] == ' ' && patmat[base + 1] == '2') ||
                                (patmat[base] == '-' && patmat[base + 1] == '2') ||
                                (patmat[base] == 'X' && patmat[base + 1] == '1')) {
                                if (l_newpatmat[pnt - 1][1] != '1') {
                                    l_newpatmat[pnt - 1][0] = '?'; l_newpatmat[pnt - 1][1] = '2';
                                } else {
                                    l_newpatmat[pnt - 1][0] = '?'; l_newpatmat[pnt - 1][1] = '?';
                                }
                            }
                        }
                    }
                    // Switch heterozygotes
                    for (j = segfirst; j <= seglast; j++) {
                        hapswap = 0;
                        for (pnt = 1; pnt <= 2; pnt++) {
                            if (l_newpatmat[pnt - 1][1] == '1') {
                                if (keephap[KHIDX(3 - pnt, i, j, maxped)] ==
                                    keephap[KHIDX(1, ped[IDX2(pnt, i, 2)], j, maxped)]) hapswap = 1;
                                break;
                            }
                            if (l_newpatmat[pnt - 1][1] == '2') {
                                if (keephap[KHIDX(3 - pnt, i, j, maxped)] ==
                                    keephap[KHIDX(2, ped[IDX2(pnt, i, 2)], j, maxped)]) hapswap = 1;
                                break;
                            }
                        }
                        if (hapswap == 1) {
                            hold = keephap[KHIDX(2, i, j, maxped)];
                            keephap[KHIDX(2, i, j, maxped)] = keephap[KHIDX(1, i, j, maxped)];
                            keephap[KHIDX(1, i, j, maxped)] = hold;
                        }
                        for (pnt = 1; pnt <= 2; pnt++) {
                            size_t base = PMIDX(i, j, maxped) + (pnt - 1) * 2;
                            if (keephap[KHIDX(pnt, i, j, maxped)] ==
                                keephap[KHIDX(1, ped[IDX2(pnt, i, 2)], j, maxped)]) {
                                patmat[base] = '?'; patmat[base + 1] = '1';
                            } else {
                                patmat[base] = '?'; patmat[base + 1] = '2';
                            }
                        }
                    }
                };
                if (chromosome_pass) {
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
                    const int first_segment = 1 + tid * width + std::min(tid, extra);
                    const int past_segment = first_segment + width + (tid < extra);
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
                                    fprintf(fp27, "%s\n", &cross_buf[k * 80 + ((size_t)seg - 1) * 80 * 10]);
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
                } // end animal-major fallback
            } // end persistent pedigree team
            // The legacy progeny stage observes this shared loop endpoint.
            if (chromosome_pass) seg = maxseg + 1;
            FINDHAP_END(pedigree_main_trace);
            FINDHAP_PHASE(pedigree_progeny_trace, "pedigree_progeny", step, iter);

            // Process progeny by parent
            nhigh = 0;
            totprog = 1;
            for (i = 1; i <= nped; i++) {
                if (prog[i] > 0) {
                    pntloc[i] = totprog;
                    totprog += prog[i];
                }
            }

            nhigh = 0;
            #pragma omp parallel private(i, pnt, pedx, mosthap, mosthaps, j, agree, grdpnt, search, proghaps) \
                num_threads(threads)
            {
                int tid = omp_get_thread_num();
                int* l_proghaps = &l_proghaps_all[tid * (maxhap + 1)];
                memset(l_proghaps, 0, (maxhap + 1) * sizeof(int));

                for (i = 1; i <= nped; i++) {
                    if (prog[i] > 0) {
                        if (listeqn[i] == 0) {
                            int l_pedx[2];
                            for (pnt = 1; pnt <= 2; pnt++) l_pedx[pnt - 1] = ped[IDX2(pnt, i, 2)];
                            if (sexv[i - 1] == 'M' && seg >= Xseg) l_pedx[0] = l_pedx[1];

                            #pragma omp for
                            for (seg = 1; seg <= maxseg; seg++) {
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
                                    (sexv[i - 1] == 'M' && seg >= Xseg))
                                    keephap[KHIDX(2, i, seg, maxped)] = keephap[KHIDX(1, i, seg, maxped)];
                                for (j = 1; j <= prog[i]; j++) {
                                    l_proghaps[fromprog[IDX2(pntloc[i] + j - 1, seg, totprog)]] = 0;
                                }
                            }
                        }
                    }
                }
            }
            // Every worker walks the pedigree loop above. Count once, outside
            // the parallel region, to avoid duplicate counts and shared writes.
            for (i = 1; i <= nped; ++i) {
                if (called[i - 1] > 0.4f * loci) nhigh++;
            }

        } // end iter loop

#ifdef FINDHAP_LOW_MEMORY
        printf(" TurboHap LM working storage: %zu bytes, %llu read, %llu written, %s\n",
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
        if (step == steps) {
            FINDHAP_PHASE(output_trace, "output", step);
            printf("\n");
            high1 = std::max(high1, nhigh);
            // Match gfortran list-directed INTEGER(4) and character output.
            fprintf(fp22, "%12d  high-density genotypes in reference\n", high1);
            fprintf(fp22, "%12d  segment where X-specific loci begin\n", Xseg);
            fprintf(fp24, "                   total     markers   total SNP\n");
            fprintf(fp24, "    animal  chip  progeny    observed    filled  corr2(est,true)\n");
            printf(" First 10 genotypes created\n\n");

            memset(hapfreq_arr.get(), 0, maxhap * maxseg * sizeof(int));
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
            constexpr size_t output_buffer_budget = 32 * 1024 * 1024;
            const size_t geno_record_bytes = (size_t)loci + 32;
            const size_t hap_record_bytes = (size_t)2 * loci + 32;
            const size_t bytes_per_animal =
                (genout > 0 ? geno_record_bytes : 0) +
                (hapout == 1 ? hap_record_bytes : 0) +
                3 * sizeof(int) + sizeof(float) + 50 * sizeof(int8_t);
            const int output_chunk_size = static_cast<int>(std::max<size_t>(1,
                std::min({(size_t)1000, (size_t)nped, output_buffer_budget / bytes_per_animal})));
            std::unique_ptr<char[]> chunk_buffer_geno, chunk_buffer_hap;
            // Every emitted byte is filled before the serial writer reads it.
            if (genout > 0)
                chunk_buffer_geno.reset(new char[output_chunk_size * geno_record_bytes]);
            if (hapout == 1)
                chunk_buffer_hap.reset(new char[output_chunk_size * hap_record_bytes]);
            auto thread_genotype = std::make_unique<int8_t[]>((size_t)loci * threads);
            auto thread_genovec2_buf = std::make_unique<int8_t[]>((size_t)loci * threads);
            auto thread_raw_genotypes = std::make_unique<int8_t[]>((size_t)maxl * threads);
            auto usable_chunk = std::make_unique<int[]>(output_chunk_size);
            auto imputed_chunk = std::make_unique<int[]>(output_chunk_size);
            auto corr2_chunk = std::make_unique<float[]>(output_chunk_size);
            auto i_chunk = std::make_unique<int[]>(output_chunk_size);
            auto preview_chunk = std::make_unique<int8_t[]>((size_t)output_chunk_size * 50);
#ifdef FINDHAP_LOW_MEMORY_PLUS
            std::vector<size_t> output_raw_rows(output_chunk_size);
            int prepared_output_count = 0;
            std::exception_ptr output_cache_error;
#endif

#if FINDHAP_BUFFER_INHERIT
            std::vector<char> inherit_payload(2 * size_t(maxseg) +
                                             (FINDHAP_INHERIT_RECORD_OUTPUT ? 64 : 1));
#endif

            // Reuse one team and its reduction arrays across all output batches.
            #pragma omp parallel private(ii, i, imputed, haps, seg, pnt, loc, locus, correct, corr2, \
                usable, chrome, n, idnum, chipn, l, m, k) \
                reduction(+: nmale, ndrop, nadd, n13a, p_missall[:loci], p_missadd[:loci], genfreq[:10]) \
                num_threads(threads)
            {
                for (int chunk_start = 1; chunk_start <= nped;) {
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

                    #pragma omp for schedule(static)
                    for (ii = chunk_start; ii <= chunk_end; ii++) {
                        int tid = omp_get_thread_num();
                        int chunk_idx = ii - chunk_start;
                        i = eqns[nhash(keyvec[ii - 1], 0, ids.get(), maxped2, maxn)];
                        i_chunk[chunk_idx] = i;
                        imputed = 0;
                        haps = 0;

                        for (seg = 1; seg <= maxseg; seg++) {
                            for (pnt = 1; pnt <= 2; pnt++) {
                                if (keephap[KHIDX(pnt, i, seg, maxped)] > 0) {
                                    haps++;
                                    for (loc = 1; loc <= len_arr[seg]; loc++) {
                                        if (haplist_arr[IDX3(loc, keephap[KHIDX(pnt, i, seg, maxped)], seg, maxlen, maxhap)] != 1)
                                            imputed++;
                                    }
                                }
                            }
                        }
                        if (listeqn[i] > 0 && high[i - 1] == 1) {
                            imputed = std::max(imputed / 2, called[i - 1]);
                        } else {
                            imputed = imputed / 2;
                        }

                        // Corr**2 (est, true genotype)
                        if (called[i - 1] > 0) {
                            correct = (float)high1 / (high1 + 50.0f);
                            correct = correct * called[i - 1] / (called[i - 1] + 120.0f * totlngth / 3.0e9);
                            corr2 = (0.9998f * called[i - 1] + correct * (loci - called[i - 1])) / loci;
                        } else {
                            correct = (float)gprog[i] / (gprog[i] + 1.0f);
                            corr2 = (0.9998f * called[i - 1] + correct * (imputed - called[i - 1])) / loci;
                        }
                        usable = 0;
                        if (corr2 > 0.84f * 0.84f) {
                            if (listeqn[i] > 0) usable = 1;
                            if (damout >= 1 && sexv[i - 1] == 'F') usable = 1;
                            if (damout == 2 && sexv[i - 1] == 'M') usable = 1;
                            if (listeqn[i] == 0 && sexv[i - 1] == 'M') nmale++;
                        } else {
                            if (listeqn[i] > 0) ndrop++;
                        }

                        imputed_chunk[chunk_idx] = imputed;
                        corr2_chunk[chunk_idx] = corr2;
                        usable_chunk[chunk_idx] = usable;

                        // Create genotypes.filled from haplotypes
                        if (usable == 1) {
                            int l_pnthap[2];
                            for (seg = 1; seg <= maxseg; seg++) {
#if FINDHAP_OUTPUT_STREAMS
                                const auto first = keephap[KHIDX(1, i, seg, maxped)];
                                const auto second = keephap[KHIDX(2, i, seg, maxped)];
                                const int8_t* a = first > 0 ? &haplist_arr[IDX3(1, first, seg, maxlen, maxhap)] : nullptr;
                                const int8_t* b = second > 0 ? &haplist_arr[IDX3(1, second, seg, maxlen, maxhap)] : nullptr;
                                turbohap_output::reconstruct(a, b, thread_genotype.get() + size_t(tid)*loci + startloc[seg]-1,
                                    len_arr[seg], gencode, output_gencode, sexv[i-1]=='M' && seg>=Xseg);
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
                                    thread_genotype[tid * loci + locus - 1] = gencode[l_pnthap[0]][l_pnthap[1]];
                                    if (sexv[i - 1] == 'M' && seg >= Xseg) {
                                        if (thread_genotype[tid * loci + locus - 1] == 1 ||
                                            thread_genotype[tid * loci + locus - 1] > 2)
                                            thread_genotype[tid * loci + locus - 1] = 5;
                                    }
                                }
#endif
                            }
                        }

                        n = cache_anml_idx[i];
                        if (n > 0) {
                            idnum = cache_id[n - 1];
                            chipn = cache_chipn[n - 1];
                            loc = cache_loc[n - 1];
                            const int8_t* raw_genotype = genotype_cache->row(n - 1, loc,
                                thread_raw_genotypes.get() + (size_t)tid * maxl);
                            for (int ll = 0; ll < loci; ll++)
                                thread_genovec2_buf[tid * loci + ll] = missing;
                            for (int ll = 1; ll <= loc; ll++) {
                                int cl = chiplist_arr[CHIPIDX(ll, chipn, loci)];
                                if (cl >= 1 && cl <= loci)
                                    thread_genovec2_buf[tid * loci + cl - 1] =
                                        raw_genotype[ll - 1] > 2 ? missing : raw_genotype[ll - 1];
                            }
                        }
                        if (n > 0) {
                            n13a++;
                            for (locus = 1; locus <= loci; locus++) {
                                if (thread_genovec2_buf[tid * loci + locus - 1] == 0)
                                    thread_genotype[tid * loci + locus - 1] = 0;
                                if (thread_genovec2_buf[tid * loci + locus - 1] == 1)
                                    thread_genotype[tid * loci + locus - 1] = 1;
                                if (thread_genovec2_buf[tid * loci + locus - 1] == 2)
                                    thread_genotype[tid * loci + locus - 1] = 2;
                            }
                        }

                        if (usable == 1) {
                            if (listeqn[i] == 0) nadd++;
                            memcpy(&preview_chunk[chunk_idx * 50], &thread_genotype[tid * loci], std::min(50, loci));
                            char* geno_record = genout > 0 ? &chunk_buffer_geno[chunk_idx * geno_record_bytes] : nullptr;
                            if (geno_record)
                                snprintf(geno_record, 32, "%10d%10d%10d ", animkey[i], chip_arr[i - 1], loci);
                            for (locus = 1; locus <= loci; locus++) {
                                // The baseline counts and previews codes before genout transforms them.
                                genfreq[thread_genotype[tid * loci + locus - 1]]++;
                                if (genout == 5 && thread_genotype[tid * loci + locus - 1] > 2)
                                    thread_genotype[tid * loci + locus - 1] = 5;
                                if (genout == 2) {
                                    if (thread_genotype[tid * loci + locus - 1] == 3)
                                        thread_genotype[tid * loci + locus - 1] = static_cast<int8_t>(freq[locus - 1] + 0.5f);
                                    if (thread_genotype[tid * loci + locus - 1] == 4)
                                        thread_genotype[tid * loci + locus - 1] = static_cast<int8_t>(freq[locus - 1] + 1.5f);
                                    if (thread_genotype[tid * loci + locus - 1] == 5) {
                                        thread_genotype[tid * loci + locus - 1] = 1;
                                        if (freq[locus - 1] < 0.3333f) thread_genotype[tid * loci + locus - 1] = 0;
                                        if (freq[locus - 1] > 0.6666f) thread_genotype[tid * loci + locus - 1] = 2;
                                    }
                                }
                                if (thread_genotype[tid * loci + locus - 1] == missing) {
                                    p_missall[locus - 1]++;
                                    if (listeqn[i] == 0) {
                                        p_missadd[locus - 1]++;
                                    }
                                }
                                if (geno_record)
                                    geno_record[30 + locus] = static_cast<char>(thread_genotype[tid * loci + locus - 1] + '0');
                            }
                        }

                        // Write haplotypes.txt
                        if (hapout == 1 && usable == 1) {
                            char* hap_record = &chunk_buffer_hap[chunk_idx * hap_record_bytes];
                            snprintf(hap_record, 32, "%10d%10d%10d ", animkey[i], 1, loci);
                            m = 0;
                            for (seg = 1; seg <= maxseg; seg++) {
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

                    // One worker writes the completed batch in ID order. Both
                    // worksharing barriers protect the buffer before it is reused.
                    #pragma omp single
                    {
                        FINDHAP_PHASE(submission_trace, "output_submission", step, 0, 0, chunk_start);
                        for (int chunk_idx = 0; chunk_idx < chunk_count; chunk_idx++) {
                            i = i_chunk[chunk_idx];
                            imputed = imputed_chunk[chunk_idx];
                            corr2 = corr2_chunk[chunk_idx];
                            usable = usable_chunk[chunk_idx];

                            if (usable == 1) {
                                for (pnt = 1; pnt <= 2; pnt++) {
                                    if (listeqn[i] == 0) {
                                        // Fortran implicitly opens unit 26 even when listout=0.
                                        if (!fp26) fp26 = open_output("fort.26");
                                        fast_write_hapfound_line(fp26, animkey[i], pnt, steps,
                                            maxseg, keephap.get(), pnt, i, maxped, maxseg);
                                        for (seg = 1; seg <= maxseg; seg++) {
                                            if (keephap[KHIDX(pnt, i, seg, maxped)] > 0)
                                                hapfreq_arr[IDX2(keephap[KHIDX(pnt, i, seg, maxped)], seg, maxhap)]++;
                                        }
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
                            for (chrome = chrome1; chrome <= chromes; chrome++) {
                                for (pnt = 1; pnt <= 2; pnt++) {
                                    if (usable == 1) {
#if FINDHAP_INHERIT_RECORD_OUTPUT && FINDHAP_BUFFER_INHERIT
                                        char* inherit_end = inherit_payload.data();
                                        inherit_end = turbohap_output::append_integer(inherit_end, animkey[i], 10);
                                        inherit_end = turbohap_output::append_integer(inherit_end, chrome, 4);
                                        inherit_end = turbohap_output::append_integer(inherit_end, pnt, 2);
                                        *inherit_end++ = ' ';
                                        *inherit_end++ = ' ';
#else
                                        fprintf(fp28, "%10d%4d%2d  ", animkey[i], chrome, pnt);
#endif
#if FINDHAP_BUFFER_INHERIT
#if FINDHAP_INHERIT_RECORD_OUTPUT
                                        size_t inherit_bytes = static_cast<size_t>(inherit_end - inherit_payload.data());
#else
                                        size_t inherit_bytes = 0;
#endif
                                        for (seg = startseg[chrome - 1]; seg <= stopseg[chrome - 1]; seg++) {
                                            size_t base = PMIDX(i, seg, maxped) + (pnt - 1) * 2;
                                            inherit_payload[inherit_bytes++] = patmat[base];
                                            inherit_payload[inherit_bytes++] = patmat[base + 1];
                                        }
                                        inherit_payload[inherit_bytes++] = '\n';
                                        fwrite(inherit_payload.data(), 1, inherit_bytes, fp28);
#else
                                        for (seg = startseg[chrome - 1]; seg <= stopseg[chrome - 1]; seg++) {
                                            size_t base = PMIDX(i, seg, maxped) + (pnt - 1) * 2;
                                            fprintf(fp28, "%c%c", patmat[base], patmat[base + 1]);
                                        }
                                        fprintf(fp28, "\n");
#endif
                                    }
                                }
                            }

                            if (usable == 1) {
                                n31++;
                                if (n31 <= 10) {
                                    printf("%10d  ", animkey[i]);
                                    for (locus = 1; locus <= std::min(50, loci); locus++) {
                                        printf("%d", preview_chunk[chunk_idx * 50 + locus - 1]);
                                    }
                                    printf("\n");
                                }
                                if (genout > 0) {
                                    char* geno_ptr = &chunk_buffer_geno[chunk_idx * geno_record_bytes];
                                    geno_ptr[geno_record_bytes - 1] = '\n';
#if !FINDHAP_OUTPUT_BATCH_WRITE
                                    fwrite(geno_ptr, 1, geno_record_bytes, fp31);
#endif
                                }
                            }
                            if (hapout == 1 && usable == 1) {
                                char* hap_ptr = &chunk_buffer_hap[chunk_idx * hap_record_bytes];
                                hap_ptr[hap_record_bytes - 1] = '\n';
#if !FINDHAP_OUTPUT_BATCH_WRITE
                                fwrite(hap_ptr, 1, hap_record_bytes, fp29);
#endif
                            }
                        }
#if FINDHAP_OUTPUT_BATCH_WRITE
                        // The records are already contiguous. Coalesce only
                        // consecutive usable runs; never write an unfilled row.
                        for (int first = 0; first < chunk_count;) {
                            while (first < chunk_count && usable_chunk[first] != 1) ++first;
                            int last = first;
                            while (last < chunk_count && usable_chunk[last] == 1) ++last;
                            if (last > first) {
                                if (genout > 0) {
                                    const size_t bytes = size_t(last-first) * geno_record_bytes;
                                    if (fwrite(chunk_buffer_geno.get()+size_t(first)*geno_record_bytes, 1, bytes, fp31) != bytes)
                                        input_error("cannot write genotype output batch");
                                }
                                if (hapout == 1) {
                                    const size_t bytes = size_t(last-first) * hap_record_bytes;
                                    if (fwrite(chunk_buffer_hap.get()+size_t(first)*hap_record_bytes, 1, bytes, fp29) != bytes)
                                        input_error("cannot write haplotype output batch");
                                }
                            }
                            first = last;
                        }
#endif
                    } // end single writer
                    if (chunk_end == nped) break;
                    chunk_start = chunk_end + 1;
                } // end chunk loop
            } // end output team
#ifdef FINDHAP_LOW_MEMORY_PLUS
            if (output_cache_error) std::rethrow_exception(output_cache_error);
            genotype_cache->release_batch();
            printf(" Raw disk cache: %zu bytes, %llu read, %llu written\n", genotype_cache->bytes(),
                   (unsigned long long)genotype_cache->read_bytes(),
                   (unsigned long long)genotype_cache->write_bytes());
#endif

            printf(" %d genotype records rematched to output\n", n13a);
            if (n13a != n13) printf(" %d genotype records expected - MUST SORT BY ID\n", n13);
        } // end if step == steps

        // ---- Write haplotype list ----
        printf("\n");
        if (step == steps) printf("    hap   freq  haplotype\n");
        if (listout != 0) {
            if (step == 1) fprintf(fp25, "%31s%3d %s\n", "", steps, "steps");
            if (step == 1) fprintf(fp25,
                "  hap_freq  tot_freq    hap_# chro step seg   locus  lngth  haplotypes\n");
            if (step == 1) fprintf(fp26, "%10s%3d %s\n", "", steps, "steps");
            if (step == 1) fprintf(fp26, "  animal  pnt step segments haplotypes\n");
        }

        // Write hap.found
        FINDHAP_PHASE(library_output_trace, "library_output", step);
        FINDHAP_LIBRARY_CALIBRATE();
        FINDHAP_LIBRARY_ENABLE(true);
        FINDHAP_PHASE(library_found_trace, "library_found", step);
        for (i = 1; i <= nped; i++) {
            if (listeqn[i] > 0) {
                for (pnt = 1; pnt <= 2; pnt++) {
                    if (listout == 1 || (listout == -1 && step == steps))
                        fast_write_hapfound_line(fp26, animkey[i], pnt, step,
                            maxseg, keephap.get(), pnt, i, maxped, maxseg);
                    FINDHAP_LIBRARY_SAMPLE(count_sample, 2);
                    for (seg = 1; seg <= maxseg; seg++) {
                        if (keephap[KHIDX(pnt, i, seg, maxped)] > 0)
                            hapfreq_arr[IDX2(keephap[KHIDX(pnt, i, seg, maxped)], seg, maxhap)]++;
                    }
                }
            }
        }

        FINDHAP_END(library_found_trace);
        FINDHAP_PHASE(library_list_trace, "library_list", step);
        n25 = 0;
        for (seg = 1; seg <= maxseg; seg++) {
            totfreq = 0.0;
            haptot = 0;
            for (i = 1; i <= maxhap; i++) {
                haptot += std::max(hapfreq_arr[IDX2(i, seg, maxhap)], 0);
            }
            n = 0;
            for (i = 1; i <= maxhap; i++) {
                // Fortran rounds each REAL(4) fraction before adding to REAL(8) totfreq.
                totfreq += static_cast<double>(1.0f * hapfreq_arr[IDX2(order_arr[ORDIDX(i, seg, maxhap + 1)], seg, maxhap)] / haptot);
                if (hapfreq_arr[IDX2(order_arr[ORDIDX(i, seg, maxhap + 1)], seg, maxhap)] > 0) {
                    n++;
                    if (listout == 1 || (listout == -1 && step == steps)) {
                        fast_write_haplist_line(fp25,
                            100.0f * hapfreq_arr[IDX2(order_arr[ORDIDX(i, seg, maxhap + 1)], seg, maxhap)] / haptot,
                            100.0 * totfreq,
                            order_arr[ORDIDX(i, seg, maxhap + 1)], chro[seg - 1], step, seg,
                            startloc[seg], len_arr[seg],
                            &haplist_arr[IDX3(1, order_arr[ORDIDX(i, seg, maxhap + 1)], seg, maxlen, maxhap)],
                            len_arr[seg]);
                    }
                    n25++;
                    if (n <= 20 && seg == 1 && step == steps) {
                        printf("%7d%7d  ", i, hapfreq_arr[IDX2(order_arr[ORDIDX(i, seg, maxhap + 1)], seg, maxhap)]);
                        for (l = 1; l <= std::min(len_arr[seg], 100); l++) {
                            printf("%d", haplist_arr[IDX3(l, order_arr[ORDIDX(i, seg, maxhap + 1)], seg, maxlen, maxhap)]);
                        }
                        printf("\n");
                    }
                    maxnhap = std::max(order_arr[ORDIDX(i, seg, maxhap + 1)], maxnhap);
                }
            }
            nhap_arr[seg - 1] = n;
        }
        FINDHAP_END(library_list_trace);
        FINDHAP_LIBRARY_ENABLE(false);

        // End of step
        if (step < steps && listout != 0) {
            fprintf(fp25, "%10.5f%10.5f%10d%3d%3d%7d%8d%6d\n", 0.0, 0.0, 0, 0, step + 1, 0, 0, 0);
            fprintf(fp26, "%10d%2d%3d%6d\n", 0, 0, step + 1, 0);
        }
    } // end step loop
    FINDHAP_LIBRARY_SAVE();

    // -------------------------------------------------------- Genotype frequency
    printf("\n");
    printf(" Frequency of codes in genotypes.filled\n");
    for (i = 0; i <= 9; i++) {
        if (genfreq[i] > 0)
            printf("%15ld genotypes with code %2d%12.5f%%\n",
                   (long)genfreq[i], i, 100.0 * genfreq[i] / n31 / loci);
    }
    if (genout == 5) printf(" Codes 3 and 4 written as code 5 with option genout = 5\n");
    if (genout == 2) printf(" Codes 3, 4, 5 written as 0, 1, 2 by best call with option genout = 5\n");
    printf("\n");
    printf(" %d maximum haplotype number within segment\n", maxnhap);
    printf("\n");
    printf("          Starting marker within each segment\n");
    segs = 1 + (locN[0] - loc1[0]) / maxlen;
    printf("   ");
    for (i = 1; i <= segs; i++) printf("%7d", i);
    printf("\n");
    printf(" Chrome\n");
    seg = 1;
    for (chrome = chrome1; chrome <= chromes; chrome++) {
        segs = 1 + (locN[chrome - 1] - loc1[chrome - 1]) / maxlen;
        printf("%3d", chrome);
        for (i = seg; i <= seg + segs - 1; i++) printf("%7d", startloc[i]);
        printf("\n");
        seg += segs;
    }
    printf("\n");
    printf("          Number of haplotypes found within each segment\n");
    segs = 1 + (locN[0] - loc1[0]) / maxlen;
    printf("   ");
    for (i = 1; i <= segs; i++) printf("%7d", i);
    printf("\n");
    printf(" Chrome\n");
    seg = 1;
    for (chrome = chrome1; chrome <= chromes; chrome++) {
        segs = 1 + (locN[chrome - 1] - loc1[chrome - 1]) / maxlen;
        printf("%3d", chrome);
        for (i = seg; i <= seg + segs - 1; i++) printf("%7d", nhap_arr[i - 1]);
        printf("\n");
        seg += segs;
    }
    printf("\n");
    if (genout > 0) printf(" %d records written to genotypes.filled\n", n31);
    if (damout == 2) nadd -= nmale;
    if (damout >= 1) printf(" %d female ancestor genotypes imputed from progeny\n", nadd);
    if (damout == 2) printf(" %d male ancestor genotypes imputed from progeny\n", nmale);
    if (nmale > 0) printf(" %d male ancestor genotypes that could be imputed\n", nmale);
    printf(" %d genotyped animals with poor imputation not distributed\n", ndrop);
    if (nadd == 0) nadd = 1;

    // -------------------------------------------------------- Allele frequency output
    fprintf(fp30, "           allele_frequency_estimates    missing_genotype_frequency\n");
    fprintf(fp30, "    locus   simple     base  current    input   output  imputed      all\n");
    for (locus = 1; locus <= loci; locus++) {
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
        simplfreq = 0.0f;
        if (n13 > 0) simplfreq = 1.0f - 1.0f * ofreq[locus - 1] / n13;
        fprintf(fp30, "%9d%9.6f%9.6f%9.6f%9.6f%9.6f%9.6f%9.6f\n",
                locus, freq[locus - 1], basefreq[locus - 1], curfreq[locus - 1],
                simplfreq,
                1.0f * (missall[locus - 1] - missadd[locus - 1]) / std::max(n31 - nadd, 1),
                1.0f * missadd[locus - 1] / nadd,
                1.0f * missall[locus - 1] / std::max(n31, 1));
    }
    printf("\n");
    // Close files
    fclose(fp10); fclose(fp13); fclose(fp14);
    if (fp15) fclose(fp15); if (fp16) fclose(fp16);
    FINDHAP_PHASE(close_trace, "output_close");
    close_outputs();
    FINDHAP_END(close_trace);
    printf(" Program findhap completed normally\n");

    // Clean up I/O buffers
    delete[] read_buf;
    delete[] haplist_buf_io;
    delete[] hapfound_buf_io;

    return 0;
}

int FINDHAP_ENTRY_POINT(int argc, char* argv[]) {
    printf(" Output from findhap.cpp (C++ port)\n");
    printf("\n");
    if (!std::filesystem::exists("findhap.options")) {
        printf(" file findhap.options is missing\n"); exit(8);
    }
    if (!std::filesystem::exists("pedigree.file")) {
        printf(" file pedigree.file is missing\n"); exit(10);
    }
    if (!std::filesystem::exists("genotypes.txt")) {
        printf(" file genotypes.txt is missing\n"); exit(13);
    }
    if (!std::filesystem::exists("chromosome.data")) {
        printf(" file chromosome.data is missing\n"); exit(14);
    }
    findhap::Options options;
    try {
        std::ifstream options_file("findhap.options");
        options = findhap::read_options(options_file);
    } catch (const std::exception& error) { input_error(error.what()); }

    // Select once from the user's unchanged capacity limit. No width branch
    // is needed in the hot loops, and larger libraries retain full-width IDs.
    if (options.controls[5] <= std::numeric_limits<uint16_t>::max())
        return run_findhap<uint16_t>(argc, argv, options);
    return run_findhap<int>(argc, argv, options);
}

#ifdef FINDHAP_ENGINE_NAMESPACE
} // namespace FINDHAP_ENGINE_NAMESPACE
#endif
