#pragma once
#include <cerrno>
#include <climits>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <string>

namespace findhap {
// Complete physical records; trailing metadata is intentionally not interpreted.
// The same reader is used by the count and load passes, before outputs are opened.
class PedigreeReader {
    FILE* file_;
    char* buffer_ = nullptr;
    size_t capacity_ = 0, line_ = 0;
    [[noreturn]] void fail(const char* message) const {
        throw std::runtime_error("pedigree.file:" + std::to_string(line_) + ": " + message);
    }
    static void spaces(char*& p) {
        while (*p && std::isspace(static_cast<unsigned char>(*p))) ++p;
    }
    int integer(char*& p) const {
        spaces(p);
        char* end = nullptr;
        errno = 0;
        const long value = std::strtol(p, &end, 10);
        if (end == p || errno == ERANGE || value < INT_MIN || value > INT_MAX ||
            (*end && !std::isspace(static_cast<unsigned char>(*end))))
            fail("expected five fields: sex, animal ID, sire ID, dam ID, birth date (32-bit integers)");
        p = end;
        return static_cast<int>(value);
    }
public:
    struct Record { char sex; int id, sire, dam, birth; };
    explicit PedigreeReader(FILE* file) : file_(file) {}
    ~PedigreeReader() { std::free(buffer_); }
    PedigreeReader(const PedigreeReader&) = delete;
    PedigreeReader& operator=(const PedigreeReader&) = delete;
    // Keep parsing out of the large inference function: inlining cold setup
    // here can perturb code generation for the unrelated matching loops.
    [[gnu::noinline]] bool read(Record& record) {
        for (;;) {
            errno = 0;
            const ssize_t length = ::getline(&buffer_, &capacity_, file_);
            if (length < 0) {
                if (!std::feof(file_) || std::ferror(file_)) fail("cannot read pedigree record");
                return false;
            }
            ++line_;
            if (std::memchr(buffer_, '\0', static_cast<size_t>(length))) fail("embedded NUL byte");
            char* p = buffer_;
            spaces(p);
            if (!*p) continue;
            record.sex = *p++;
            if (!*p || !std::isspace(static_cast<unsigned char>(*p))) fail("expected a one-character sex field");
            record.id = integer(p);
            record.sire = integer(p);
            record.dam = integer(p);
            record.birth = integer(p);
            if (record.id <= 0) fail("animal ID must be positive");
            return true;
        }
    }
};
}
