#include "platform_policy.hpp"
#include "population_segment.hpp"
#include "pedigree_conflict.hpp"
#include "pedigree_frequency.hpp"
#include <immintrin.h>
#include <omp.h>
#if defined(FINDHAP_PROBED_GRANITE) && !TURBOHAP_NATIVE_INTEL_WORKLOAD_PROFILE
#error Granite kernel target unavailable
#elif defined(FINDHAP_PROBED_EMERALD) && !TURBOHAP_NATIVE_EMERALD_RAPIDS
#error Emerald kernel target unavailable
#elif defined(FINDHAP_PROBED_SAPPHIRE) && !TURBOHAP_NATIVE_SAPPHIRE_RAPIDS
#error Sapphire kernel target unavailable
#elif defined(FINDHAP_PROBED_ICE) && !TURBOHAP_NATIVE_ICE_LAKE
#error Ice kernel target unavailable
#elif defined(FINDHAP_PROBED_CASCADE) && !TURBOHAP_NATIVE_CASCADE_LAKE
#error Cascade kernel target unavailable
#elif defined(FINDHAP_PROBED_TURIN) && !TURBOHAP_NATIVE_AMD_WORKLOAD_PROFILE
#error Turin kernel target unavailable
#elif defined(FINDHAP_PROBED_GENOA) && !TURBOHAP_NATIVE_GENOA
#error Genoa kernel target unavailable
#elif !defined(__AVX2__)
#error AVX2 unavailable
#endif
int main(int argc, char**) {
    alignas(64) int8_t values[64]{}, other[64]{};
    int sums[64]{}, counts[64]{};
    values[0] = static_cast<int8_t>(argc);
    int known = findhap_population::normalise(values, other, 64);
    findhap_pedigree::add_base_frequency(values, sums, counts, 64);
    int result = known + pedigree_single_conflict(values, other, 64) + sums[0] + omp_get_max_threads();
#if FINDHAP_POP_FINAL_SCAN_X86
    int filled = 0;
    result += findhap_population::final_fill_positions(values, other, 64, counts, filled);
#endif
    return result;
}
