cmake_minimum_required(VERSION 3.16)
project(TurboHap VERSION 1.5.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

include(cmake/MacOSDefaults.cmake)
include(cmake/MacCohortProfile.cmake)

# Preserve published compiler settings on all other platforms.
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    set(FINDHAP_RELEASE_FLAGS "${FINDHAP_DEFAULT_RELEASE_FLAGS}"
        CACHE STRING "Release optimisation flags (retain exact floating-point semantics)")
    set(CMAKE_CXX_FLAGS_RELEASE "${FINDHAP_RELEASE_FLAGS}")
endif()
find_package(OpenMP REQUIRED)
if(FINDHAP_MAC_COHORT_PROFILE)
    findhap_check_mac_cohort_runtime()
endif()
if(APPLE AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(arm64|aarch64)$")
    include(CMakePushCheckState)
    include(CheckCXXSourceCompiles)
    cmake_push_check_state(RESET)
    set(CMAKE_REQUIRED_LIBRARIES ${OpenMP_CXX_LIBRARIES})
    check_cxx_source_compiles("extern \"C\" void kmp_set_blocktime(int);
        extern \"C\" int kmp_get_blocktime();
        int main() { kmp_set_blocktime(1); return kmp_get_blocktime(); }" FINDHAP_HAS_KMP_BLOCKTIME)
    cmake_pop_check_state()
    if(FINDHAP_HAS_KMP_BLOCKTIME)
        add_compile_definitions(FINDHAP_HAVE_KMP_BLOCKTIME=1)
    endif()
endif()
include(cmake/PhaseBufferDefaults.cmake)
if(FINDHAP_MAC_COHORT_PROFILE AND NOT FINDHAP_MAC_MEMORY_DEFAULTS)
    message(FATAL_ERROR "The M4 Pro cohort profile requires the normal Apple Silicon memory policy")
endif()
option(FINDHAP_MODERN_STDOUT_DEFAULT "Use modern STDOUT by default; public source packages must set this OFF" OFF)
add_compile_definitions(FINDHAP_MODERN_STDOUT_DEFAULT=$<BOOL:${FINDHAP_MODERN_STDOUT_DEFAULT}>)
if(FINDHAP_MMAP_PHASE_BUFFERS)
    if(NOT UNIX)
        message(FATAL_ERROR "FINDHAP_MMAP_PHASE_BUFFERS requires POSIX anonymous mappings")
    endif()
    add_compile_definitions(FINDHAP_MMAP_PHASE_BUFFERS=1)
endif()
if(FINDHAP_MAC_MEMORY_DEFAULTS)
    # One directory-level marker reaches all four engines, launcher, native
    # tests and exported Python fixtures. Individual feature overrides win.
    add_compile_definitions(FINDHAP_MAC_MEMORY_DEFAULTS=1)
    message(STATUS "TurboHap: native Apple Silicon memory defaults")
endif()
option(FINDHAP_LIGHT_PROFILE "Build coarse phase diagnostics (not for benchmark timing)" OFF)
option(FINDHAP_WORKLOAD_OPTIMISATIONS "Use the qualified workload profile on native Linux DGX Spark" ON)
set(FINDHAP_ENGINE_SOURCE src/findhap.cpp)
if(FINDHAP_LIGHT_PROFILE)
    add_compile_definitions(FINDHAP_PROFILE=1 FINDHAP_LIGHT_PROFILE=1)
endif()
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
get_directory_property(turbohap_native_definitions COMPILE_DEFINITIONS)
if(FINDHAP_WORKLOAD_OPTIMISATIONS AND NOT CMAKE_CROSSCOMPILING AND CMAKE_SYSTEM_NAME STREQUAL "Linux"
        AND "FINDHAP_NATIVE_GB10=1" IN_LIST turbohap_native_definitions)
    add_compile_definitions(FINDHAP_WORKLOAD_OPTIMISATIONS=1)
endif()
set(FINDHAP_INDEX_MIN_LENGTH 16 CACHE STRING "Minimum exact-index segment length")
add_compile_definitions(FINDHAP_INDEX_MIN_LENGTH=${FINDHAP_INDEX_MIN_LENGTH})

add_library(findhap-standard-engine OBJECT ${FINDHAP_ENGINE_SOURCE})
target_compile_definitions(findhap-standard-engine PRIVATE
    FINDHAP_HIGH_MEMORY=1 FINDHAP_ENGINE_NAMESPACE=findhap_standard FINDHAP_ENTRY_POINT=run)
target_link_libraries(findhap-standard-engine PRIVATE OpenMP::OpenMP_CXX)

add_library(findhap-low-memory-engine OBJECT ${FINDHAP_ENGINE_SOURCE})
target_compile_definitions(findhap-low-memory-engine PRIVATE
    FINDHAP_LOW_MEMORY=1 FINDHAP_LOW_MEMORY_PLUS=1
    FINDHAP_ENGINE_NAMESPACE=findhap_low_memory FINDHAP_ENTRY_POINT=run)
target_link_libraries(findhap-low-memory-engine PRIVATE OpenMP::OpenMP_CXX)

# Preserve the original static engines and compile the two independent row
# loops separately for dynamic scheduling. Startup selects the engine once.
add_library(findhap-dynamic-standard-engine OBJECT ${FINDHAP_ENGINE_SOURCE})
target_compile_definitions(findhap-dynamic-standard-engine PRIVATE
    FINDHAP_HIGH_MEMORY=1 FINDHAP_ENGINE_NAMESPACE=findhap_dynamic_standard FINDHAP_ENTRY_POINT=run
    "FINDHAP_ROW_SCHEDULE=dynamic,4")
target_link_libraries(findhap-dynamic-standard-engine PRIVATE OpenMP::OpenMP_CXX)
add_library(findhap-dynamic-low-memory-engine OBJECT ${FINDHAP_ENGINE_SOURCE})
target_compile_definitions(findhap-dynamic-low-memory-engine PRIVATE
    FINDHAP_LOW_MEMORY=1 FINDHAP_LOW_MEMORY_PLUS=1
    FINDHAP_ENGINE_NAMESPACE=findhap_dynamic_low_memory FINDHAP_ENTRY_POINT=run
    "FINDHAP_ROW_SCHEDULE=dynamic,4")
target_link_libraries(findhap-dynamic-low-memory-engine PRIVATE OpenMP::OpenMP_CXX)


# Retain the original executable name for existing workflows.
add_executable(findhap src/launcher.cpp
    $<TARGET_OBJECTS:findhap-standard-engine>
    $<TARGET_OBJECTS:findhap-low-memory-engine>
    $<TARGET_OBJECTS:findhap-dynamic-standard-engine>
    $<TARGET_OBJECTS:findhap-dynamic-low-memory-engine>)
target_link_libraries(findhap PRIVATE OpenMP::OpenMP_CXX)
set(TURBOHAP_VERSION_LABEL "${PROJECT_VERSION}" CACHE STRING "Version label displayed by turbohap-build-info")
execute_process(COMMAND git rev-parse HEAD WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    OUTPUT_VARIABLE TURBOHAP_SOURCE_COMMIT OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
if(NOT TURBOHAP_SOURCE_COMMIT)
    set(TURBOHAP_SOURCE_COMMIT "archive")
endif()
execute_process(COMMAND git status --porcelain --untracked-files=normal -- . WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    OUTPUT_VARIABLE turbohap_git_status ERROR_QUIET)
set(TURBOHAP_SOURCE_DIRTY false)
if(turbohap_git_status)
    set(TURBOHAP_SOURCE_DIRTY true)
endif()
file(GLOB_RECURSE turbohap_identity_files CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/src/*" "${CMAKE_SOURCE_DIR}/cmake/*")
list(APPEND turbohap_identity_files "${CMAKE_SOURCE_DIR}/CMakeLists.txt")
list(SORT turbohap_identity_files)
set(turbohap_identity "")
foreach(identity_file IN LISTS turbohap_identity_files)
    file(SHA256 "${identity_file}" identity_hash)
    file(RELATIVE_PATH identity_name "${CMAKE_SOURCE_DIR}" "${identity_file}")
    string(APPEND turbohap_identity "${identity_name}:${identity_hash}\n")
endforeach()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${turbohap_identity_files})
string(SHA256 TURBOHAP_SOURCE_SHA256 "${turbohap_identity}")
set(TURBOHAP_OPENMP_NAMES "")
foreach(runtime IN LISTS OpenMP_CXX_LIBRARIES)
    get_filename_component(runtime_name "${runtime}" NAME)
    string(APPEND TURBOHAP_OPENMP_NAMES "${runtime_name} ")
endforeach()
set(TURBOHAP_BUILD_FLAGS "${CMAKE_CXX_FLAGS} ${CMAKE_CXX_FLAGS_RELEASE}")
get_directory_property(TURBOHAP_COMPILE_DEFINITIONS COMPILE_DEFINITIONS)
foreach(identity_field TURBOHAP_BUILD_FLAGS TURBOHAP_VERSION_LABEL TURBOHAP_OPENMP_NAMES TURBOHAP_COMPILE_DEFINITIONS)
    string(REPLACE "\\" "\\\\" ${identity_field} "${${identity_field}}")
    string(REPLACE "\"" "\\\"" ${identity_field} "${${identity_field}}")
endforeach()
configure_file(cmake/BuildIdentity.hpp.in generated/TurboHapBuildIdentity.hpp @ONLY)
add_custom_command(OUTPUT "${CMAKE_BINARY_DIR}/generated/TurboHapExecutableIdentity.hpp"
    COMMAND ${CMAKE_COMMAND} -DFINDHAP_BINARY=$<TARGET_FILE:findhap>
        -DOUTPUT=${CMAKE_BINARY_DIR}/generated/TurboHapExecutableIdentity.hpp
        -P ${CMAKE_SOURCE_DIR}/cmake/ExecutableIdentity.cmake
    DEPENDS findhap ${CMAKE_SOURCE_DIR}/cmake/ExecutableIdentity.cmake)
add_executable(turbohap-build-info src/build_info_main.cpp
    "${CMAKE_BINARY_DIR}/generated/TurboHapExecutableIdentity.hpp")
target_include_directories(turbohap-build-info PRIVATE "${CMAKE_BINARY_DIR}/generated")
target_link_libraries(turbohap-build-info PRIVATE OpenMP::OpenMP_CXX)
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    # Native Apple Silicon memory builds use the selected single partition;
    # every other default retains the original source-unit layout.
    target_link_options(findhap PRIVATE "-flto-partition=${FINDHAP_GNU_LTO_PARTITION}")
endif()
install(TARGETS findhap turbohap-build-info RUNTIME DESTINATION bin)
add_executable(turbohap-validate-inputs src/validate_inputs_main.cpp)
target_link_libraries(turbohap-validate-inputs PRIVATE OpenMP::OpenMP_CXX)
install(TARGETS turbohap-validate-inputs RUNTIME DESTINATION bin)
install(FILES LICENSE README.md MACOS.md CITATION.cff CHANGELOG.md SHA256SUMS
    PUBLIC_PACKAGE_FILES.json DESTINATION share/doc/turbohap)
install(DIRECTORY docs examples DESTINATION share/doc/turbohap PATTERN "__pycache__" EXCLUDE)

include(cmake/PythonFixtureBuild.cmake)

include(CTest)
if(BUILD_TESTING)
    foreach(test_name input_validation map_validation conflict_scan phase_buffer genotype_cache packed_genotypes genotype_workspace low_memory_storage pedigree_conflict pedigree_crossovers ordered_crossovers pedigree_rematch population_signature predicate_order population_minimum ordered_scan lm_ordered integer_output library_output output_streams cpu_policy row_schedule workload_vectors bit_extract haplotype_metadata haplotype_metadata_order library_peaks progress_clock)
        add_executable(test_${test_name} tests/test_${test_name}.cpp)
        target_link_libraries(test_${test_name} PRIVATE OpenMP::OpenMP_CXX)
        # Release builds must retain checks, including cache/storage assertions.
        target_compile_options(test_${test_name} PRIVATE -UNDEBUG)
        add_test(NAME ${test_name} COMMAND test_${test_name})
    endforeach()
    include(CheckLanguage)
    check_language(Fortran)
    if(CMAKE_Fortran_COMPILER)
        enable_language(Fortran)
        add_library(reference_listhap STATIC tests/reference_listhap.f90)
        if(CMAKE_Fortran_COMPILER_ID STREQUAL "GNU")
            target_compile_options(reference_listhap PRIVATE -ffp-contract=off)
        endif()
        add_executable(test_listhap tests/test_listhap.cpp)
        target_link_libraries(test_listhap PRIVATE reference_listhap OpenMP::OpenMP_CXX)
        target_compile_options(test_listhap PRIVATE -UNDEBUG)
        add_test(NAME listhap_fortran COMMAND test_listhap)
        set_tests_properties(listhap_fortran PROPERTIES LABELS "reference")
    else()
        message(STATUS "Fortran listhap differential test unavailable: install a Fortran compiler; inference does not require it")
    endif()
    set(FINDHAP_REFERENCE_BINARY "" CACHE FILEPATH "Pinned compatible executable for raw-byte pedigree differential tests")
    # Exercise imported kernels explicitly without changing production defaults.
    function(findhap_kernel_test name source)
        add_executable(test_${name} tests/test_${source}.cpp)
        target_link_libraries(test_${name} PRIVATE OpenMP::OpenMP_CXX)
        target_compile_options(test_${name} PRIVATE -UNDEBUG)
        target_compile_definitions(test_${name} PRIVATE ${ARGN})
        add_test(NAME ${name} COMMAND test_${name})
    endfunction()
    if(UNIX AND NOT FINDHAP_MMAP_PHASE_BUFFERS)
        findhap_kernel_test(phase_buffer_mmap phase_buffer FINDHAP_MMAP_PHASE_BUFFERS=1)
    endif()
    findhap_kernel_test(word_major_standard population_minimum FINDHAP_ORDERED_LAYOUT=1)
    findhap_kernel_test(word_major_lm lm_ordered FINDHAP_ORDERED_LAYOUT=1)
    findhap_kernel_test(lm_adaptive lm_adaptive FINDHAP_LM_ADAPTIVE_POSITIONS=1
        FINDHAP_LOW_MEMORY=1 FINDHAP_LM_ORDERED_BYTES=4194304)
    findhap_kernel_test(word_major_lm_adaptive lm_adaptive FINDHAP_LM_ADAPTIVE_POSITIONS=1
        FINDHAP_LOW_MEMORY=1 FINDHAP_LM_ORDERED_BYTES=4194304 FINDHAP_ORDERED_LAYOUT=1)
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(arm64|aarch64)$")
        findhap_kernel_test(pop_final_scan population_final_scan FINDHAP_TEST_POP_FINAL_SCAN_NEON=1)
        target_compile_options(test_pop_final_scan PRIVATE
            -UFINDHAP_POP_FINAL_SCAN_NEON -DFINDHAP_POP_FINAL_SCAN_NEON=1)
        findhap_kernel_test(neon_sampled_full population_signature FINDHAP_POP_ENCODE=5 FINDHAP_POP_FULL=2)
        findhap_kernel_test(neon_reused_signature population_signature
            FINDHAP_POP_ENCODE=5 FINDHAP_POP_FULL=2
            FINDHAP_POP_COMPRESS_SIGNATURE=1 FINDHAP_POP_FUSED_VALIDITY=1)
        findhap_kernel_test(neon_packed_text packed_genotypes FINDHAP_NEON_PACK=2 FINDHAP_NEON_TEXT=1)
        findhap_kernel_test(neon_ordered_scan ordered_scan FINDHAP_ORDERED_SCAN_NEON=1 FINDHAP_ORDERED_SCAN_ORDER=1)
    endif()
    find_package(Python3 3.11 COMPONENTS Interpreter QUIET)
    if(Python3_Interpreter_FOUND)
        add_test(NAME workload_policy COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_SOURCE_DIR}/tests/test_workload_policy.py --compiler ${CMAKE_CXX_COMPILER})
        add_test(NAME amd_workload_policy COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_SOURCE_DIR}/tests/test_amd_workload_policy.py --compiler ${CMAKE_CXX_COMPILER})
        set(stdout_default legacy)
        if(FINDHAP_MODERN_STDOUT_DEFAULT)
            set(stdout_default modern)
        endif()
        add_test(NAME stdout_modes COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_SOURCE_DIR}/tests/test_stdout_modes.py --binary $<TARGET_FILE:findhap>
            --default ${stdout_default})
        foreach(suite pedigree deep_pedigree)
            add_test(NAME ${suite}_differential COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_SOURCE_DIR}/tests/run_reference_tests.py --suite ${suite}
                --reference "${FINDHAP_REFERENCE_BINARY}" --candidate $<TARGET_FILE:findhap>
                --record ${CMAKE_BINARY_DIR}/${suite}-differential.json)
            set_tests_properties(${suite}_differential PROPERTIES LABELS "reference;extended" SKIP_RETURN_CODE 77)
        endforeach()
        add_test(NAME release_validation COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_SOURCE_DIR}/tests/test_release_validation.py)
        add_test(NAME standalone_validation COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_SOURCE_DIR}/tests/test_standalone_validation.py --binary $<TARGET_FILE:turbohap-validate-inputs>
            )
        add_test(NAME full_input_audit COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_SOURCE_DIR}/tests/test_full_input_audit.py --binary $<TARGET_FILE:turbohap-validate-inputs>
            --engine $<TARGET_FILE:findhap>)
        add_test(NAME build_information COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_SOURCE_DIR}/tests/test_build_information.py --binary $<TARGET_FILE:turbohap-build-info>
            --engine $<TARGET_FILE:findhap>)
        add_test(NAME input_validation_integration COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_SOURCE_DIR}/tests/test_input_validation.py --binary $<TARGET_FILE:findhap>)
        add_test(NAME fixture_build COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/test_fixture_build.py)
        set_tests_properties(fixture_build PROPERTIES ENVIRONMENT "${FINDHAP_TEST_BUILD_ENV}")
        add_test(NAME launcher COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/test_launcher.py)
        set_tests_properties(launcher PROPERTIES ENVIRONMENT "${FINDHAP_TEST_BUILD_ENV}")
        add_test(NAME example COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/check_example.py
            --binary $<TARGET_FILE:findhap>)
        add_test(NAME platform_policy COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/test_platform_policy.py
            --compiler ${CMAKE_CXX_COMPILER})
        add_test(NAME mac_memory_defaults COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/test_mac_memory_defaults.py
            --compiler ${CMAKE_CXX_COMPILER})
        add_test(NAME cpu_workers COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/test_cpu_workers.py
            --binary $<TARGET_FILE:findhap>)
        set_tests_properties(cpu_workers PROPERTIES ENVIRONMENT "${FINDHAP_TEST_BUILD_ENV}"
            SKIP_REGULAR_EXPRESSION "SKIP:")
        if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
            add_test(NAME output_contract COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/test_output_contract.py
                --candidate $<TARGET_FILE:findhap> --record ${CMAKE_BINARY_DIR}/output-contract.json)
            set_tests_properties(output_contract PROPERTIES ENVIRONMENT "CXX=${CMAKE_CXX_COMPILER}" TIMEOUT 120)
            foreach(schedule static dynamic)
                add_test(NAME example_${schedule} COMMAND ${Python3_EXECUTABLE}
                    ${CMAKE_SOURCE_DIR}/tests/check_example.py --binary $<TARGET_FILE:findhap>
                    --row-schedule ${schedule})
            endforeach()
            add_test(NAME darwin_startup COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/test_darwin_startup.py)
            set_tests_properties(darwin_startup PROPERTIES ENVIRONMENT "${FINDHAP_TEST_BUILD_ENV}" SKIP_REGULAR_EXPRESSION "SKIP:")
        endif()
        if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
            add_test(NAME runtime_binding COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/test_runtime_binding.py)
            set_tests_properties(runtime_binding PROPERTIES ENVIRONMENT "${FINDHAP_TEST_BUILD_ENV}")
            add_test(NAME row_dispatch COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/test_row_dispatch.py
                --binary $<TARGET_FILE:findhap> --record ${CMAKE_BINARY_DIR}/row-dispatch.json)
            set_tests_properties(row_dispatch PROPERTIES TIMEOUT 120)
            add_test(NAME output_contract COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/test_output_contract.py
                --candidate $<TARGET_FILE:findhap> --record ${CMAKE_BINARY_DIR}/output-contract.json)
            set_tests_properties(output_contract PROPERTIES ENVIRONMENT "${FINDHAP_TEST_BUILD_ENV}" TIMEOUT 120)
        endif()
    else()
        message(STATUS "Python integration tests unavailable: the full suite requires Python 3.11 or newer")
    endif()
endif()
