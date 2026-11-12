#include "../src/report_events.hpp"
#include <cstdio>
#include <cstdlib>
#include <string>
#include <unistd.h>

static void require(bool ok) {
    if (!ok) {
        std::fputs("library high-water reporting check failed\n", stderr);
        std::exit(1);
    }
}

int main() {
    turbohap_reporting::stdout_mode = turbohap_reporting::StdoutMode::modern;
    unsetenv("TURBOHAP_REPORT_EVENTS");
    FILE* capture = std::tmpfile();
    require(capture != nullptr);
    const int original = dup(fileno(stdout));
    require(original >= 0 && dup2(fileno(capture), fileno(stdout)) >= 0);
    {
        turbohap_reporting::Events events;
        // Padded counters exercise the same stride as the native engines.
        int counts[] = {2, -99, 360000, -99};
        const int chromosomes[] = {1, 5};
        auto emit = [&](const char* kind, int step, int iteration) {
            events.emit(kind, step, iteration, 2, counts, 2,
                        20, 0, 0, 0, -1, -1, chromosomes, 500000);
        };
        events.emit("step_start", 1);
        emit("iteration_end", 1, 2);
        counts[2] = 400001;
        emit("iteration_end", 1, 4);
        counts[2] = 264587;
        emit("step_end", 1, 0);
        events.emit("step_start", 2);
        counts[0] = 300000;
        emit("iteration_end", 2, 1);
        counts[0] = 10; counts[2] = 20;
        emit("step_end", 2, 0);
        emit("run_complete", 2, 4);
    }
    std::fflush(stdout);
    require(dup2(original, fileno(stdout)) >= 0);
    close(original);
    std::rewind(capture);
    std::string text;
    char block[4096];
    for (size_t n; (n = std::fread(block, 1, sizeof(block), capture));) text.append(block, n);
    std::fclose(capture);
    require(text.find("step 1 live peak 400001 before pruning; segment 2 chromosome 5; retained maximum 264587") != std::string::npos);
    require(text.find("step 2 iteration 1 completed; live peak 300000; segment 1 chromosome 1") != std::string::npos);
    require(text.find("run peak 400001; step 1 iteration 4 segment 2 chromosome 5; maxhap 500000") != std::string::npos);
    require(text.find("headroom 99999 slots") != std::string::npos);
}
