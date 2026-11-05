#include "../src/input_validation.hpp"

#include <cassert>
#include <memory>
#include <string>
#include <vector>
#include <unistd.h>

using findhap::input::Policy;
using findhap::input::Validator;

struct CloseFile { void operator()(FILE* file) const { if (file) std::fclose(file); } };
using File = std::unique_ptr<FILE, CloseFile>;

static File input(const std::string& bytes) {
    File file(std::tmpfile());
    assert(file);
    assert(std::fwrite(bytes.data(), 1, bytes.size(), file.get()) == bytes.size());
    std::rewind(file.get());
    return file;
}

static std::string row(const std::string& payload, int id = 1, int chip = 1,
                       int length = -1, const std::string& newline = "\n") {
    char fields[64];
    std::snprintf(fields, sizeof(fields), "%10d%10d%10d ", id, chip,
                  length < 0 ? int(payload.size()) : length);
    return fields + payload + newline;
}

static void rejects(const std::string& bytes, const std::string& expected,
                    Policy selected = Policy::strict) {
    auto file = input(bytes);
    int lengths[] = {6, 3};
    Validator reader(file.get(), 6, lengths, 2, selected, false);
    int id, chip, loc;
    int8_t decoded[6];
    try {
        while (reader.read(id, chip, loc, decoded)) {}
        assert(!"invalid input was accepted");
    } catch (const std::runtime_error& error) {
        const std::string message = error.what();
        assert(message.find("genotypes.txt line ") != std::string::npos);
        if (message.find(expected) == std::string::npos)
            throw std::runtime_error("Expected diagnostic '" + expected + "', received '" + message + "'");
        assert(message.find(expected) != std::string::npos);
    }
}

