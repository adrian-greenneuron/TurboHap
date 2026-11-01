#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include "platform_policy.hpp"
#if FINDHAP_HAPLIST_INIT == 3
#include <new>
#include <sys/mman.h>
#endif
#if FINDHAP_HAPLIST_ASAN
#include <sanitizer/asan_interface.h>
static_assert(FINDHAP_HAPLIST_INIT == 1 || FINDHAP_HAPLIST_INIT == 3, "Logical ASan poisoning requires lazy initialization");
#endif
namespace findhap_haplotype_init {
#if FINDHAP_HAPLIST_INIT == 3
// Explicit mapping lifetime prevents resident pages from accumulating across
// different segment strides. The previous-step snapshot is complete first.
class MappedLibrary {
    int8_t* data_ = nullptr; size_t bytes_ = 0;
    void release() {
        if (!data_) return;
#if FINDHAP_HAPLIST_ASAN
        __asan_unpoison_memory_region(data_, bytes_);
#endif
        munmap(data_, bytes_); data_ = nullptr; bytes_ = 0;
    }
public:
    MappedLibrary() = default;
    MappedLibrary(const MappedLibrary&) = delete;
    MappedLibrary& operator=(const MappedLibrary&) = delete;
    ~MappedLibrary() { release(); }
    void allocate(size_t bytes) {
        release();
        if (!bytes) return;
        void* memory = mmap(nullptr, bytes, PROT_READ | PROT_WRITE,
                            MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (memory == MAP_FAILED) throw std::bad_alloc();
        data_ = static_cast<int8_t*>(memory); bytes_ = bytes;
    }
    int8_t* get() const { return data_; }
    int8_t& operator[](size_t offset) const { return data_[offset]; }
};
#endif
inline void begin_step(int8_t* library, size_t allocated_bytes) {
#if FINDHAP_HAPLIST_ASAN
    // Called after the previous-step snapshot. Unpublished rows remain poisoned
    // until the engine initializes their complete stride before any lookup.
    __asan_poison_memory_region(library, allocated_bytes);
#else
    (void)library; (void)allocated_bytes;
#endif
}
inline void clear_capacity(int8_t* segment, size_t bytes) {
#if FINDHAP_HAPLIST_INIT == 0
    std::memset(segment, 0, bytes);
#elif FINDHAP_HAPLIST_INIT == 2
    std::memset(segment, 0xa5, bytes);
#else
    (void)segment; (void)bytes;
#endif
}
inline void publish_row(int8_t* row, size_t stride) {
#if FINDHAP_HAPLIST_ASAN
    __asan_unpoison_memory_region(row, stride);
#else
    (void)row; (void)stride;
#endif
}
}
