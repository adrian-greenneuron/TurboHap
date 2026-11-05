#pragma once




#include "genotype_text.hpp"
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>

namespace findhap::input {

enum class Policy { strict, warn };
inline Policy policy = Policy::strict;

inline Policy parse_policy(const std::string& text) {
    if (text == "strict") return Policy::strict;
    if (text == "warn") return Policy::warn;
    throw std::invalid_argument("--input-validation must be 'strict' or 'warn'");
}

// A bounded, record-aware reader for the documented 3i10,1x,<loc>i1 layout.
// The existing count pass and cache-loading pass use the same validator. Warn
// is an explicit compatibility mode, not an automatic repair: blanks remain
// zero and safe trailing fields remain ignored exactly as by the legacy read.
class Validator {
public:
    Validator(FILE* file, int max_loci, const int* chip_lengths, int chips,
              Policy selected, bool emit_warnings = true)
        : file_(file), max_loci_(max_loci), chip_lengths_(chip_lengths),
          chips_(chips), selected_(selected), emit_warnings_(emit_warnings),
          record_limit_(max_loci > 0 ? size_t(max_loci) * 2 + 65536 : 0) {
        if (!file_ || max_loci_ <= 0 || !chip_lengths_ || chips_ <= 0)
            throw std::invalid_argument("invalid genotype validator configuration");
        record_.reserve(std::min(size_t(max_loci_) + 33, size_t(65536)));
    }

    bool read(int& id, int& chip, int& loc, int8_t* decoded) {
        if (!read_record()) {
            if (!line_) {
                line_ = 1;
                fail(1, "empty genotype file; expected at least one genotype record");
            }
            return false;
        }
        ++line_;
        if (record_.empty()) fail(1, "blank record; expected three 10-byte header fields and a genotype payload");
        // NULs must not truncate C-string based diagnostics or hide a tail.
        const size_t nul = record_.find('\0');
        if (nul != std::string::npos) fail(nul + 1, "embedded NUL byte is not allowed");
        if (record_.size() < 31) fail(record_.size() + 1, "truncated header; expected three 10-byte integer fields followed by one space");
        id = header(0, "animal identifier");
        chip = header(10, "chip number");
        loc = header(20, "locus count");
        if (record_[30] != ' ') fail(31, "expected exactly one ASCII separator space after the three header fields");
        if (chip > chips_) fail(11, "chip number is not present in chromosome.data");
        if (loc > max_loci_) fail(21, "declared locus count exceeds the maximum chip length in chromosome.data");
        if (loc != chip_lengths_[chip - 1])
            fail(21, "declared locus count " + std::to_string(loc) +
                 " does not match chip " + std::to_string(chip) +
                 " length " + std::to_string(chip_lengths_[chip - 1]) +
                 " in chromosome.data");
        const size_t expected = 31 + size_t(loc);
        if (record_.size() < expected)
            fail(record_.size() + 1, "truncated genotype payload: expected " +
                 std::to_string(loc) + " genotype fields, found " +
                 std::to_string(record_.size() - 31));

        // Keep the ordinary path a vectorizable range reduction. Locating and
        // describing suspect fields happens only for a non-canonical record.
        unsigned suspect = 0;
        for (size_t offset = 31; offset < expected; ++offset)
            suspect |= (unsigned(static_cast<unsigned char>(record_[offset])) - unsigned('0')) > 5u;
        size_t blanks = 0, first_blank = 0;
        if (suspect) {
            for (size_t offset = 31; offset < expected; ++offset) {
                const unsigned char value = static_cast<unsigned char>(record_[offset]);
                if (value == ' ') {
                    if (!blanks) first_blank = offset;
                    ++blanks;
                } else if (value < '0' || value > '5') {
                    fail(offset + 1, "invalid genotype byte; supported genotype codes are ASCII digits 0 through 5; tabs and other whitespace are not genotype codes");
                }
            }
        }
        size_t extra = 0, first_extra = 0;
        for (size_t offset = expected; offset < record_.size(); ++offset) {
            const unsigned char value = static_cast<unsigned char>(record_[offset]);
            if (value == ' ') continue; // Harmless padding after complete fields.
            if (value < '0' || value > '5')
                fail(offset + 1, "invalid trailing byte after the declared genotype payload; only ASCII-space padding is accepted");
            if (!extra) first_extra = offset;
            ++extra;
        }
        if (selected_ == Policy::strict) {
            if (blanks) fail(first_blank + 1, blank_message(first_blank));
            if (extra) fail(first_extra + 1, "extra genotype digits after the declared payload; marker alignment is ambiguous and trailing digits would be silently ignored by a legacy reader");
        } else if (blanks || extra) {
            ++warning_records_;
            blank_fields_ += blanks;
            ignored_nonspace_bytes_ += extra;
            if (emit_warnings_ && warning_records_ <= warning_limit_) {
                const std::string message = blanks ? blank_message(first_blank) :
                    "extra genotype digits after the declared payload";
                std::fprintf(stderr,
                    "Input warning: genotypes.txt line %zu, column %zu: %s. "
                    "Explicit warn mode preserves legacy interpretation: %zu blank fields become 0; "
                    "%zu extra genotype digits are ignored. Input is not repaired.\n",
                    line_, (blanks ? first_blank : first_extra) + 1,
                    message.c_str(), blanks, extra);
            }
        }
        if (!decoded) fail(1, "missing genotype decode destination");
        findhap_text::decode(record_.data() + 31, decoded, loc);
        return true;
    }

