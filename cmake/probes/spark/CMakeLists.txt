cmake_minimum_required(VERSION 3.16)
project(SparkCompilerProbe LANGUAGES CXX)
if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    message(FATAL_ERROR "Expected LLVM Clang")
endif()
find_package(OpenMP REQUIRED COMPONENTS CXX)
set(llvm_omp FALSE)
foreach(lib IN LISTS OpenMP_CXX_LIBRARIES)
    get_filename_component(name "${lib}" NAME)
    if(name MATCHES "^libomp\\.so($|\\.)")
        set(llvm_omp TRUE)
    elseif(name MATCHES "^lib(gomp|iomp5)\\.")
        message(FATAL_ERROR "Unexpected OpenMP runtime: ${lib}")
    endif()
endforeach()
if(NOT llvm_omp)
    message(FATAL_ERROR "LLVM libomp is required for this recipe")
endif()
include(CheckCXXSourceRuns)
set(CMAKE_REQUIRED_FLAGS "-std=c++17 -O3 -march=native -funroll-loops -flto -ffp-contract=off -mtune=generic")
set(CMAKE_REQUIRED_LIBRARIES OpenMP::OpenMP_CXX)
unset(recipe_runs CACHE)
check_cxx_source_runs([=[
    #include <arm_neon.h>
    #include <charconv>
    #include <omp.h>
    int main() {
        char buffer[16];
        int count = 0;
        #pragma omp parallel reduction(+:count) num_threads(2)
        count += vgetq_lane_s32(vdupq_n_s32(omp_get_thread_num() + 1), 0);
        return std::to_chars(buffer, buffer + sizeof buffer, count).ec != std::errc{} || count < 1;
    }
]=] recipe_runs)
if(NOT recipe_runs EQUAL 1)
    message(FATAL_ERROR "Native Spark NEON/OpenMP/LTO probe failed")
endif()