int main() {
    assert(findhap::input::policy == Policy::strict);
    assert(findhap::input::parse_policy("strict") == Policy::strict);
    assert(findhap::input::parse_policy("warn") == Policy::warn);
    try {
        (void)findhap::input::parse_policy("off");
        assert(!"unsupported policy accepted");
    } catch (const std::invalid_argument& error) {
        assert(std::string(error.what()).find("--input-validation") != std::string::npos);
    }

    // Multiple chips, explicit partial calls, CRLF, harmless trailing spaces
    // and the last complete record without a newline all preserve each value.
    for (Policy selected : {Policy::strict, Policy::warn}) {
        auto file = input(row("012345", 17) + row("502", 22, 2, 3, "\r\n") +
                          row("543210   ", 31, 1, 6, ""));
        int lengths[] = {6, 3};
        Validator reader(file.get(), 6, lengths, 2, selected, false);
        int id, chip, loc;
        int8_t decoded[6];
        assert(reader.read(id, chip, loc, decoded));
        assert(id == 17 && chip == 1 && loc == 6);
        for (int i = 0; i < 6; ++i) assert(decoded[i] == i);
        assert(reader.read(id, chip, loc, decoded));
        assert(id == 22 && chip == 2 && loc == 3);
        assert(decoded[0] == 5 && decoded[1] == 0 && decoded[2] == 2);
        assert(reader.read(id, chip, loc, decoded));
        assert(id == 31 && chip == 1 && loc == 6);
        for (int i = 0; i < 6; ++i) assert(decoded[i] == 5-i);
        assert(!reader.read(id, chip, loc, decoded));
        reader.finish();
        assert(reader.warning_records() == 0);
    }

    // Left/right ASCII padding and an optional leading plus are unambiguous.
    {
        auto file = input("+12       " "    +1    " "6         " " 012345\n");
        int lengths[] = {6};
        Validator reader(file.get(), 6, lengths, 1, Policy::strict, false);
        int id, chip, loc; int8_t decoded[6];
        assert(reader.read(id, chip, loc, decoded));
        assert(id == 12 && chip == 1 && loc == 6 && decoded[5] == 5);
    }

    // Recoverable warn-mode differences have exactly the legacy interpretation.
    {
        auto file = input(row(" 012345", 1, 1, 6) + row("0 23 5") +
                          row("01234505", 3, 1, 6));
        int lengths[] = {6};
        Validator reader(file.get(), 6, lengths, 1, Policy::warn, false);
        int id, chip, loc; int8_t decoded[6];
        assert(reader.read(id, chip, loc, decoded));
        const int8_t shifted[] = {0, 0, 1, 2, 3, 4};
        assert(std::equal(decoded, decoded + 6, shifted));
        assert(reader.read(id, chip, loc, decoded));
        const int8_t blanks[] = {0, 0, 2, 3, 0, 5};
        assert(std::equal(decoded, decoded + 6, blanks));
        assert(reader.read(id, chip, loc, decoded));
        for (int i = 0; i < 6; ++i) assert(decoded[i] == i);
        assert(!reader.read(id, chip, loc, decoded));
        assert(reader.warning_records() == 3);
        assert(reader.blank_fields() == 3);
        assert(reader.ignored_nonspace_bytes() == 3);
    }
    rejects(row(" 012345", 1, 1, 6), "column 32: blank first genotype field");
    rejects(row(" 000000", 1, 1, 6), "likely an extra separator");
    rejects(row("01 345"), "blank inside");
    rejects(row("0123452", 1, 1, 6), "extra genotype digits");

    // Unsafe cases always fail, even when the explicit legacy warning mode
    // was requested. No row may be decoded out of bounds or silently padded.
    std::vector<std::pair<std::string, std::string>> unsafe = {
        {"", "line 1, column 1: empty genotype file"},
        {"\n", "blank record"},
        {" \r\n", "truncated header"},
        {std::string(30, ' ') + "\n", "truncated header"},
        {row("01234", 1, 1, 6), "truncated genotype payload"},
        {row("012345", 1, 0), "chip number must be positive"},
        {row("012345", 1, 3), "chip number is not present"},
        {row("012345", 0), "animal identifier must be positive"},
        {row("012345", -1), "invalid animal identifier"},
        {row("012345", 1, 1, 0), "locus count must be positive"},
        {row("0123450", 1, 1, 7), "exceeds the maximum chip length"},
        {row("012345", 1, 2), "does not match chip"},
        {row("012\t45"), "invalid genotype byte"},
        {row("012\r45"), "invalid genotype byte"},
        {row("012945"), "invalid genotype byte"},
        {row(std::string("012\0" "45", 6)), "embedded NUL"},
        {row("012345x", 1, 1, 6), "invalid trailing byte"},
        {row("012345\t", 1, 1, 6), "invalid trailing byte"},
        {row(std::string("012345\0", 7), 1, 1, 6), "embedded NUL"},
        {row("012345", 1, 1, 6, "\r"), "invalid trailing byte"},
    };
    auto edited = row("012345");
    edited[30] = '\t'; unsafe.emplace_back(edited, "column 31: expected exactly one");
    edited = row("012345");
    edited.replace(0, 10, "2147483648"); unsafe.emplace_back(edited, "integer range");
    edited = row("012345");
    edited.replace(10, 10, "          "); unsafe.emplace_back(edited, "empty chip number");
    edited = row("012345");
    edited.replace(0, 10, "1 2       "); unsafe.emplace_back(edited, "invalid animal identifier");
    edited = row("012345");
    edited.replace(0, 10, " 1e2      "); unsafe.emplace_back(edited, "invalid animal identifier");
    edited = row("012345");
    edited.replace(0, 10, "       + 1"); unsafe.emplace_back(edited, "invalid animal identifier");
    edited = row("012345");
    edited[0] = '\0'; unsafe.emplace_back(edited, "embedded NUL");
    for (const auto& [bytes, message] : unsafe)
        for (Policy selected : {Policy::strict, Policy::warn}) rejects(bytes, message, selected);
    rejects(row("012345") + row(" 012345", 2, 1, 6), "line 2, column 32");
    rejects(row(std::string(70000, '0'), 1, 1, 6), "bounded input line limit", Policy::warn);

    // A stream error is not interpreted as clean EOF.
    {
        File file(std::fopen("/dev/null", "r")); assert(file);
        assert(std::fputc('x', file.get()) == EOF && std::ferror(file.get()));
        int length = 6, id, chip, loc; int8_t decoded[6];
        Validator reader(file.get(), 6, &length, 1, Policy::warn, false);
        try {
            (void)reader.read(id, chip, loc, decoded);
            assert(!"read error accepted as EOF");
        } catch (const std::runtime_error& error) {
            assert(std::string(error.what()).find("failed reading genotype input") != std::string::npos);
        }
    }

    // Record boundaries may occur anywhere in the 64-KiB input chunks,
    // including between CR and LF; the last record may end at EOF.
    for (int length : {65503, 65504, 65505, 100000}) {
        std::string values(size_t(length), '0');
        for (int i = 0; i < length; ++i) values[size_t(i)] = char('0' + i % 6);
        auto file = input(row(values, 1, 1, length, "\r\n") + row(values, 2, 1, length, ""));
        Validator reader(file.get(), length, &length, 1, Policy::strict, false);
        int id, chip, loc; std::vector<int8_t> decoded(size_t(length), -1);
        for (int record = 1; record <= 2; ++record) {
            assert(reader.read(id, chip, loc, decoded.data()));
            assert(id == record && loc == length);
            for (int i = 0; i < length; ++i) assert(decoded[size_t(i)] == i % 6);
        }
        assert(!reader.read(id, chip, loc, decoded.data()));
    }

    // Warnings are bounded, have a summary, and finish() cannot duplicate it.
    {
        std::string bytes;
        for (int i = 1; i <= 12; ++i) bytes += row(" 012345", i, 1, 6);
        auto file = input(bytes);
        File log(std::tmpfile()); assert(log);
        std::fflush(stderr);
        const int saved_stderr = dup(fileno(stderr)); assert(saved_stderr >= 0);
        assert(dup2(fileno(log.get()), fileno(stderr)) >= 0);
        int length = 6;
        Validator reader(file.get(), 6, &length, 1, Policy::warn);
        int id, chip, loc; int8_t decoded[6];
        while (reader.read(id, chip, loc, decoded)) {}
        reader.finish(); reader.finish();
        std::fflush(stderr);
        assert(dup2(saved_stderr, fileno(stderr)) >= 0); close(saved_stderr);
        std::rewind(log.get());
        char buffer[1024]; std::string text;
        while (size_t count = std::fread(buffer, 1, sizeof(buffer), log.get())) text.append(buffer, count);
        assert(std::count(text.begin(), text.end(), '\n') == 6);
        assert(text.find("12 genotype records") != std::string::npos);
        assert(text.find("12 blank fields interpreted as 0") != std::string::npos);
        assert(text.find("12 extra genotype digits ignored") != std::string::npos);
    }
    std::puts("Input validation: fixed-width layout, chip mapping, safe warn compatibility, line boundaries and bounded diagnostics passed");
}
