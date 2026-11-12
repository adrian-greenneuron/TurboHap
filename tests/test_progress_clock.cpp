#include "../src/progress_clock.hpp"
#include <cstdlib>
#include <string>
#include <unistd.h>

static void require(bool passed) {
    if (!passed) {
        std::fputs("progress timestamp check failed\n", stderr);
        std::exit(1);
    }
}

static std::string contents(FILE* stream) {
    require(std::fflush(stream) == 0);
    std::rewind(stream);
    std::string result;
    char block[512];
    for (size_t size; (size = std::fread(block, 1, sizeof(block), stream));)
        result.append(block, size);
    require(!std::ferror(stream));
    return result;
}

int main() {
    using namespace std::chrono;
    using turbohap_reporting::ProgressClock;
    // UTC must be independent of the user's time zone, including DST rules.
    require(setenv("TZ", "Pacific/Auckland", 1) == 0);
    tzset();
    FILE* capture = std::tmpfile();
    require(capture != nullptr);
    const auto epoch = system_clock::from_time_t(0);
    ProgressClock::prefix(capture, epoch + milliseconds(123), milliseconds(172456));
    std::fputs("Step 2 iteration 3 completed\n", capture);
    ProgressClock::prefix(capture, epoch + milliseconds(86399999), milliseconds(86400000));
    std::fputs("Last millisecond before midnight\n", capture);
    ProgressClock::prefix(capture, epoch + seconds(86400), milliseconds(86400001));
    std::fputs("Next day\n", capture);
    // A wall-clock correction does not reset monotonic elapsed time.
    ProgressClock::prefix(capture, epoch + seconds(86399), milliseconds(86400002));
    std::fputs("Wall clock moved backwards\n", capture);
    require(contents(capture) ==
        "[1970-01-01T00:00:00.123Z +172.456s] Step 2 iteration 3 completed\n"
        "[1970-01-01T23:59:59.999Z +86400.000s] Last millisecond before midnight\n"
        "[1970-01-02T00:00:00.000Z +86400.001s] Next day\n"
        "[1970-01-01T23:59:59.000Z +86400.002s] Wall clock moved backwards\n");
    std::fclose(capture);

    // The live writer flushes each complete record, even with redirected STDOUT.
    capture = std::tmpfile();
    require(capture != nullptr && std::fflush(stdout) == 0);
    const int saved = dup(fileno(stdout));
    require(saved >= 0 && dup2(fileno(capture), fileno(stdout)) >= 0);
    ProgressClock clock;
    clock.print("Step %d iteration %d completed\n", 2, 3);
    require(lseek(fileno(capture), 0, SEEK_CUR) > 0);
    require(dup2(saved, fileno(stdout)) >= 0);
    close(saved);
    const auto text = contents(capture);
    require(!text.empty() && text.front() == '[');
    require(text.find("Z +") != std::string::npos);
    require(text.find("s] Step 2 iteration 3 completed\n") != std::string::npos);
    std::fclose(capture);
}
