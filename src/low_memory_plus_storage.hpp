#pragma once

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>
#include <fcntl.h>
#include <unistd.h>

namespace low_memory_plus {

inline size_t product(size_t a, size_t b) {
    if (b && a > std::numeric_limits<size_t>::max() / b)
        throw std::length_error("TurboHap LM storage is too large");
    return a * b;
}

inline void pack(uint8_t* out, const int8_t* in, size_t length) {
    auto code = [](int8_t value) -> uint8_t { return value == 5 ? 3 : value; };
    size_t l = 0;
    for (; length - l >= 4; l += 4)
        out[l / 4] = code(in[l]) | (code(in[l + 1]) << 2) |
                     (code(in[l + 2]) << 4) | (code(in[l + 3]) << 6);
    if (l < length) {
        uint8_t tail = 0;
        const size_t byte = l / 4;
        for (int shift = 0; l < length; ++l, shift += 2)
            tail |= code(in[l]) << shift;
        out[byte] = tail;
    }
}

inline void unpack(int8_t* out, const uint8_t* in, size_t length) {
    static constexpr auto decoded = [] {
        std::array<std::array<int8_t, 4>, 256> table{};
        for (int byte = 0; byte < 256; ++byte)
            for (int l = 0; l < 4; ++l) {
                const int value = (byte >> (2 * l)) & 3;
                table[byte][l] = value == 3 ? 5 : value;
            }
        return table;
    }();
    size_t l = 0;
    for (; length - l >= 4; l += 4)
        std::memcpy(out + l, decoded[in[l / 4]].data(), 4);
    for (; l < length; ++l) {
        const uint8_t value = (in[l / 4] >> (2 * (l % 4))) & 3;
        out[l] = value == 3 ? 5 : value;
    }
}

// Own a scratch file on the output filesystem, never implicitly in /tmp.
// The name is removed immediately; exceptions also close the descriptor.
class BackingFile {
    int fd_ = -1;
    size_t size_;

    [[noreturn]] static void fail(const char* operation) {
        throw std::runtime_error(std::string("TurboHap LM scratch ") + operation +
                                 ": " + std::strerror(errno));
    }

public:
    explicit BackingFile(size_t bytes) : size_(bytes) {
        if (bytes > static_cast<uint64_t>(std::numeric_limits<off_t>::max()))
            throw std::length_error("TurboHap LM scratch file is too large");
        char name[] = ".findhap-lmp-XXXXXX";
        fd_ = mkstemp(name);
        if (fd_ < 0) fail("create");
        if (unlink(name) != 0 || ftruncate(fd_, bytes) != 0) {
            const int error = errno;
            close(fd_);
            fd_ = -1;
            errno = error;
            fail("prepare");
        }
    }
    BackingFile(const BackingFile&) = delete;
    BackingFile& operator=(const BackingFile&) = delete;
    ~BackingFile() { if (fd_ >= 0) close(fd_); }

    void transfer(bool writing, uint8_t* bytes, size_t count, size_t offset) const {
        if (offset > size_ || count > size_ - offset)
            throw std::out_of_range("TurboHap LM scratch transfer exceeds file");
        while (count) {
            const size_t part = std::min(count, size_t(1024) * 1024 * 1024);
            const ssize_t done = writing ? pwrite(fd_, bytes, part, offset)
                                         : pread(fd_, bytes, part, offset);
            if (done < 0 && errno == EINTR) continue;
            if (done <= 0) {
                if (!done) errno = EIO;
                fail(writing ? "write" : "read");
            }
            bytes += done;
            count -= done;
            offset += done;
        }
    }

    void discard(bool written, size_t offset = 0, size_t bytes = 0) const {
        // Avoid treating an unbounded kernel page cache as a memory saving.
        // DONTNEED remains an OS hint, not a total-system-memory guarantee.
        if (written && fsync(fd_) != 0) fail("sync");
#ifdef POSIX_FADV_DONTNEED
        const int error = posix_fadvise(fd_, offset, bytes, POSIX_FADV_DONTNEED);
        if (error) { errno = error; fail("release file cache"); }
#endif
    }

    void prefetch(size_t offset, size_t bytes) const {
#ifdef POSIX_FADV_WILLNEED
        if (bytes) (void)posix_fadvise(fd_, offset, bytes, POSIX_FADV_WILLNEED);
#endif
    }
};

} // namespace low_memory_plus
