cmake_minimum_required(VERSION 3.16)
project(TurboHap VERSION 1.2.0 LANGUAGES CXX)

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
# Resolve qualified ARM profiles at build time. GCC13 recognises V2 but not
# GB10, so native Linux Spark builds also use the exact firmware product ID.
# This selects code paths only; it does not select or bind execution cores.
if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64)$" AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    execute_process(COMMAND ${CMAKE_CXX_COMPILER} -march=native "-###" -c -x c++ /dev/null
        OUTPUT_VARIABLE native_driver_stdout ERROR_VARIABLE native_driver_stderr
        RESULT_VARIABLE native_driver_status)
    if(native_driver_status EQUAL 0 AND "${native_driver_stdout}${native_driver_stderr}" MATCHES "-mcpu=neoverse-v2([+ \"]|$)")
        add_compile_definitions(FINDHAP_NATIVE_NEOVERSE_V2=1)
    elseif(native_driver_status EQUAL 0 AND native_driver_stderr MATCHES "-mcpu=gb10([+ \"]|$)")
        add_compile_definitions(FINDHAP_NATIVE_GB10=1)
    elseif(NOT CMAKE_CROSSCOMPILING AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
        execute_process(COMMAND ${CMAKE_COMMAND} -E cat /sys/class/dmi/id/product_name
            OUTPUT_VARIABLE native_product OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET RESULT_VARIABLE native_product_status)
        if(native_product_status EQUAL 0 AND native_product STREQUAL "NVIDIA_DGX_Spark")
            add_compile_definitions(FINDHAP_NATIVE_GB10=1)
        endif()
    endif()
endif()
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
    foreach(test_name genotype_cache genotype_workspace low_memory_storage pedigree_conflict pedigree_crossovers pedigree_rematch population_signature population_minimum lm_ordered integer_output library_output output_streams cpu_policy)
        add_executable(test_${test_name} tests/test_${test_name}.cpp)
        target_link_libraries(test_${test_name} PRIVATE OpenMP::OpenMP_CXX)
        if(test_name MATCHES "^(pedigree_|population_|lm_ordered|integer_output|library_output|output_streams|cpu_policy)")
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
        add_test(NAME platform_policy COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/test_platform_policy.py
            --compiler ${CMAKE_CXX_COMPILER})
        add_test(NAME runtime_binding COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/test_runtime_binding.py)
        set_tests_properties(runtime_binding PROPERTIES ENVIRONMENT "CXX=${CMAKE_CXX_COMPILER}")
        add_test(NAME cpu_workers COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/test_cpu_workers.py
            --binary $<TARGET_FILE:findhap>)
        set_tests_properties(cpu_workers PROPERTIES ENVIRONMENT "CXX=${CMAKE_CXX_COMPILER}")
        if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
            add_test(NAME output_contract COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/test_output_contract.py
                --candidate $<TARGET_FILE:findhap> --record ${CMAKE_BINARY_DIR}/output-contract.json)
            set_tests_properties(output_contract PROPERTIES ENVIRONMENT "CXX=${CMAKE_CXX_COMPILER}" TIMEOUT 120)
        endif()
    endif()
endif()
