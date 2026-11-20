#pragma once

#include <cerrno>
#include <cstdlib>
#include <string>
#include <system_error>
#include <unistd.h>

namespace findhap {
inline int open_scratch_file(char* name, off_t bytes, const char* storage) {
    const int fd = mkstemp(name);
    if (fd < 0)
        throw std::system_error(errno, std::generic_category(), std::string(storage) + " create");

    int unlinked;
    do { unlinked = unlink(name); } while (unlinked < 0 && errno == EINTR);
    if (unlinked != 0) {
        const int error = errno;
        close(fd);
        // This file is still empty. Give the path if its permissions prevent
        // unlinking it, so a caller can remove the remnant.
        throw std::system_error(error, std::generic_category(), std::string(storage) + " unlink " + name);
    }
    if (ftruncate(fd, bytes) != 0) {
        const int error = errno;
        close(fd);
        throw std::system_error(error, std::generic_category(), std::string(storage) + " size");
    }
    return fd;
}
}
