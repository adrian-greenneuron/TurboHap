#pragma once
#include <algorithm>
#include "options_reader.hpp"
#include <omp.h>

namespace findhap {
struct ThreadRequest {
    int openmp_limit = 0;
    int effective = 0;
};
// Resolve before changing affinity. Reuse exactly this request in either engine;
// an affinity change must not silently change an omitted OpenMP thread count.
inline ThreadRequest startup_thread_request;
inline ThreadRequest resolve_thread_request(int argc, char** argv) {
    const int limit = omp_get_max_threads();
    return {limit, argc > 1 ? std::min(limit, thread_argument(argv[1])) : limit};
}
inline ThreadRequest engine_thread_request(int argc, char** argv) {
    return startup_thread_request.openmp_limit ? startup_thread_request : resolve_thread_request(argc, argv);
}
}
