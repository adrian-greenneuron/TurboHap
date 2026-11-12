#pragma once
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include "progress_clock.hpp"

#ifndef FINDHAP_MODERN_STDOUT_DEFAULT
#define FINDHAP_MODERN_STDOUT_DEFAULT 0
#endif

namespace turbohap_reporting {
enum class StdoutMode { legacy, modern };
inline StdoutMode stdout_mode = FINDHAP_MODERN_STDOUT_DEFAULT
    ? StdoutMode::modern : StdoutMode::legacy;

inline bool modern_stdout() { return stdout_mode == StdoutMode::modern; }

inline StdoutMode parse_stdout_mode(const char* value) {
    if (std::strcmp(value, "legacy") == 0) return StdoutMode::legacy;
    if (std::strcmp(value, "modern") == 0) return StdoutMode::modern;
    throw std::runtime_error("use --stdout=legacy or --stdout=modern");
}

// Construct the clock only when modern reporting is first used. Legacy runs
// do not read a progress clock or scan library counters for STDOUT.
inline ProgressClock& stdout_clock() {
    static ProgressClock clock;
    return clock;
}

inline int legacy_printf(const char* format, ...) __attribute__((format(printf, 1, 2)));
inline int legacy_printf(const char* format, ...) {
    if (modern_stdout()) return 0;
    va_list arguments;
    va_start(arguments, format);
    const int result = std::vprintf(format, arguments);
    va_end(arguments);
    return result;
}

// Warnings, failures and the completion marker remain visible in both modes.
inline int status_printf(const char* format, ...) __attribute__((format(printf, 1, 2)));
inline int status_printf(const char* format, ...) {
    va_list arguments;
    va_start(arguments, format);
    const int result = modern_stdout() ? stdout_clock().vprint(format, arguments)
                                       : std::vprintf(format, arguments);
    va_end(arguments);
    return result;
}
}