    void finish() {
        if (finished_) return;
        finished_ = true;
        if (emit_warnings_ && warning_records_)
            std::fprintf(stderr,
                "Input validation warning summary: %zu genotype records contain ambiguous formatting; "
                "%zu blank fields interpreted as 0, %zu extra genotype digits ignored. "
                "Displayed at most %zu record warnings. Warn mode does not correct marker alignment.\n",
                warning_records_, blank_fields_, ignored_nonspace_bytes_, warning_limit_);
    }

    size_t warning_records() const { return warning_records_; }
    size_t blank_fields() const { return blank_fields_; }
    size_t ignored_nonspace_bytes() const { return ignored_nonspace_bytes_; }

private:
    [[noreturn]] void fail(size_t column, const std::string& message) const {
        throw std::runtime_error("genotypes.txt line " + std::to_string(line_) +
                                 ", column " + std::to_string(column) + ": " + message);
    }

    static std::string blank_message(size_t offset) {
        if (offset == 31)
            return "blank first genotype field: likely an extra separator space; expected exactly one space in column 31 and the first genotype digit in column 32; this risks shifting every marker assignment";
        return "blank inside the declared genotype payload; use an explicit genotype code (5 for missing), not a blank that a legacy reader interprets as 0";
    }

    int header(size_t offset, const char* name) const {
        size_t start = offset, end = offset + 10;
        while (start < end && record_[start] == ' ') ++start;
        while (end > start && record_[end - 1] == ' ') --end;
        if (start == end) fail(offset + 1, std::string("empty ") + name + " header field");
        // A leading plus is a valid Fortran integer; all other signs, embedded
        // padding and non-digits are rejected rather than silently discarded.
        if (record_[start] == '+') ++start;
        if (start == end) fail(offset + 1, std::string("invalid ") + name + " header field");
        int result = 0;
        for (size_t position = start; position < end; ++position) {
            const unsigned char value = static_cast<unsigned char>(record_[position]);
            if (value < '0' || value > '9')
                fail(position + 1, std::string("invalid ") + name + " header field; expected a positive integer with ASCII-space padding only");
            const int digit = value - '0';
            if (result > (std::numeric_limits<int>::max() - digit) / 10)
                fail(position + 1, std::string(name) + " exceeds the supported integer range");
            result = result * 10 + digit;
        }
        if (result <= 0) fail(offset + 1, std::string(name) + " must be positive");
        return result;
    }

    bool read_record() {
        record_.clear();
        bool newline = false;
        while (true) {
            if (begin_ == end_) {
                end_ = std::fread(buffer_.data(), 1, buffer_.size(), file_);
                begin_ = 0;
                if (std::ferror(file_)) {
                    ++line_;
                    fail(1, "failed reading genotype input");
                }
                if (!end_) break;
            }
            const char* start = buffer_.data() + begin_;
            const auto* found = static_cast<const char*>(std::memchr(start, '\n', end_ - begin_));
            const size_t count = found ? size_t(found - start) : end_ - begin_;
            if (count > record_limit_ - record_.size()) {
                ++line_;
                fail(record_limit_ + 1, "record exceeds the bounded input line limit; check header fields, separator and missing line breaks");
            }
            record_.append(start, count);
            begin_ += count;
            if (found) { ++begin_; newline = true; break; }
        }
        if (newline && !record_.empty() && record_.back() == '\r') record_.pop_back();
        return newline || !record_.empty();
    }

    FILE* file_;
    int max_loci_;
    const int* chip_lengths_;
    int chips_;
    Policy selected_;
    bool emit_warnings_;
    size_t record_limit_;
    std::array<char, 65536> buffer_{};
    size_t begin_ = 0, end_ = 0, line_ = 0;
    std::string record_;
    size_t warning_records_ = 0, blank_fields_ = 0, ignored_nonspace_bytes_ = 0;
    static constexpr size_t warning_limit_ = 5;
    bool finished_ = false;
};

} // namespace findhap::input

// Production validator.
