#pragma once
#include <iostream>
#include <string>
#if __has_include("TurboHapBuildIdentity.hpp")
#include "TurboHapBuildIdentity.hpp"
#else
namespace turbohap_build {
inline constexpr const char* version = "development-unconfigured";
inline constexpr const char* source_commit = "unknown";
inline constexpr const char* source_sha256 = "unknown";
inline constexpr const char* compiler = "unconfigured fixture";
inline constexpr const char* openmp_libraries = "unconfigured fixture";
inline constexpr const char* flags = "unconfigured fixture";
inline constexpr const char* features = "unconfigured fixture";
inline constexpr const char *host_model = "unknown", *host_identity = "unknown", *host_features = "unknown";
inline constexpr const char *profile_reason = "unconfigured", *recipe_reason = "unconfigured";
inline constexpr const char *target_processor = "", *compiler_target = "", *mac_architectures = "";
inline constexpr const char *deployment_target = "", *sdk = "";
inline constexpr bool native = false;
inline constexpr bool source_is_dirty = true;
}
#endif

void report_standard(std::ostream&);
void report_low_memory(std::ostream&);
void report_dynamic_standard(std::ostream&);
void report_dynamic_low_memory(std::ostream&);

namespace turbohap_build {
inline std::string quoted(const char* value) {
    std::string result = "\"";
    for (const unsigned char c : std::string(value)) {
        if (c == '\\' || c == '"') { result += '\\'; result += char(c); }
        else if (c < 32) result += '?';
        else result += char(c);
    }
    return result + '"';
}
inline void information(std::ostream& out, const char* executable_sha256) {
#if defined(__aarch64__) || defined(__arm64__)
    const char* architecture = "arm64";
#elif defined(__x86_64__)
    const char* architecture = "x86-64";
#else
    const char* architecture = "other";
#endif
#if defined(__AVX512BW__)
    const char* isa = "avx512bw";
#elif defined(__AVX2__)
    const char* isa = "avx2";
#elif defined(__ARM_NEON) || defined(__ARM_NEON__)
    const char* isa = "neon";
#else
    const char* isa = "portable";
#endif
    std::string compiled;
#ifdef __ARM_NEON
    compiled += " neon";
#endif
#ifdef __AVX2__
    compiled += " avx2";
#endif
#ifdef __BMI2__
    compiled += " bmi2";
#endif
#ifdef __AVX512F__
    compiled += " avx512f";
#endif
#ifdef __AVX512BW__
    compiled += " avx512bw";
#endif
#ifdef __AVX512VBMI__
    compiled += " vbmi";
#endif
#ifdef __AVX512VP2INTERSECT__
    compiled += " vp2intersect";
#endif
    out << "{\"version\":" << quoted(version)
        << ",\"source_commit\":" << quoted(source_commit)
        << ",\"source_sha256\":" << quoted(source_sha256)
        << ",\"executable_sha256\":" << quoted(executable_sha256)
        << ",\"compiler\":" << quoted(compiler)
        << ",\"openmp_libraries\":" << quoted(openmp_libraries)
        << ",\"compile_flags\":" << quoted(flags)
        << ",\"compile_definitions\":" << quoted(features)
        << ",\"source_is_dirty\":" << (source_is_dirty ? "true" : "false")
        << ",\"architecture\":" << quoted(architecture)
        << ",\"compiler_isa\":" << quoted(isa)
        << ",\"compiler_simd_features\":" << quoted(compiled.empty() ? "baseline" : compiled.c_str() + 1)
#ifdef _OPENMP
        << ",\"openmp_compile_version\":" << _OPENMP
#endif
        << ",\"native_build\":" << (native ? "true" : "false")
        << ",\"target_processor\":" << quoted(target_processor)
        << ",\"compiler_target\":" << quoted(compiler_target)
        << ",\"mac_architectures\":" << quoted(mac_architectures)
        << ",\"deployment_target\":" << quoted(deployment_target)
        << ",\"sdk\":" << quoted(sdk)
        << ",\"configure_cpu_model\":" << quoted(host_model)
        << ",\"configure_cpu_identity\":" << quoted(host_identity)
        << ",\"configure_os_features_probed\":" << quoted(host_features)
        << ",\"profile_selection\":" << quoted(profile_reason)
        << ",\"recipe_selection\":" << quoted(recipe_reason)
        << ",\"engines\":{\"standard\":";
    report_standard(out);
    out << ",\"lm\":";
    report_low_memory(out);
    out << ",\"dynamic_standard\":";
    report_dynamic_standard(out);
    out << ",\"dynamic_lm\":";
    report_dynamic_low_memory(out);
    out << "},\"cpu_policy\":\"runtime auto; inspect --cpu-info\"}\n";
}
}
