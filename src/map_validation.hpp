#pragma once

#include <charconv>
#include <climits>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace findhap::input {

struct MapRecord {
    std::string marker;
    int chromosome = 0;
    int within = 0;
    int overall = 0;
    int position = 0;
    int chips = 0;
    std::vector<int> chip_loci;
};

// Non-owning reader for chromosome.data. Each physical record is bounded at
// 1 MiB (excluding LF); malformed rows cannot consume fields from the next row.
// These are structural/index-safety requirements, also in diagnostic warn mode.
class MapReader {
public:
    static constexpr std::size_t max_record_bytes = 1024 * 1024;

    explicit MapReader(FILE* stream) : stream_(stream) {
        if (!stream_) fail("file", "cannot read a null file handle");
        if (!physical_record()) fail("header", "missing header and marker records");
        // FindHap discards the first record regardless of its contents; some
        // established exports use an empty header. Blank padding is permitted
        // at EOF, but never between marker records.
    }

    bool read(MapRecord& result) {
        if (!physical_record()) {
            if (records_ == 0) fail("marker record", "no markers follow the header");
            return false;
        }
        const auto tokens = fields();
        if (tokens.empty()) {
            const std::size_t first_blank = line_;
            while (physical_record()) {
                if (!fields().empty())
                    fail("marker record", "blank records are permitted only at the end of the file; another marker follows",
                         1, first_blank);
            }
            if (!records_)
                fail("marker record", "blank records contain no markers after the header", 1, first_blank);
            return false;
        }
        static constexpr const char* names[] = {
            "marker", "chromosome", "within", "overall", "position", "n_chips"
        };
        if (tokens.size() < 6)
            fail(names[tokens.size()], "truncated marker record; all fields must occur on this line");

        MapRecord row;
        if (tokens[0].size() > 50)
            fail("marker", "marker names must contain at most 50 bytes");
        row.marker.assign(tokens[0]);
        row.chromosome = integer(tokens[1], "chromosome");
        row.within = integer(tokens[2], "within");
        row.overall = integer(tokens[3], "overall");
        row.position = integer(tokens[4], "position");
        row.chips = integer(tokens[5], "n_chips");
        if (row.chromosome < 1) fail("chromosome", "expected a positive chromosome number");
        if (row.chromosome == INT_MAX)
            fail("chromosome", "chromosome number must leave room for the engine's terminal index (at most INT_MAX - 1)");
        if (row.within < 1) fail("within", "expected a positive within-chromosome marker number");
        if (row.overall < 1) fail("overall", "expected a positive overall marker number");
        if (row.position < 0) fail("position", "expected a nonnegative position");
        if (row.chips < 1) fail("n_chips", "expected a positive chip count");
        if (records_ && row.chips != chips_)
            fail("n_chips", "chip count differs from earlier records (expected " + std::to_string(chips_) + ")");
        const std::size_t expected_fields = 6 + static_cast<std::size_t>(row.chips);
        if (tokens.size() < expected_fields)
            fail("chip columns", "expected " + std::to_string(row.chips) +
                 " chip ordinals on this line; found " + std::to_string(tokens.size() - 6), text_.size() + 1);
        if (tokens.size() > expected_fields)
            fail("chip columns", "extra fields after the declared " + std::to_string(row.chips) + " chip ordinals",
                 static_cast<std::size_t>(tokens[expected_fields].data() - text_.data()) + 1);
        if (records_ == INT_MAX - 1)
            fail("overall", "marker count must leave room for the engine's terminal index (at most INT_MAX - 1)");
        if (row.overall != records_ + 1)
            fail("overall", "marker numbers must be consecutive from 1 (expected " +
                 std::to_string(static_cast<long long>(records_) + 1) + ")");
        if (records_ && row.chromosome < last_chromosome_)
            fail("chromosome", "chromosomes must be in nondecreasing order");
        const bool same_chromosome = records_ && row.chromosome == last_chromosome_;
        const long long expected_within = same_chromosome ? static_cast<long long>(last_within_) + 1 : 1;
        if (row.within != expected_within)
            fail("within", "marker numbers must be consecutive from 1 on each chromosome (expected " +
                 std::to_string(expected_within) + ")");
        if (same_chromosome && row.position < last_position_)
            fail("position", "positions must be nondecreasing within each chromosome");

        // Only allocate after the complete physical row proves the chip count.
        row.chip_loci.reserve(static_cast<std::size_t>(row.chips));
        for (int chip = 0; chip < row.chips; ++chip) {
            const std::string field = "chip " + std::to_string(chip + 1);
            const int ordinal = integer(tokens[6 + static_cast<std::size_t>(chip)], field);
            const long long next = records_ ? static_cast<long long>(chip_counts_[chip]) + 1 : 1;
            if (ordinal < 0 || (ordinal != 0 && ordinal != next))
                fail(field, "expected 0 for an absent marker or the next chip ordinal " + std::to_string(next));
            row.chip_loci.push_back(ordinal);
        }

        // No caller can observe partially validated indexing values or counts.
        if (!records_) {
            chips_ = row.chips;
            chip_counts_.assign(static_cast<std::size_t>(chips_), 0);
        }
        for (int chip = 0; chip < chips_; ++chip)
            if (row.chip_loci[chip]) ++chip_counts_[chip];
        ++records_;
        last_chromosome_ = row.chromosome;
        last_within_ = row.within;
        last_position_ = row.position;
        result = std::move(row);
        return true;
    }

