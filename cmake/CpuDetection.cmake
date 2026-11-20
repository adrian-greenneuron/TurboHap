# Host discovery is confined to configuration. Cross and portable builds use the target toolchain.
option(FINDHAP_PORTABLE_BUILD "Build for the toolchain baseline, without native CPU specialisation" OFF)
option(FINDHAP_TRY_CPU_PROFILE "Qualify a recognised native CPU with another compiler" OFF)
set(turbohap_host_model unknown)
set(turbohap_host_identity "not probed")
set(turbohap_host_features unknown)
set(turbohap_can_probe_host TRUE)
if(CMAKE_CROSSCOMPILING OR FINDHAP_PORTABLE_BUILD OR CMAKE_CXX_COMPILER_TARGET OR
        (APPLE AND CMAKE_OSX_ARCHITECTURES AND
         NOT CMAKE_OSX_ARCHITECTURES STREQUAL CMAKE_HOST_SYSTEM_PROCESSOR))
    set(turbohap_can_probe_host FALSE)
endif()
if(turbohap_can_probe_host)
    # Baseline executable: do not run the caller's optional target ISA on the build host.
    function(turbohap_probe_host)
        # Retain compiler search paths. A standalone Clang can require these even
        # to link a baseline program against the system C++ library.
        separate_arguments(driver_args NATIVE_COMMAND "${CMAKE_CXX_FLAGS}")
        set(CMAKE_CXX_FLAGS "")
        set(wants_path FALSE)
        foreach(arg IN LISTS driver_args)
            if(wants_path OR arg MATCHES "^(--gcc-install-dir|--gcc-toolchain|-resource-dir|--sysroot|--?isysroot)="
                    OR arg MATCHES "^-B.+")
                string(REPLACE "\\" "\\\\" quoted "${arg}")
                string(REPLACE "\"" "\\\"" quoted "${quoted}")
                string(APPEND CMAKE_CXX_FLAGS " \"${quoted}\"")
                set(wants_path FALSE)
            elseif(arg MATCHES "^(--gcc-install-dir|--gcc-toolchain|-resource-dir|--sysroot|--?isysroot|-B)$")
                string(APPEND CMAKE_CXX_FLAGS " ${arg}")
                set(wants_path TRUE)
            endif()
        endforeach()
        set(CMAKE_TRY_COMPILE_CONFIGURATION Debug)
        set(CMAKE_CXX_FLAGS_DEBUG "")
        set(CMAKE_EXE_LINKER_FLAGS "")
        set(CMAKE_EXE_LINKER_FLAGS_DEBUG "")
        try_run(cpu_status cpu_compiled "${CMAKE_BINARY_DIR}/CMakeFiles/host-cpu"
            "${CMAKE_CURRENT_LIST_DIR}/HostCpu.cpp" RUN_OUTPUT_VARIABLE result)
        if(cpu_compiled AND cpu_status STREQUAL "0" AND
                result MATCHES "^([^\n]+)\n([^\n]+)\n([^\n]+)\n")
            set(turbohap_host_model "${CMAKE_MATCH_1}" PARENT_SCOPE)
            set(turbohap_host_identity "${CMAKE_MATCH_2}" PARENT_SCOPE)
            set(turbohap_host_features "${CMAKE_MATCH_3}" PARENT_SCOPE)
        endif()
        unset(cpu_status CACHE)
        unset(cpu_compiled CACHE)
    endfunction()
    turbohap_probe_host()
endif()

# Apple reports the product tier as well as the generation. Clang's CPU names
# use the generation (Apple M1 Pro -> apple-m1).
set(turbohap_apple_cpu "")
if(APPLE AND turbohap_can_probe_host AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(arm64|aarch64)$"
        AND turbohap_host_identity MATCHES "^Apple M([1-9][0-9]*)( |$)")
    set(turbohap_apple_cpu "apple-m${CMAKE_MATCH_1}")
endif()

# Test a real linked executable using the same OpenMP target, SDK and flags as Release.
# Reprobe on reconfiguration so SDK, runtime or user flag changes cannot reuse a stale result.
include(CheckCXXSourceCompiles)
include(CMakePushCheckState)
function(turbohap_check_recipe flags source result)
    cmake_push_check_state(RESET)
    set(CMAKE_TRY_COMPILE_TARGET_TYPE EXECUTABLE)
    set(CMAKE_TRY_COMPILE_CONFIGURATION Release)
    set(CMAKE_CXX_FLAGS_RELEASE "${flags}")
    set(CMAKE_REQUIRED_INCLUDES "${PROJECT_SOURCE_DIR}/src")
    set(CMAKE_REQUIRED_LIBRARIES OpenMP::OpenMP_CXX)
    get_directory_property(definitions COMPILE_DEFINITIONS)
    foreach(def IN LISTS definitions)
        list(APPEND CMAKE_REQUIRED_DEFINITIONS "-D${def}")
    endforeach()
    get_directory_property(CMAKE_REQUIRED_LINK_OPTIONS LINK_OPTIONS)
    set(CMAKE_REQUIRED_QUIET TRUE)
    unset(turbohap_recipe_ok CACHE)
    check_cxx_source_compiles("${source}" turbohap_recipe_ok)
    set(${result} "${turbohap_recipe_ok}" PARENT_SCOPE)
    unset(turbohap_recipe_ok CACHE)
    cmake_pop_check_state()
endfunction()
