#pragma once
#include <cstring>
#include <istream>
#include <string>

namespace turbohap_probe {
// The Linux device-tree compatible property is a list of NUL-terminated strings.
inline bool is_tegra234(std::istream& compatible) {
    std::string entry;
    while (std::getline(compatible, entry, '\0')) {
        if (!compatible.eof() && entry == "nvidia,tegra234") return true;
    }
    return false;
}

// CPUID family/model and OS-enabled vector state are independent inputs.
inline std::string x86_model(const char* vendor, unsigned a, std::string& identity) {
    std::string model = "unknown";
    const unsigned base = (a >> 8) & 15, stepping = a & 15;
    const unsigned family = base == 15 ? base + ((a >> 20) & 255) : base;
    unsigned number = (a >> 4) & 15;
    if (base == 6 || base == 15) number += (a >> 12) & 0xf0;
    identity = std::string(vendor) + " family " + std::to_string(family) + " model " +
        std::to_string(number) + " stepping " + std::to_string(stepping);
    if (!std::strcmp(vendor, "GenuineIntel") && family == 6) {
        if (number == 207) model = "emerald";
        if (number == 143) model = "sapphire";
        if (number == 106) model = "ice";
        if (number == 173 || number == 174) model = "granite";
        if (number == 85 && stepping >= 5 && stepping <= 7) model = "cascade";
    } else if (!std::strcmp(vendor, "AuthenticAMD")) {
        if (family == 26 && number == 2) model = "turin";
        if (family == 25 && number == 17) model = "genoa";
        if (family == 25 && number == 1) model = "milan";
    }
    return model;
}

inline std::string x86_features(unsigned state, unsigned b, unsigned c, unsigned d) {
    std::string features;
    if (b & (1u << 8)) features += " bmi2";
    if ((state & 6) == 6 && (b & (1u << 5))) features += " avx2";
    if ((state & 0xe6) == 0xe6 && (b & (1u << 16))) {
        features += " avx512f";
        if (b & (1u << 30)) features += " avx512bw";
        if (c & (1u << 1)) features += " vbmi";
        if (d & (1u << 8)) features += " vp2intersect";
    }
    return features;
}
}
