// Configure-time probe only. Keep this executable independent of the inference engines.
#include <iostream>
#include <string>
#include "CpuIdentity.hpp"
#if defined(__linux__) && defined(__x86_64__)
#include <cpuid.h>
#elif defined(__linux__) && defined(__aarch64__)
#include <sys/auxv.h>
#include <asm/hwcap.h>
#include <fstream>
#include <set>
#elif defined(__APPLE__) && defined(__aarch64__)
#include <sys/sysctl.h>
#endif

int main() {
    std::string model = "unknown", identity = "unknown", features;
#if defined(__linux__) && defined(__x86_64__)
    unsigned a, b, c, d;
    char vendor[13] = {};
    if (!__get_cpuid(0, &a, &b, &c, &d)) return 1;
    std::memcpy(vendor, &b, 4); std::memcpy(vendor + 4, &d, 4); std::memcpy(vendor + 8, &c, 4);
    if (!__get_cpuid(1, &a, &b, &c, &d)) return 1;
    model = turbohap_probe::x86_model(vendor, a, identity);
    // CPUID alone cannot establish OS support for YMM, opmask and ZMM state.
    unsigned state = 0;
    if ((c & bit_OSXSAVE) && (c & bit_AVX)) {
        unsigned high;
        __asm__("xgetbv" : "=a"(state), "=d"(high) : "c"(0));
    }
    if (__get_cpuid_count(7, 0, &a, &b, &c, &d)) {
        features = turbohap_probe::x86_features(state, b, c, d);
    }
#elif defined(__linux__) && defined(__aarch64__)
    if (getauxval(AT_HWCAP) & HWCAP_ASIMD) features += " neon";
    std::string product, vendor, line;
    std::getline(std::ifstream("/sys/class/dmi/id/product_name"), product);
    std::getline(std::ifstream("/sys/class/dmi/id/sys_vendor"), vendor);
    // On a heterogeneous processor, one core's MIDR cannot identify the machine.
    std::ifstream cpuinfo("/proc/cpuinfo");
    std::set<std::string> parts, vendors;
    while (std::getline(cpuinfo, line)) {
        const auto value = line.find("0x");
        if (value == std::string::npos) continue;
        if (line.compare(0, 8, "CPU part") == 0) parts.insert(line.substr(value));
        if (line.compare(0, 15, "CPU implementer") == 0) vendors.insert(line.substr(value));
    }
    std::ifstream compatible("/sys/firmware/devicetree/base/compatible", std::ios::binary);
    if (!compatible) compatible.open("/proc/device-tree/compatible", std::ios::binary);
    if (product == "NVIDIA_DGX_Spark") model = "gb10";
    else if (turbohap_probe::is_tegra234(compatible)) model = "orin";
    else if (vendors == std::set<std::string>{"0x41"} &&
             (vendor.find("Google") != std::string::npos || product.find("Google") != std::string::npos)) {
        if (parts == std::set<std::string>{"0xd4f"}) model = "axion";
        if (parts == std::set<std::string>{"0xd8e"}) model = "axion-n3";
    }
    identity = product.empty() ? "Linux arm64" : product;
    if (model == "orin") identity = "NVIDIA Tegra234 (Jetson Orin)";
    for (const auto& part : parts) identity += " " + part;
#elif defined(__APPLE__) && defined(__aarch64__)
    char brand[256]{};
    size_t length = sizeof brand;
    if (!sysctlbyname("machdep.cpu.brand_string", brand, &length, nullptr, 0)) identity = brand;
    if (identity == "Apple M4 Pro") model = "m4pro";
    else if (identity.compare(0, 7, "Apple M") == 0) model = "apple-silicon";
    int neon = 0;
    length = sizeof neon;
    if (!sysctlbyname("hw.optional.neon", &neon, &length, nullptr, 0) && neon) features += " neon";
#endif
    std::cout << model << '\n' << identity << '\n' << (features.empty() ? "none" : features.substr(1)) << '\n';
}
