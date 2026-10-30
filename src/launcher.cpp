#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <iostream>
#include <unistd.h>
#include "platform_policy.hpp"
#include "thread_request.hpp"
#include "cpu_policy.hpp"

namespace findhap_standard { int run(int argc, char* argv[]); }
namespace findhap_low_memory { int run(int argc, char* argv[]); }

int main(int argc, char* argv[]) {
    using namespace findhap::placement;
    bool low_memory = false, cpu_info = false, policy_seen = false;
    Policy policy = Policy::automatic;
    std::vector<char*> original(argv, argv + argc);
    original.push_back(nullptr);
    int count = 1;
    try {
        for (int i = 1; i < argc; ++i) {
            if (std::strcmp(argv[i], "--low-memory") == 0) low_memory = true;
            else if (std::strcmp(argv[i], "--cpu-info") == 0) cpu_info = true;
            else if (std::strncmp(argv[i], "--cpu-policy=", 13) == 0) {
                const Policy value = parse(argv[i] + 13);
                if (policy_seen && value != policy) throw std::runtime_error("conflicting --cpu-policy values");
                policy = value; policy_seen = true;
            } else if (std::strncmp(argv[i], "--cpu-policy", 12) == 0)
                throw std::runtime_error("use --cpu-policy=auto, --cpu-policy=inherit or --cpu-policy=performance");
            else argv[count++] = argv[i];
        }
        argv[count] = nullptr;
        if (count > 1 && std::strcmp(argv[1], "--help") == 0) {
            std::puts(
                "TurboHap\n"
                "Usage: findhap [threads] [--low-memory] [--cpu-policy=auto|inherit|performance] [--cpu-info]\n\n"
                "Standard mode prioritizes speed. --low-memory reduces peak memory.\n"
                "Options may appear before or after the thread count.\n"
                "Without a thread count, preserve the OpenMP default (OMP_NUM_THREADS).\n"
                "auto: qualified Spark performance-core selection when the thread request fits.\n"
                "inherit: leave CPU placement to the caller/OpenMP runtime.\n"
                "performance: require a qualified, sufficient performance-core allocation.\n"
                "Explicit OpenMP placement and inherited CPU restrictions take precedence in auto.\n"
                "--cpu-info: show the proposed CPU policy as JSON without applying it or reading inputs.\n"
                "Original findhap.options fields and input/output filenames are unchanged.\n"
                "Other arguments after the thread count are ignored, as in findhapV3.");
            return 0;
        }
        findhap::startup_thread_request = findhap::resolve_thread_request(count, argv);
        const auto request = findhap::startup_thread_request;
        Topology topology;
        const auto decision = start(policy, request.effective, cpu_info, topology);
        if (cpu_info) {
            information(std::cout, policy, request.effective, request.openmp_limit, topology, decision);
            return 0;
        }
#if FINDHAP_TURIN_BINDING
        // Native binding bootstrap: libgomp consumes placement before main.
        // The explicit inherit policy opts out; caller placement always wins.
        if (policy == Policy::automatic && !external_binding()) {
            if (::setenv("OMP_PROC_BIND", "close", 0) || ::setenv("OMP_PLACES", "cores", 0))
                throw std::runtime_error("cannot configure OpenMP placement");
            ::execv("/proc/self/exe", original.data());
            throw std::runtime_error("cannot restart with OpenMP placement");
        }
#endif
        diagnostic(std::cerr, policy, request.effective, decision);
        return low_memory ? findhap_low_memory::run(count, argv)
                          : findhap_standard::run(count, argv);
    } catch (const std::exception& error) {
        std::cerr << "TurboHap: " << error.what() << '\n';
        return 2;
    }
}
