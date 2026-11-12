#define FINDHAP_LIBRARY_METADATA 1
#define FINDHAP_METADATA_CALLOC 1
#include "../src/haplotype_metadata.hpp"
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <limits>
#include <type_traits>

static void require(bool ok) {
    if (!ok) {
        std::fputs("haplotype metadata allocation check failed\n", stderr);
        std::exit(1);
    }
}

int main() {
    using findhap_haplotype_metadata::Array;
    static_assert(sizeof(Array) == sizeof(int*), "Preserve the engine owner's layout");
    static_assert(!std::is_copy_constructible<Array>::value, "Own each allocation once");
    Array data;
    require(data.get() == nullptr);
    // Repeated steps must reset old frequencies and prior-ID remapping slots,
    // including after a changed segment stride or a smaller allocation.
    for (size_t count : {size_t(0), size_t(13), size_t(65535), size_t(65536),
                         size_t(500001), size_t(17), size_t(0)}) {
        data.allocate(count);
        require((data.get() == nullptr) == (count == 0));
        for (size_t i = 0; i < count; ++i) require(data[i] == 0);
        for (size_t i = 0; i < count; ++i) data[i] = int(i % 127) + 1;
    }
    data.allocate(7);
    data[3] = 19;
    bool rejected = false;
    try {
        data.allocate(std::numeric_limits<size_t>::max() / sizeof(int) + 1);
    } catch (const std::bad_array_new_length&) {
        rejected = true;
    }
    require(rejected && data[3] == 19);
}
