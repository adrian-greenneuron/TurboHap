cmake_minimum_required(VERSION 3.16)
include(cmake/SparkCompiler.cmake)
project(TurboHap VERSION 1.6.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

include(cmake/CpuDetection.cmake)
set(turbohap_native_request ${turbohap_can_probe_host})
set(turbohap_explicit_flags FALSE)
if(DEFINED FINDHAP_RELEASE_FLAGS AND NOT FINDHAP_RELEASE_FLAGS STREQUAL FINDHAP_LAST_AUTO_FLAGS)
    set(turbohap_explicit_flags TRUE)
elseif(DEFINED CMAKE_CXX_FLAGS_RELEASE AND NOT DEFINED FINDHAP_RELEASE_FLAGS)
    string(STRIP "${CMAKE_CXX_FLAGS_RELEASE_INIT}" init_flags)
    if(NOT CMAKE_CXX_FLAGS_RELEASE STREQUAL init_flags)
        set(FINDHAP_RELEASE_FLAGS "${CMAKE_CXX_FLAGS_RELEASE}")
        set(turbohap_explicit_flags TRUE)
    endif()
endif()
set(turbohap_user_target_flags "${CMAKE_CXX_FLAGS}")
if(turbohap_explicit_flags)
    string(APPEND turbohap_user_target_flags " ${FINDHAP_RELEASE_FLAGS}")
endif()
if(turbohap_explicit_flags OR CMAKE_CXX_FLAGS MATCHES "(^| )-(march|mcpu)=")
    set(turbohap_native_request FALSE)
    string(TOUPPER "${FINDHAP_MAC_COHORT_PROFILE}" turbohap_requested_mac_profile)
    if(turbohap_can_probe_host AND (turbohap_user_target_flags MATCHES "(^| )-(march|mcpu)=native( |$)"
          OR (turbohap_requested_mac_profile MATCHES "^(ON|TRUE|YES|Y|1)$" AND turbohap_apple_cpu AND
              turbohap_user_target_flags MATCHES "(^| )-mcpu=${turbohap_apple_cpu}( |$)")))
        set(turbohap_native_request TRUE)
    endif()
endif()
include(cmake/MacOSDefaults.cmake)

if(NOT turbohap_can_probe_host OR CMAKE_CXX_FLAGS MATCHES "(^| )-(march|mcpu)=")
    string(REPLACE "${turbohap_default_cpu_flag} " "" FINDHAP_DEFAULT_RELEASE_FLAGS "${FINDHAP_DEFAULT_RELEASE_FLAGS}")
endif()
if(FINDHAP_PORTABLE_BUILD AND turbohap_user_target_flags MATCHES "(^| )-(march|mcpu|mtune)=")
    message(FATAL_ERROR "A portable build uses the toolchain baseline. Use a fresh build directory without CPU tuning flags, or turn off FINDHAP_PORTABLE_BUILD for an explicit CPU target.")
endif()
if(NOT turbohap_can_probe_host AND turbohap_user_target_flags MATCHES "(^| )-(march|mcpu|mtune)=native( |$)")
    message(FATAL_ERROR "A cross or portable build needs a declared target; native tuning would describe the build machine.")
endif()
find_package(OpenMP REQUIRED)
include(cmake/PhaseBufferDefaults.cmake)
include(cmake/MacCohortProfile.cmake)
include(cmake/SelectCpuRecipe.cmake)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    if(NOT turbohap_explicit_flags)
        set(FINDHAP_RELEASE_FLAGS "${FINDHAP_DEFAULT_RELEASE_FLAGS}" CACHE STRING "Release optimisation flags" FORCE)
        set(FINDHAP_LAST_AUTO_FLAGS "${FINDHAP_RELEASE_FLAGS}" CACHE INTERNAL "Last automatically selected flags")
    endif()
    set(CMAKE_CXX_FLAGS_RELEASE "${FINDHAP_RELEASE_FLAGS}")
endif()
if(FINDHAP_QUALIFIED_APPLE_M4)
    add_compile_definitions(FINDHAP_NATIVE_APPLE_M4PRO=1)
endif()

# C++17 support alone does not guarantee a linked floating-point overload.
function(findhap_probe_float_format)
    include(CheckCXXSourceCompiles)
    set(CMAKE_TRY_COMPILE_TARGET_TYPE EXECUTABLE)
    check_cxx_source_compiles([=[
        #include <charconv>
        int main(int argc, char**) {
            char digits[16];
            auto result = std::to_chars(digits, digits + sizeof digits,
                static_cast<double>(argc), std::chars_format::fixed, 5);
            return static_cast<int>(result.ec);
        }
    ]=] FINDHAP_HAS_FLOAT_TO_CHARS)
endfunction()
findhap_probe_float_format()
if(FINDHAP_HAS_FLOAT_TO_CHARS)
    add_compile_definitions(FINDHAP_HAS_FLOAT_TO_CHARS=1)
else()
    add_compile_definitions(FINDHAP_HAS_FLOAT_TO_CHARS=0)
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
option(FINDHAP_MODERN_STDOUT_DEFAULT "Use modern STDOUT by default; public source packages must set this OFF" OFF)
if(FINDHAP_MODERN_STDOUT_DEFAULT)
    add_compile_definitions(FINDHAP_MODERN_STDOUT_DEFAULT=1)
else()
    add_compile_definitions(FINDHAP_MODERN_STDOUT_DEFAULT=0)
endif()
if(FINDHAP_MMAP_PHASE_BUFFERS)
    if(NOT UNIX)
        message(FATAL_ERROR "FINDHAP_MMAP_PHASE_BUFFERS requires POSIX anonymous mappings")
    endif()
    add_compile_definitions(FINDHAP_MMAP_PHASE_BUFFERS=1)
endif()
if(FINDHAP_MAC_MEMORY_DEFAULTS)
    # One directory-level marker reaches the launcher and all four engines.
    # Individual feature overrides win.
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
include(cmake/NativeProfile.cmake)
get_directory_property(turbohap_native_definitions COMPILE_DEFINITIONS)
# Google Axion and Jetson Orin share matching, storage and output settings.
if("FINDHAP_NATIVE_NEOVERSE_V2=1" IN_LIST turbohap_native_definitions OR
        "FINDHAP_NATIVE_NEOVERSE_N3=1" IN_LIST turbohap_native_definitions OR
        "FINDHAP_NATIVE_TEGRA234=1" IN_LIST turbohap_native_definitions)
    add_compile_definitions(
        FINDHAP_LIBRARY_METADATA=1
        FINDHAP_LIBRARY_ACTIVE_BOUND=1
        FINDHAP_PEDIGREE_VECTOR_BASE=1
        FINDHAP_PEDIGREE_NEON_NEAR=1
        FINDHAP_PEDIGREE_EARLY_REJECT=1
        FINDHAP_PEDIGREE_MASK_BACKEND=4
        FINDHAP_POP_VECTOR_SEGMENT=1
        FINDHAP_POP_COMPRESS_SIGNATURE=1
        FINDHAP_POP_FUSED_VALIDITY=1
        FINDHAP_ORDERED_SCAN_ORDER=2
        FINDHAP_PROGENY_SEGMENT_OWNED=1
        FINDHAP_PEDIGREE_REMATCH=2
        FINDHAP_LM_ADAPTIVE_POSITIONS=1
        FINDHAP_PACKED_INHERITANCE=1
        FINDHAP_NHAP_STRIDE=16
        FINDHAP_AXION_STD_SNAPSHOT=1
        FINDHAP_AXION_STD_SUGGESTIONS=1
        FINDHAP_LM_WRITEBACK_BATCH_BYTES=134217728
        FINDHAP_PARALLEL_ASSIGNMENT_COUNTS=1
        FINDHAP_PARALLEL_ASSIGNMENT_OUTPUT=1
        FINDHAP_ASYNC_DENSE_OUTPUT=1
        FINDHAP_OVERLAP_CROSSOVERS=1
        FINDHAP_GROUP_HAPFOUND=1
    )
endif()
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
set(turbohap_configurations Debug Release RelWithDebInfo MinSizeRel ${CMAKE_BUILD_TYPE} ${CMAKE_CONFIGURATION_TYPES})
list(REMOVE_DUPLICATES turbohap_configurations)
foreach(config IN LISTS turbohap_configurations)
    string(TOUPPER "${config}" key)
    set(actual_flags "${CMAKE_CXX_FLAGS} ${CMAKE_CXX_FLAGS_${key}}")
    string(REPLACE "\\" "\\\\" actual_flags "${actual_flags}")
    string(REPLACE "\"" "\\\"" actual_flags "${actual_flags}")
    string(APPEND turbohap_actual_flags "$<$<CONFIG:${config}>:${actual_flags}>")
    set(config_native false)
    if(turbohap_can_probe_host AND (actual_flags MATCHES "(^| )-(march|mcpu)=native( |$)" OR
            (turbohap_apple_cpu AND actual_flags MATCHES "(^| )-mcpu=${turbohap_apple_cpu}( |$)") OR
            (turbohap_host_model STREQUAL "orin" AND actual_flags MATCHES "(^| )-mcpu=cortex-a78ae( |$)")))
        set(config_native true)
    endif()
    string(APPEND turbohap_actual_native "$<$<CONFIG:${config}>:${config_native}>")
endforeach()
file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/generated/$<CONFIG>/TurboHapConfigIdentity.hpp" CONTENT
"#pragma once\nnamespace turbohap_build {\ninline constexpr const char* flags = \"${turbohap_actual_flags}\";\ninline constexpr bool native = ${turbohap_actual_native};\n}\n")
get_directory_property(TURBOHAP_COMPILE_DEFINITIONS COMPILE_DEFINITIONS)
foreach(identity_field turbohap_host_identity turbohap_host_features turbohap_profile_reason turbohap_recipe_reason TURBOHAP_VERSION_LABEL TURBOHAP_OPENMP_NAMES TURBOHAP_COMPILE_DEFINITIONS)
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
foreach(engine standard low-memory dynamic-standard dynamic-low-memory)
    string(REPLACE "-" "_" symbol "${engine}")
    add_library(policy-${engine} OBJECT src/report_kernel_policy.cpp)
    get_target_property(definitions findhap-${engine}-engine COMPILE_DEFINITIONS)
    target_compile_definitions(policy-${engine} PRIVATE ${definitions} TURBOHAP_REPORT_POLICY=report_${symbol})
    target_link_libraries(policy-${engine} PRIVATE OpenMP::OpenMP_CXX)
    target_sources(turbohap-build-info PRIVATE $<TARGET_OBJECTS:policy-${engine}>)
endforeach()
target_include_directories(turbohap-build-info PRIVATE "${CMAKE_BINARY_DIR}/generated" "${CMAKE_BINARY_DIR}/generated/$<CONFIG>")
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
    DESTINATION share/doc/turbohap)
install(DIRECTORY docs examples DESTINATION share/doc/turbohap PATTERN "__pycache__" EXCLUDE)

option(BUILD_TESTING "Register the bundled installation check with CTest" ON)
if(BUILD_TESTING)
    enable_testing()
    find_package(Python3 3.8 QUIET COMPONENTS Interpreter)
    if(Python3_Interpreter_FOUND)
        add_test(NAME installation_example
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/examples/check_installation.py"
                --binary $<TARGET_FILE:findhap>)
    else()
        message(STATUS "Python 3.8 or newer is needed to run the optional installation check")
    endif()
endif()
