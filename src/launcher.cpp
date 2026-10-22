#include <cstdio>
#include <cstring>

namespace findhap_standard { int run(int argc, char* argv[]); }
namespace findhap_low_memory { int run(int argc, char* argv[]); }

int main(int argc, char* argv[]) {
    bool low_memory = false;
    int count = 1;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--low-memory") == 0) {
            low_memory = true;
        } else {
            argv[count++] = argv[i];
        }
    }
    argv[count] = nullptr;

    if (count > 1 && std::strcmp(argv[1], "--help") == 0) {
        std::puts(
            "TurboHap\n"
            "Usage: findhap [threads] [--low-memory]\n\n"
            "Standard mode prioritizes speed. --low-memory reduces peak memory.\n"
            "The flag may appear before or after the thread count.\n"
            "Without a thread count, use the OpenMP default (OMP_NUM_THREADS).\n"
            "Original findhap.options fields and input/output filenames are unchanged.\n"
            "Other arguments after the thread count are ignored, as in findhapV3.");
        return 0;
    }

    // Select once, before either engine allocates data. There are no mode
    // branches in the processing loops and no subprocess or second executable.
    return low_memory ? findhap_low_memory::run(count, argv)
                      : findhap_standard::run(count, argv);
}
