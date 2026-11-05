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
inline constexpr bool source_is_dirty = true;
}
#endif

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
#ifdef _OPENMP
        << ",\"openmp_compile_version\":" << _OPENMP
#endif
        << ",\"native_build\":" << (std::string(flags).find("native") != std::string::npos ? "true" : "false")
        << ",\"cpu_policy\":\"runtime auto; inspect --cpu-info\"}\n";
}
}
