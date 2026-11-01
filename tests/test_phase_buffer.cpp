#include "../src/phase_buffer.hpp"
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <type_traits>

#if FINDHAP_MMAP_PHASE_BUFFERS
struct RecordingMapping {
    static inline int allocated = 0, released = 0;
    static inline size_t last_length = 0;
    static inline void* last_address = nullptr;
    static void* allocate(size_t bytes) noexcept {
        ++allocated;
        last_length = bytes;
        last_address = findhap::detail::PhaseBufferMapping::allocate(bytes);
        assert(last_address != nullptr);
        return last_address;
    }
    static int release(void* address, size_t bytes) noexcept {
        assert(address == last_address && bytes == last_length);
        ++released;
        return findhap::detail::PhaseBufferMapping::release(address, bytes);
    }
};
struct FailedMapping {
    static inline int attempts = 0;
    static void* allocate(size_t) noexcept { ++attempts; return nullptr; }
    static int release(void*, size_t) noexcept { assert(false); return -1; }
};
struct RoundedAllocationFailure {
    static inline int attempts = 0;
    static inline size_t first_count = 0, second_count = 0;
    static std::unique_ptr<uint32_t[]> allocate(size_t count) {
        if (++attempts == 1) { first_count = count; throw std::bad_alloc(); }
        second_count = count;
        return std::unique_ptr<uint32_t[]>(new uint32_t[count]);
    }
};
#endif

