#include "build_information.hpp"
#include "TurboHapExecutableIdentity.hpp"
#include <cstring>

int main(int argc, char** argv) {
    if (argc == 2 && std::strcmp(argv[1], "--version") == 0) {
        std::cout << "TurboHap " << turbohap_build::version << '\n';
        return 0;
    }
    if (argc == 1 || (argc == 2 && std::strcmp(argv[1], "--build-info") == 0)) {
        turbohap_build::information(std::cout, turbohap_build::executable_sha256);
        return 0;
    }
    std::cerr << "Usage: turbohap-build-info [--version|--build-info]\n";
    return 2;
}
