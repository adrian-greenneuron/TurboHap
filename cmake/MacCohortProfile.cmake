# AUTO follows the native host and toolchain at each configure. Preserve explicit
# and cached choices, including OFF from older builds.
if(NOT DEFINED FINDHAP_MAC_COHORT_PROFILE)
    set(FINDHAP_MAC_COHORT_PROFILE AUTO CACHE STRING "Shared native Apple Silicon engine (AUTO, ON or OFF)")
endif()
if(DEFINED CACHE{FINDHAP_MAC_COHORT_PROFILE})
    set_property(CACHE FINDHAP_MAC_COHORT_PROFILE PROPERTY STRINGS AUTO ON OFF)
endif()
string(TOUPPER "${FINDHAP_MAC_COHORT_PROFILE}" turbohap_mac_engine_choice)
set(turbohap_mac_engine_required FALSE)
set(turbohap_mac_engine_enabled FALSE)
if(NOT turbohap_mac_engine_choice STREQUAL "AUTO")
    if(turbohap_mac_engine_choice MATCHES "^(ON|TRUE|YES|Y|1)$")
        set(turbohap_mac_engine_required TRUE)
    elseif(turbohap_mac_engine_choice MATCHES "^(OFF|FALSE|NO|N|0)$")
        return()
    else()
        message(FATAL_ERROR "FINDHAP_MAC_COHORT_PROFILE accepts AUTO, ON or OFF")
    endif()
endif()

list(LENGTH OpenMP_CXX_LIBRARIES turbohap_omp_count)
get_filename_component(turbohap_omp_name "${OpenMP_CXX_LIBRARIES}" NAME)
if(APPLE AND turbohap_can_probe_host AND turbohap_native_request AND turbohap_apple_cpu
        AND FINDHAP_APPLE_OPTIMISATIONS AND FINDHAP_MAC_MEMORY_DEFAULTS
        AND " ${turbohap_host_features} " MATCHES " neon " AND CMAKE_CXX_COMPILER_ID STREQUAL "AppleClang"
        AND CMAKE_BUILD_TYPE STREQUAL "Release" AND NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_CXX_FLAGS
        AND NOT CMAKE_EXE_LINKER_FLAGS AND NOT CMAKE_EXE_LINKER_FLAGS_RELEASE
        AND turbohap_omp_count EQUAL 1 AND turbohap_omp_name MATCHES "^libomp([.][0-9]+)*[.]dylib$"
        AND OpenMP_CXX_FLAGS STREQUAL "-Xclang -fopenmp"
        AND (NOT turbohap_explicit_flags OR turbohap_mac_engine_required))
    set(turbohap_mac_engine_enabled TRUE)
    if(FINDHAP_QUALIFIED_APPLE_M4)
        # Keep the exact compiler flags used for the M4 Pro qualification.
        set(FINDHAP_DEFAULT_RELEASE_FLAGS "-O3 ${turbohap_default_cpu_flag} -funroll-loops -flto -Wl,-mllvm,-threads=2 -ffp-contract=off")
    endif()
    if(turbohap_explicit_flags AND NOT FINDHAP_RELEASE_FLAGS STREQUAL FINDHAP_DEFAULT_RELEASE_FLAGS)
        message(FATAL_ERROR "The requested Apple Silicon engine requires the native Release flags for this Mac")
    endif()
    add_compile_definitions(FINDHAP_APPLE_SILICON_COHORT=1)
    add_compile_definitions(
        FINDHAP_LIBRARY_METADATA=1 FINDHAP_METADATA_CALLOC=0 FINDHAP_LIBRARY_ACTIVE_BOUND=1
        FINDHAP_PEDIGREE_EARLY_REJECT=1 FINDHAP_PEDIGREE_NEON_NEAR=1 FINDHAP_POP_FUSED_VALIDITY=1
        FINDHAP_POP_VECTOR_SEGMENT=1 FINDHAP_PROGENY_SEGMENT_OWNED=1 FINDHAP_POP_FINAL_SCAN_NEON=1)
    # Matching and LM lookup.
    add_compile_definitions(
        FINDHAP_PEDIGREE_VECTOR_BASE=1
        FINDHAP_PEDIGREE_MASK_BACKEND=4
        FINDHAP_POP_COMPRESS_SIGNATURE=1
        FINDHAP_M4_COHORT_ORDERED_SCAN_ORDER=2
        FINDHAP_PEDIGREE_REMATCH=2
        FINDHAP_LM_ADAPTIVE_POSITIONS=1)
    # Packed inheritance, padded counters and Standard suggestion avoidance.
    add_compile_definitions(
        FINDHAP_PACKED_INHERITANCE=1
        FINDHAP_NHAP_STRIDE=16
        FINDHAP_AXION_STD_SUGGESTIONS=1)
    # LM scratch writeback.
    add_compile_definitions(
        FINDHAP_LM_WRITEBACK_BATCH_BYTES=134217728)
    # Output writers.
    add_compile_definitions(
        FINDHAP_PARALLEL_ASSIGNMENT_COUNTS=1
        FINDHAP_PARALLEL_ASSIGNMENT_OUTPUT=1
        FINDHAP_ASYNC_DENSE_OUTPUT=1
        FINDHAP_OVERLAP_CROSSOVERS=1
        FINDHAP_GROUP_HAPFOUND=1)
elseif(turbohap_mac_engine_required)
    message(FATAL_ERROR "The requested engine needs a native Apple Silicon Release build, Apple Clang with libomp, and native memory settings. See MACOS.md.")
endif()
