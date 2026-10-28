cmake_minimum_required(VERSION 3.16)
project(TurboHap VERSION 1.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# Preserve the compiler settings used for the published measurements.
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    set(CMAKE_CXX_FLAGS_RELEASE "-O3 -march=native -funroll-loops -flto -ffp-contract=off")
endif()
find_package(OpenMP REQUIRED)
set(FINDHAP_INDEX_MIN_LENGTH 16 CACHE STRING "Minimum exact-index segment length")
add_compile_definitions(FINDHAP_INDEX_MIN_LENGTH=${FINDHAP_INDEX_MIN_LENGTH})

add_library(findhap-standard-engine OBJECT src/findhap.cpp)
target_compile_definitions(findhap-standard-engine PRIVATE
    FINDHAP_HIGH_MEMORY=1 FINDHAP_ENGINE_NAMESPACE=findhap_standard FINDHAP_ENTRY_POINT=run)
target_link_libraries(findhap-standard-engine PRIVATE OpenMP::OpenMP_CXX)

add_library(findhap-low-memory-engine OBJECT src/findhap.cpp)
target_compile_definitions(findhap-low-memory-engine PRIVATE
    FINDHAP_LOW_MEMORY=1 FINDHAP_LOW_MEMORY_PLUS=1
    FINDHAP_ENGINE_NAMESPACE=findhap_low_memory FINDHAP_ENTRY_POINT=run)
target_link_libraries(findhap-low-memory-engine PRIVATE OpenMP::OpenMP_CXX)

# Retain the original executable name for existing workflows.
add_executable(findhap src/launcher.cpp
    $<TARGET_OBJECTS:findhap-standard-engine>
    $<TARGET_OBJECTS:findhap-low-memory-engine>)
target_link_libraries(findhap PRIVATE OpenMP::OpenMP_CXX)
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    # Keep each engine's source-unit layout during link-time optimisation.
    target_link_options(findhap PRIVATE -flto-partition=1to1)
endif()
install(TARGETS findhap RUNTIME DESTINATION bin)
install(FILES LICENSE DESTINATION share/doc/turbohap)

include(CTest)
if(BUILD_TESTING)
    foreach(test_name genotype_cache genotype_workspace low_memory_storage pedigree_conflict pedigree_crossovers population_signature population_minimum)
        add_executable(test_${test_name} tests/test_${test_name}.cpp)
        target_link_libraries(test_${test_name} PRIVATE OpenMP::OpenMP_CXX)
        if(test_name MATCHES "^(pedigree_|population_)")
            target_compile_options(test_${test_name} PRIVATE -UNDEBUG)
        endif()
        add_test(NAME ${test_name} COMMAND test_${test_name})
    endforeach()
    find_package(Python3 COMPONENTS Interpreter QUIET)
    if(Python3_Interpreter_FOUND)
        add_test(NAME launcher COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/test_launcher.py)
        set_tests_properties(launcher PROPERTIES ENVIRONMENT "CXX=${CMAKE_CXX_COMPILER}")
        add_test(NAME example COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/check_example.py
            --binary $<TARGET_FILE:findhap>)
    endif()
endif()
