# Native Mac qualification is independent of the Linux server profiles.
option(FINDHAP_APPLE_OPTIMISATIONS "Enable the measured native Apple M4 Pro profile" ON)
option(FINDHAP_PREFER_LIBOMP "Prefer libomp on the qualified native Mac" ON)
set(FINDHAP_QUALIFIED_APPLE_M4 FALSE)
set(FINDHAP_DEFAULT_RELEASE_FLAGS "-O3 -march=native -funroll-loops -flto -ffp-contract=off")
if(APPLE AND NOT CMAKE_CROSSCOMPILING AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(arm64|aarch64)$")
    execute_process(COMMAND /usr/sbin/sysctl -n machdep.cpu.brand_string
        OUTPUT_VARIABLE findhap_host_cpu OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET RESULT_VARIABLE findhap_host_status)
    if(FINDHAP_APPLE_OPTIMISATIONS AND findhap_host_status EQUAL 0 AND findhap_host_cpu STREQUAL "Apple M4 Pro" AND
            (NOT CMAKE_OSX_ARCHITECTURES OR "${CMAKE_OSX_ARCHITECTURES}" STREQUAL "arm64"))
        set(FINDHAP_QUALIFIED_APPLE_M4 TRUE)
        add_compile_definitions(FINDHAP_NATIVE_APPLE_M4PRO=1)
        message(STATUS "TurboHap: qualified Apple M4 Pro profile; core count is discovered at runtime")
        if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
            set(FINDHAP_DEFAULT_RELEASE_FLAGS "-O3 -mcpu=apple-m4 -flto -Wl,-mllvm,-threads=2 -ffp-contract=off")
        endif()
    endif()
endif()

# Respect explicit OpenMP cache settings. Apple Clang needs an external runtime;
# qualified GNU Mac builds use its tested GOMP-compatible libomp C interface.
if(APPLE AND (CMAKE_CXX_COMPILER_ID STREQUAL "AppleClang" OR
        (FINDHAP_QUALIFIED_APPLE_M4 AND FINDHAP_PREFER_LIBOMP)) AND
        NOT DEFINED OpenMP_CXX_FLAGS AND NOT DEFINED OpenMP_CXX_LIB_NAMES)
    find_program(findhap_brew brew HINTS /opt/homebrew/bin /usr/local/bin)
    if(findhap_brew)
        execute_process(COMMAND ${findhap_brew} --prefix libomp
            OUTPUT_VARIABLE findhap_omp_prefix OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    endif()
    find_library(findhap_omp_library NAMES omp HINTS "${OpenMP_ROOT}/lib" "$ENV{OpenMP_ROOT}/lib" "${findhap_omp_prefix}/lib"
        /opt/homebrew/opt/libomp/lib /usr/local/opt/libomp/lib)
    find_path(findhap_omp_include omp.h HINTS "${OpenMP_ROOT}/include" "$ENV{OpenMP_ROOT}/include" "${findhap_omp_prefix}/include"
        /opt/homebrew/opt/libomp/include /usr/local/opt/libomp/include)
    if(findhap_omp_library AND findhap_omp_include)
        if(CMAKE_CXX_COMPILER_ID STREQUAL "AppleClang")
            set(OpenMP_CXX_FLAGS "-Xclang -fopenmp" CACHE STRING "Native Mac OpenMP compile flags")
        else()
            set(OpenMP_CXX_FLAGS "-fopenmp" CACHE STRING "Native Mac OpenMP compile flags")
        endif()
        set(OpenMP_CXX_LIB_NAMES omp CACHE STRING "Native Mac OpenMP library")
        set(OpenMP_omp_LIBRARY "${findhap_omp_library}" CACHE FILEPATH "Native Mac OpenMP runtime")
        if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
            set(OpenMP_CXX_INCLUDE_DIR "${findhap_omp_include}" CACHE PATH "Native Mac OpenMP headers")
        endif()
    endif()
endif()

# Keep LLVM bitcode and its reader in the same toolchain. Apple Clang uses
# the SDK linker default; the separate Homebrew LLVM has its own libLTO.
if(FINDHAP_QUALIFIED_APPLE_M4 AND CMAKE_CXX_COMPILER_ID STREQUAL "Clang" AND
        NOT FINDHAP_RELEASE_FLAGS MATCHES "lto_library")
    get_filename_component(findhap_compiler_directory "${CMAKE_CXX_COMPILER}" DIRECTORY)
    get_filename_component(findhap_matching_lto "${findhap_compiler_directory}/../lib/libLTO.dylib" ABSOLUTE)
    if(EXISTS "${findhap_matching_lto}")
        add_link_options("-Wl,-lto_library,${findhap_matching_lto}")
    endif()
endif()
