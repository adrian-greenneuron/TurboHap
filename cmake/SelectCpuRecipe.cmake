set(turbohap_recipe_reason "toolchain default")
set(turbohap_arm_test [=[
    #include "population_segment.hpp"
    #if !defined(__ARM_NEON)
    #error This recipe requires ARM64 NEON
    #endif
    #include <omp.h>
    int main(int argc, char**) {
        int8_t raw[64]{}, segment[64]{};
        raw[0] = static_cast<int8_t>(argc);
        int result = findhap_population::normalise(raw, segment, 64) + omp_get_max_threads();
        #if FINDHAP_POP_FINAL_SCAN_NEON
        int fills[64]{}, count = 0;
        result += findhap_population::final_fill_positions(raw, segment, 64, fills, count);
        #endif
        return result;
    }
]=])
set(turbohap_default_spark_clang FALSE)
if(FINDHAP_AUTO_SPARK_CLANG STREQUAL CMAKE_CXX_COMPILER AND CMAKE_CXX_COMPILER_ID STREQUAL "Clang"
        AND turbohap_native_request AND turbohap_host_model STREQUAL "gb10" AND NOT turbohap_explicit_flags
        AND NOT CMAKE_CXX_FLAGS MATCHES "(^| )-(mtune|march|mcpu)=")
    set(turbohap_default_spark_clang TRUE)
    # An unpacked LLVM installation may supply libomp outside the loader's
    # system paths. Carry the linked location into installed executables.
    if(NOT DEFINED CMAKE_INSTALL_RPATH_USE_LINK_PATH)
        set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)
    endif()
    string(APPEND FINDHAP_DEFAULT_RELEASE_FLAGS " -mtune=generic")
    set(turbohap_recipe_reason "native DGX Spark; generic Clang scheduling and LLVM OpenMP")
endif()
if(APPLE AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(arm64|aarch64)$" AND
        (NOT CMAKE_OSX_ARCHITECTURES OR CMAKE_OSX_ARCHITECTURES STREQUAL "arm64"))
    if(turbohap_explicit_flags)
        set(selected "${FINDHAP_RELEASE_FLAGS}")
    else()
        set(selected "${FINDHAP_DEFAULT_RELEASE_FLAGS}")
    endif()
    turbohap_check_recipe("${selected}" "${turbohap_arm_test}" recipe_ok)
    if(recipe_ok)
        set(turbohap_recipe_reason "selected ARM64 recipe linked with NEON and OpenMP")
    elseif(turbohap_explicit_flags OR turbohap_mac_engine_required)
        message(FATAL_ERROR "The requested Apple ARM64 recipe could not link the NEON and OpenMP probe. See the CMake configure log.")
    else()
        # Older toolchains may lack either a CPU spelling or the matching LTO linker.
        set(cpu_flags "")
        if(turbohap_native_request AND NOT CMAKE_CXX_FLAGS MATCHES "(^| )-(march|mcpu)=")
            if(turbohap_apple_cpu AND CMAKE_CXX_COMPILER_ID MATCHES "Clang")
                list(APPEND cpu_flags "-mcpu=${turbohap_apple_cpu}" "-mcpu=native")
            else()
                list(APPEND cpu_flags "-mcpu=native")
                if(turbohap_apple_cpu)
                    list(APPEND cpu_flags "-mcpu=${turbohap_apple_cpu}")
                endif()
            endif()
        endif()
        list(APPEND cpu_flags "baseline")
        foreach(target IN LISTS cpu_flags)
            if(target STREQUAL "baseline")
                set(cpu_flag "")
            else()
                set(cpu_flag "${target} ")
            endif()
            foreach(lto "-flto " "without-lto")
                set(flags "-O3 ${cpu_flag}-funroll-loops ")
                if(NOT lto STREQUAL "without-lto")
                    string(APPEND flags "${lto}")
                endif()
                string(APPEND flags "-ffp-contract=off")
                if(flags STREQUAL selected)
                    continue()
                endif()
                turbohap_check_recipe("${flags}" "${turbohap_arm_test}" recipe_ok)
                if(recipe_ok)
                    set(FINDHAP_DEFAULT_RELEASE_FLAGS "${flags}")
                    set(turbohap_recipe_reason "original recipe failed to link; selected ${target} ${lto}")
                    set(FINDHAP_QUALIFIED_APPLE_M4 FALSE)
                    break()
                endif()
            endforeach()
            if(recipe_ok)
                break()
            endif()
        endforeach()
        if(NOT recipe_ok)
            message(FATAL_ERROR "Apple ARM64 baseline failed to link NEON and OpenMP. Check the SDK, native compiler and OpenMP runtime in MACOS.md.")
        endif()
    endif()
    if(turbohap_mac_engine_enabled)
        string(APPEND turbohap_recipe_reason "; shared Apple Silicon engine")
    endif()
endif()