    std::size_t line_number() const noexcept { return line_; }
    int record_count() const noexcept { return records_; }
    int chip_count() const noexcept { return chips_; }
    // Chip 1 is index 0; counts are final after read() returns false.
    const std::vector<int>& chip_counts() const noexcept { return chip_counts_; }

private:
    FILE* stream_;
    std::string text_;
    std::size_t line_ = 0;
    int records_ = 0, chips_ = 0;
    int last_chromosome_ = 0, last_within_ = 0, last_position_ = 0;
    std::vector<int> chip_counts_;

    [[noreturn]] void fail(const std::string& field, const std::string& message,
                          std::size_t column = 0, std::size_t line = 0) const {
        if (!column) {
            // Resolve a byte column only on failure, keeping successful parsing
            // free from additional token scans or diagnostic allocations.
            int index = -1;
            static constexpr const char* names[] = {
                "marker", "chromosome", "within", "overall", "position", "n_chips"
            };
            for (int i = 0; i < 6; ++i) if (field == names[i]) index = i;
            if (field.compare(0, 5, "chip ") == 0) {
                int number = 0;
                const auto parsed = std::from_chars(field.data() + 5, field.data() + field.size(), number);
                if (parsed.ec == std::errc{} && parsed.ptr == field.data() + field.size() && number > 0)
                    index = 5 + number;
            }
            if (index >= 0) {
                const auto tokens = fields();
                column = static_cast<std::size_t>(index) < tokens.size()
                    ? static_cast<std::size_t>(tokens[index].data() - text_.data()) + 1 : text_.size() + 1;
            } else {
                column = 1;
            }
        }
        if (!line) line = line_ ? line_ : 1;
        throw std::runtime_error("chromosome.data line " + std::to_string(line) +
                                 ", column " + std::to_string(column) + ", " + field + ": " + message);
    }

    bool physical_record() {
        text_.clear();
        int ch = std::fgetc(stream_);
        if (ch == EOF) {
            if (std::ferror(stream_)) fail("file", "I/O error while reading marker records");
            return false;
        }
        ++line_;
        do {
            if (ch == '\n') return true;
            if (ch == 0) fail("record", "embedded NUL byte; expected a plain-text marker record", text_.size() + 1);
            if (text_.size() == max_record_bytes)
                fail("record", "record exceeds the 1 MiB safety limit", text_.size() + 1);
            text_.push_back(static_cast<char>(ch));
            ch = std::fgetc(stream_);
        } while (ch != EOF);
        if (std::ferror(stream_)) fail("file", "I/O error while reading marker records");
        return true;
    }

    static bool space(char ch) noexcept {
        return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\v' || ch == '\f';
    }

    std::vector<std::string_view> fields() const {
        std::vector<std::string_view> result;
        std::size_t start = 0;
        while (start < text_.size()) {
            while (start < text_.size() && space(text_[start])) ++start;
            if (start == text_.size()) break;
            std::size_t end = start;
            while (end < text_.size() && !space(text_[end])) ++end;
            result.emplace_back(text_.data() + start, end - start);
            start = end;
        }
        return result;
    }

    int integer(std::string_view token, const std::string& field) const {
        // Like fscanf's decimal %d, accept a leading plus; reject suffixes and
        // overflow instead of allowing silent truncation or undefined indices.
        if (!token.empty() && token.front() == '+') {
            token.remove_prefix(1);
            if (!token.empty() && (token.front() < '0' || token.front() > '9'))
                fail(field, "expected a complete decimal integer in the signed 32-bit range");
        }
        int value = 0;
        const auto parsed = std::from_chars(token.data(), token.data() + token.size(), value);
        if (token.empty() || parsed.ec != std::errc{} || parsed.ptr != token.data() + token.size())
            fail(field, "expected a complete decimal integer in the signed 32-bit range");
        return value;
    }
};

} // namespace findhap::input
