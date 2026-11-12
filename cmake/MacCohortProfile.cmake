# The selected native Apple Clang M4 Pro cohort recipe. General Apple Silicon
# builds keep their ordinary defaults. No arbitrary feature injection or PGO.
option(FINDHAP_MAC_COHORT_PROFILE "Use the qualified native Apple Clang M4 Pro cohort profile" OFF)
if(FINDHAP_MAC_COHORT_PROFILE)
    if(NOT APPLE OR CMAKE_CROSSCOMPILING OR NOT CMAKE_SYSTEM_PROCESSOR MATCHES "^(arm64|aarch64)$"
            OR NOT CMAKE_CXX_COMPILER_ID STREQUAL "AppleClang" OR NOT FINDHAP_QUALIFIED_APPLE_M4)
        message(FATAL_ERROR "The Mac cohort profile requires a native Apple Clang build on an Apple M4 Pro")
    endif()
    if(NOT CMAKE_BUILD_TYPE STREQUAL "Release" OR CMAKE_CONFIGURATION_TYPES OR CMAKE_CXX_FLAGS
            OR CMAKE_EXE_LINKER_FLAGS OR CMAKE_EXE_LINKER_FLAGS_RELEASE)
        message(FATAL_ERROR "Use a fresh single-configuration Release build without extra compiler or linker flags for the Mac cohort profile")
    endif()
    set(FINDHAP_DEFAULT_RELEASE_FLAGS "-O3 -mcpu=apple-m4 -funroll-loops -flto -Wl,-mllvm,-threads=2 -ffp-contract=off")
    if(DEFINED FINDHAP_RELEASE_FLAGS AND NOT FINDHAP_RELEASE_FLAGS STREQUAL FINDHAP_DEFAULT_RELEASE_FLAGS)
        message(FATAL_ERROR "The Mac cohort profile uses fixed, qualified Release flags; use a fresh build directory")
    endif()
    add_compile_definitions(
        FINDHAP_LIBRARY_METADATA=1 FINDHAP_METADATA_CALLOC=0 FINDHAP_LIBRARY_ACTIVE_BOUND=1
        FINDHAP_PEDIGREE_EARLY_REJECT=1 FINDHAP_PEDIGREE_NEON_NEAR=1 FINDHAP_POP_FUSED_VALIDITY=1
        FINDHAP_POP_VECTOR_SEGMENT=1 FINDHAP_PROGENY_SEGMENT_OWNED=1 FINDHAP_POP_FINAL_SCAN_NEON=1)
endif()

function(findhap_check_mac_cohort_runtime)
    list(LENGTH OpenMP_CXX_LIBRARIES lib_count)
    get_filename_component(runtime_name "${OpenMP_CXX_LIBRARIES}" NAME)
    if(NOT lib_count EQUAL 1 OR NOT runtime_name MATCHES "^libomp(\\.[0-9]+)*\\.dylib$"
            OR NOT OpenMP_CXX_FLAGS STREQUAL "-Xclang -fopenmp")
        message(FATAL_ERROR "The Mac cohort profile requires Apple Clang with libomp only; see MACOS.md")
    endif()
endfunction()
