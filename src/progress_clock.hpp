#pragma once
// Coarse progress timestamps only: never read the clock in an inference loop.
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <ctime>

namespace turbohap_reporting {

class ProgressClock {
    using Steady = std::chrono::steady_clock;
    const Steady::time_point started_ = Steady::now();
public:
    // Separate wall time from monotonic elapsed time: clock corrections must
    // not change the durations reconstructed from the elapsed column.
    static void prefix(FILE* destination,
                       std::chrono::system_clock::time_point wall,
                       std::chrono::milliseconds elapsed) {
        using namespace std::chrono;
        const auto second = floor<seconds>(wall);
        const auto fraction = duration_cast<milliseconds>(wall - second).count();
        const std::time_t calendar = system_clock::to_time_t(second);
        std::tm utc{};
#if defined(_WIN32)
        const bool converted = gmtime_s(&utc, &calendar) == 0;
#else
        const bool converted = gmtime_r(&calendar, &utc) != nullptr;
#endif
        char date[32];
        const bool formatted = converted &&
            std::strftime(date, sizeof(date), "%Y-%m-%dT%H:%M:%S", &utc) != 0;
        const long long ticks = elapsed.count();
        if (formatted)
            std::fprintf(destination, "[%s.%03lldZ +%lld.%03llds] ", date,
                         static_cast<long long>(fraction), ticks / 1000, ticks % 1000);
        else
            std::fprintf(destination, "[UTC unavailable +%lld.%03llds] ",
                         ticks / 1000, ticks % 1000);
    }

    // Callers supply one complete progress record, including its final newline.
    // Fixed format strings and stdio avoid a new string allocation per record.
    int vprint(const char* format, va_list arguments) const {
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            Steady::now() - started_);
        prefix(stdout, std::chrono::system_clock::now(), elapsed);
        const int result = std::vprintf(format, arguments);
        std::fflush(stdout);
        return result;
    }

    void print(const char* format, ...) const {
        va_list arguments;
        va_start(arguments, format);
        vprint(format, arguments);
        va_end(arguments);
    }
};

} // namespace turbohap_reporting