int main() {
    using findhap::make_phase_buffer;
    using findhap::PhaseBuffer;
    static_assert(!std::is_copy_constructible<PhaseBuffer<uint32_t>>::value);
    static_assert(std::is_nothrow_move_constructible<PhaseBuffer<uint32_t>>::value);
    static_assert(std::is_nothrow_move_assignable<PhaseBuffer<uint32_t>>::value);
#if FINDHAP_MMAP_PHASE_BUFFERS
    static_assert(sizeof(PhaseBuffer<uint32_t>) == sizeof(std::unique_ptr<uint32_t[]>),
                  "mapped ownership must not widen hot workspace members");
#endif
#if !FINDHAP_MMAP_PHASE_BUFFERS
    static_assert(std::is_same<PhaseBuffer<uint32_t>, std::unique_ptr<uint32_t[]>>::value,
                  "disabled opt-in must retain the native owner");
#endif
    PhaseBuffer<uint32_t> empty;
    assert(!empty && empty.get() == nullptr);
    empty.reset();
    const size_t boundary = findhap::phase_buffer_mmap_min_bytes / sizeof(uint32_t);
    for (size_t count : {size_t(0), size_t(1), boundary - 1, boundary, boundary + 1, boundary * 3 + 7}) {
        auto original = make_phase_buffer<uint32_t>(count);
        for (size_t i = 0; i < count; ++i) original[i] = uint32_t(i * 971 + count);
#if FINDHAP_MMAP_PHASE_BUFFERS
        assert(original.uses_mapping() == (count >= boundary));
        assert(original.capacity() == count);
#endif
        auto* address = original.get();
        auto moved = std::move(original);
        assert(!original && moved.get() == address);
        auto replacement = make_phase_buffer<uint32_t>(count + 1);
        replacement = std::move(moved);
        assert(!moved && replacement.get() == address);
        auto* self = &replacement;
        replacement = std::move(*self);
        for (size_t i = 0; i < count; ++i) assert(replacement[i] == uint32_t(i * 971 + count));
        replacement.reset();
        replacement.reset();
        assert(!replacement);
    }
#if FINDHAP_MMAP_PHASE_BUFFERS
    auto retained = make_phase_buffer<uint32_t>(boundary);
    retained[0] = 157;
    auto* before = retained.get();
    bool rejected = false;
    try { retained = make_phase_buffer<uint32_t>(std::numeric_limits<size_t>::max() / sizeof(uint32_t) + 1); }
    catch (const std::bad_alloc&) { rejected = true; }
    assert(rejected && retained.get() == before && retained[0] == 157);
    rejected = false;
    try { (void)make_phase_buffer<uint8_t>(size_t(std::numeric_limits<std::ptrdiff_t>::max()) + 1); }
    catch (const std::bad_alloc&) { rejected = true; }
    assert(rejected);
    {
        findhap::detail::MappedPhaseBuffer<uint32_t, RecordingMapping> owned(boundary + 1);
        const size_t prefix = reinterpret_cast<uintptr_t>(owned.get()) -
                              reinterpret_cast<uintptr_t>(RecordingMapping::last_address);
        assert(prefix > 0 && prefix % alignof(uint32_t) == 0);
        assert(RecordingMapping::last_length == prefix + (boundary + 1) * sizeof(uint32_t));
        auto transferred = std::move(owned);
        assert(!owned && transferred.uses_mapping());
        transferred[boundary] = 19;
        assert(transferred[boundary] == 19);
    }
    assert(RecordingMapping::allocated == 1 && RecordingMapping::released == 1);
    {
        findhap::NativePhaseBufferPool<uint32_t> pool(4096);
        auto first = pool.acquire(boundary + 1);
        auto* address = first.get();
        const size_t capacity = pool.leased_capacity();
        first[boundary] = 173;
        pool.recycle(std::move(first));
        assert(!first && pool.spare_capacity() == capacity);
        auto second = pool.acquire(boundary);
        assert(second.get() == address && second[boundary] == 173);
        pool.recycle(std::move(second));
        auto grown = pool.acquire(capacity + 1);
        assert(pool.leased_capacity() >= capacity + 1 && pool.leased_capacity() < capacity + 1 + 4096);
        grown[capacity] = 219;
        pool.recycle(std::move(grown));
        assert(!grown);
        auto zero = pool.acquire(0);
        assert(pool.leased_capacity() == 0);
        pool.recycle(std::move(zero));
        auto leased = pool.acquire(1);
        bool double_lease = false;
        try { (void)pool.acquire(1); }
        catch (const std::logic_error&) { double_lease = true; }
        assert(double_lease);
        pool.recycle(std::move(leased));
        bool rejected = false;
        try { (void)pool.acquire(std::numeric_limits<size_t>::max()); }
        catch (const std::bad_alloc&) { rejected = true; }
        assert(rejected);
    }
    {
        findhap::NativePhaseBufferPool<uint32_t, RoundedAllocationFailure> pool(4096);
        auto exact = pool.acquire(boundary + 1);
        assert(RoundedAllocationFailure::attempts == 2 &&
               RoundedAllocationFailure::first_count > boundary + 1 &&
               RoundedAllocationFailure::second_count == boundary + 1 &&
               pool.leased_capacity() == boundary + 1);
        exact[boundary] = 251;
        pool.recycle(std::move(exact));
        auto reused = pool.acquire(boundary + 1);
        assert(reused[boundary] == 251 && RoundedAllocationFailure::attempts == 2);
        pool.recycle(std::move(reused));
    }
    {
        findhap::detail::MappedPhaseBuffer<uint32_t, FailedMapping> fallback(boundary + 1);
        assert(fallback && !fallback.uses_mapping() && FailedMapping::attempts == 1);
        fallback[0] = 31; fallback[boundary] = 47;
        assert(fallback[0] == 31 && fallback[boundary] == 47);
        auto transferred = std::move(fallback);
        assert(!fallback && transferred[boundary] == 47);
    }
#endif
#if FINDHAP_MMAP_PHASE_BUFFERS && FINDHAP_PHASE_LM_WORKER_REUSE
    {
        findhap::NativePhaseBufferBank<uint32_t> bank(4096);
        auto& first_pool = bank.at(0);
        auto first = first_pool.acquire(17);
        auto* address = first.get();
        first[16] = 193;
        // Growing the slot vector must not invalidate a live worker's pool.
        auto& second_pool = bank.at(7);
        auto second = second_pool.acquire(33);
        assert(second.get() != address && bank.pool_count() == 8);
        first_pool.recycle(std::move(first));
        second_pool.recycle(std::move(second));
        auto again = bank.at(0).acquire(16);
        assert(again.get() == address && again[16] == 193);
        bank.at(0).recycle(std::move(again));
        bool rejected = false;
        try { (void)bank.at(std::numeric_limits<size_t>::max()); }
        catch (const std::length_error&) { rejected = true; }
        assert(rejected);
    }
#endif
#if FINDHAP_MMAP_PHASE_BUFFERS && FINDHAP_PHASE_STD_SNAPSHOT_REUSE > 0
    {
        using Native = findhap::ReusableSnapshotBuffer<uint32_t, false,
            findhap::detail::SnapshotPhaseAllocation<uint32_t, false>, 16384>;
        static_assert(!std::is_copy_constructible<Native>::value);
        static_assert(std::is_nothrow_move_constructible<Native>::value);
        Native buffer;
        assert(!buffer && buffer.capacity() == 0);
        buffer.prepare(0);
        assert(buffer && buffer.capacity() == 0);
        buffer.prepare(17);
        assert(buffer.capacity() == 4096);
        auto* address = buffer.get();
        buffer[16] = 241;
        buffer.prepare(16);
        assert(buffer.get() == address && buffer[16] == 241);
        bool rejected = false;
        try { buffer.prepare(std::numeric_limits<size_t>::max()); }
        catch (const std::bad_alloc&) { rejected = true; }
        assert(rejected && buffer.get() == address && buffer[16] == 241);
        auto moved = std::move(buffer);
        assert(!buffer && buffer.capacity() == 0 && moved[16] == 241);
        moved.prepare(4097);
        assert(moved.capacity() == 8192);
        moved[4096] = 211;
        assert(moved[4096] == 211);
        moved.reset(); moved.reset();
        assert(!moved && moved.capacity() == 0);
    }
    {
        using Mapped = findhap::ReusableSnapshotBuffer<uint32_t, true,
            findhap::detail::SnapshotPhaseAllocation<uint32_t, true>, 16384>;
        Mapped buffer;
        buffer.prepare(boundary + 1);
        auto* address = buffer.get();
        buffer[boundary] = 197;
        buffer.prepare(1);
        assert(buffer.get() == address && buffer[boundary] == 197);
        auto moved = std::move(buffer);
        assert(!buffer && buffer.capacity() == 0 && moved[boundary] == 197);
    }
    {
        struct FallbackAllocation {
            using Owner = std::unique_ptr<uint32_t[]>;
            static Owner allocate(size_t count) {
                if (count > 17) throw std::bad_alloc();
                return Owner(new uint32_t[count]);
            }
        };
        findhap::ReusableSnapshotBuffer<uint32_t, false, FallbackAllocation, 16384> buffer;
        buffer.prepare(17); // rounded allocation fails; exact requirement fits
        assert(buffer.capacity() == 17);
        buffer[16] = 251;
        buffer.prepare(16);
        assert(buffer.capacity() == 17 && buffer[16] == 251);
    }
#endif
    struct alignas(std::max_align_t) AlignedRecord { uint64_t first, second; };
    static_assert(std::is_trivial<AlignedRecord>::value);
    for (size_t count : {size_t(1), boundary + 1}) {
        auto aligned = make_phase_buffer<AlignedRecord>(count);
        assert(reinterpret_cast<uintptr_t>(aligned.get()) % alignof(AlignedRecord) == 0);
        for (size_t i = 0; i < count; ++i) aligned[i] = {uint64_t(i * 71), uint64_t(i + 83)};
        for (size_t i = 0; i < count; ++i) assert(aligned[i].first == i * 71 && aligned[i].second == i + 83);
    }
    std::puts("phase buffer ownership, boundaries, release and allocation fallback passed");
}
