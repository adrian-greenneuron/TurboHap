#pragma once

#include <cstddef>
#include <limits>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>

#ifndef FINDHAP_MMAP_PHASE_BUFFERS
#define FINDHAP_MMAP_PHASE_BUFFERS 0
#endif
#ifndef FINDHAP_MAC_MEMORY_DEFAULTS
#define FINDHAP_MAC_MEMORY_DEFAULTS 0
#endif

// Independent LM choices under the outer memory option. Definitions are common
// to every engine translation unit; no shared class changes with engine mode.
#ifndef FINDHAP_PHASE_LM_NATIVE_CACHE
#if FINDHAP_MAC_MEMORY_DEFAULTS && FINDHAP_MMAP_PHASE_BUFFERS
#define FINDHAP_PHASE_LM_NATIVE_CACHE 0
#else
#define FINDHAP_PHASE_LM_NATIVE_CACHE 1
#endif
#endif
#ifndef FINDHAP_PHASE_LM_RETAIN_CACHE
#define FINDHAP_PHASE_LM_RETAIN_CACHE FINDHAP_PHASE_LM_NATIVE_CACHE
#endif
#ifndef FINDHAP_PHASE_LM_RESERVE
#define FINDHAP_PHASE_LM_RESERVE 1
#endif
#ifndef FINDHAP_PHASE_LM_WORKER_REUSE
#if FINDHAP_MAC_MEMORY_DEFAULTS && FINDHAP_MMAP_PHASE_BUFFERS
#define FINDHAP_PHASE_LM_WORKER_REUSE 1
#else
#define FINDHAP_PHASE_LM_WORKER_REUSE 0
#endif
#endif
#ifndef FINDHAP_PHASE_LM_WORKER_MAPPING
#define FINDHAP_PHASE_LM_WORKER_MAPPING 0
#endif
#ifndef FINDHAP_PHASE_STD_COMPACT_WORKSPACE
#if FINDHAP_MAC_MEMORY_DEFAULTS && FINDHAP_MMAP_PHASE_BUFFERS
#define FINDHAP_PHASE_STD_COMPACT_WORKSPACE 1
#else
#define FINDHAP_PHASE_STD_COMPACT_WORKSPACE 0
#endif
#endif
#ifndef FINDHAP_PHASE_STD_SNAPSHOT_REUSE
#define FINDHAP_PHASE_STD_SNAPSHOT_REUSE 0
#endif
#if FINDHAP_PHASE_STD_SNAPSHOT_REUSE < 0 || FINDHAP_PHASE_STD_SNAPSHOT_REUSE > 2
#error "Standard snapshot reuse must be 0 (existing), 1 (native) or 2 (mapped)"
#endif
#if FINDHAP_MMAP_PHASE_BUFFERS && FINDHAP_PHASE_LM_WORKER_REUSE && FINDHAP_PHASE_LM_WORKER_MAPPING
#error "Choose native worker reuse or mapped population workers for this experiment"
#endif

#if FINDHAP_MMAP_PHASE_BUFFERS
#include <algorithm>
#include <cstdint>
#include <exception>
#include <stdexcept>
#include <sys/mman.h>
#if FINDHAP_PHASE_LM_WORKER_REUSE
#include <vector>
#endif
#endif

namespace findhap {
// Only explicitly selected phase buffers use this owner. Small allocations and
// hot population-worker buffers retain the native allocator. With the opt-in
// disabled the owner is exactly the original unique_ptr<T[]> type.
inline constexpr size_t phase_buffer_mmap_min_bytes = 64 * 1024;

#if FINDHAP_MMAP_PHASE_BUFFERS
namespace detail {
#if defined(__GNUC__) || defined(__clang__)
#define FINDHAP_PHASE_FRESH __attribute__((malloc, alloc_size(1), noinline))
#define FINDHAP_PHASE_INLINE __attribute__((always_inline))
#else
#define FINDHAP_PHASE_FRESH
#define FINDHAP_PHASE_INLINE
#endif
struct PhaseBufferMapping {
    // Anonymous, non-fixed mappings cannot alias an existing live object.
    // Expose that fresh-storage/extent contract just as array-new does. Keep
    // the allocator call separate so inlining mmap cannot erase its contract.
    FINDHAP_PHASE_FRESH static void* allocate(size_t bytes) noexcept {
        void* address = mmap(nullptr, bytes, PROT_READ | PROT_WRITE,
                             MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        return address == MAP_FAILED ? nullptr : address;
    }
    static int release(void* address, size_t bytes) noexcept {
        return munmap(address, bytes);
    }
};

// Mapping is a template policy so allocation failure and exact release can be
// tested without changing process-wide allocator or address-space limits.
// Optional compiler-contract experiment. The default owner path is unchanged.
#if defined(FINDHAP_PHASE_ASSUME_ALIGNED) && FINDHAP_PHASE_ASSUME_ALIGNED && \
    (defined(__GNUC__) || defined(__clang__))
#define FINDHAP_PHASE_DATA(pointer) static_cast<T*>(__builtin_assume_aligned(pointer, alignof(std::max_align_t)))
#else
#define FINDHAP_PHASE_DATA(pointer) pointer
#endif
template<class T, class Mapping = PhaseBufferMapping>
class MappedPhaseBuffer {
    static_assert(std::is_trivial<T>::value && !std::is_const<T>::value &&
                  !std::is_volatile<T>::value, "phase buffers require trivial mutable elements");
    static_assert(alignof(T) <= alignof(std::max_align_t), "over-aligned phase buffers are unsupported");
    // Keep the owner pointer-sized, like the native unique_ptr. Allocation
    // metadata travels with storage instead of widening every hot workspace
    // and every previous-snapshot/output owner in the engine stack frame.
    struct Header { size_t count, bytes; bool mapped; };
    static constexpr size_t prefix_bytes = 128;
#if defined(FINDHAP_PHASE_ASSUME_ALIGNED) && FINDHAP_PHASE_ASSUME_ALIGNED
    static_assert(prefix_bytes % alignof(std::max_align_t) == 0,
                  "alignment contract requires a suitably aligned prefix");
#endif
    static_assert(sizeof(Header) <= prefix_bytes && prefix_bytes % alignof(T) == 0);
    T* data_ = nullptr;
    Header* header() const noexcept {
        return reinterpret_cast<Header*>(reinterpret_cast<std::uintptr_t>(data_) - prefix_bytes);
    }

public:
    MappedPhaseBuffer() noexcept = default;
    FINDHAP_PHASE_INLINE explicit MappedPhaseBuffer(size_t count) {
        if (count > (size_t(std::numeric_limits<std::ptrdiff_t>::max()) - prefix_bytes) / sizeof(T))
            throw std::bad_array_new_length();
        const size_t payload_bytes = count * sizeof(T), bytes = payload_bytes + prefix_bytes;
        void* memory = nullptr;
        if (payload_bytes >= phase_buffer_mmap_min_bytes) memory = Mapping::allocate(bytes);
        const bool mapped = memory != nullptr;
        if (!mapped) memory = new unsigned char[bytes];
        ::new (memory) Header{count, bytes, mapped};
        // Trivial payload is deliberately not value-initialized. Its bytes are
        // filled by the same application code as in the native implementation.
        data_ = ::new (static_cast<unsigned char*>(memory) + prefix_bytes) T[count];
    }
    MappedPhaseBuffer(const MappedPhaseBuffer&) = delete;
    MappedPhaseBuffer& operator=(const MappedPhaseBuffer&) = delete;
    MappedPhaseBuffer(MappedPhaseBuffer&& other) noexcept { swap(other); }
    MappedPhaseBuffer& operator=(MappedPhaseBuffer&& other) noexcept {
        if (this != &other) { reset(); swap(other); }
        return *this;
    }
    ~MappedPhaseBuffer() { reset(); }
    void swap(MappedPhaseBuffer& other) noexcept { std::swap(data_, other.data_); }
    void reset() noexcept {
        if (data_) {
            Header* h = header();
            if (h->mapped) {
                if (Mapping::release(h, h->bytes) != 0) std::terminate();
            } else delete[] reinterpret_cast<unsigned char*>(h);
            data_ = nullptr;
        }
    }
    size_t capacity() const noexcept { return data_ ? header()->count : 0; }
    T* get() const noexcept { return FINDHAP_PHASE_DATA(data_); }
    T& operator[](size_t offset) const noexcept { return FINDHAP_PHASE_DATA(data_)[offset]; }
    explicit operator bool() const noexcept { return data_ != nullptr; }
    bool uses_mapping() const noexcept { return data_ && header()->mapped; }
};
#undef FINDHAP_PHASE_DATA
#undef FINDHAP_PHASE_FRESH
#undef FINDHAP_PHASE_INLINE
} // namespace detail

template<class T> using PhaseBuffer = detail::MappedPhaseBuffer<T>;
#else
template<class T> using PhaseBuffer = std::unique_ptr<T[]>;
#endif

template<class T>
PhaseBuffer<T> make_phase_buffer(size_t count) {
#if FINDHAP_MMAP_PHASE_BUFFERS
    return PhaseBuffer<T>(count);
#else
    return PhaseBuffer<T>(new T[count]);
#endif
}
#if FINDHAP_MMAP_PHASE_BUFFERS
// Standard matching keeps its original native unique_ptr owner and native
// backing. Cache one allocation across serial steps instead of leaving several
// differently sized genomes in the process allocator. Only the unused tail of
// a rounded allocation may remain untouched; clients overwrite consumed bytes.
namespace detail {
template<class T>
struct NativePhaseAllocation {
    static std::unique_ptr<T[]> allocate(size_t count) {
        return std::unique_ptr<T[]>(new T[count]);
    }
};
} // namespace detail

template<class T, class Allocation = detail::NativePhaseAllocation<T>>
class NativePhaseBufferPool {
    static_assert(std::is_trivial<T>::value, "phase buffers require trivial elements");
    std::unique_ptr<T[]> spare_;
    size_t spare_capacity_ = 0, leased_capacity_ = 0, grain_;
    bool leased_ = false;
public:
    explicit NativePhaseBufferPool(size_t grain_elements = (16 * 1024 * 1024) / sizeof(T))
        : grain_(std::min(size_t(std::numeric_limits<std::ptrdiff_t>::max()) / sizeof(T),
                          std::max(size_t(1), grain_elements))) {}
    NativePhaseBufferPool(const NativePhaseBufferPool&) = delete;
    NativePhaseBufferPool& operator=(const NativePhaseBufferPool&) = delete;
    std::unique_ptr<T[]> acquire(size_t count) {
        if (leased_) throw std::logic_error("phase workspace already leased");
        const size_t limit = size_t(std::numeric_limits<std::ptrdiff_t>::max()) / sizeof(T);
        if (count > limit) throw std::bad_array_new_length();
        std::unique_ptr<T[]> buffer;
        size_t capacity = count;
        if (count && spare_capacity_ >= count) {
            capacity = spare_capacity_;
            buffer = std::move(spare_);
            spare_capacity_ = 0;
        } else {
            if (count) {
                // Release before growing: at most one large workspace is live.
                spare_.reset(); spare_capacity_ = 0;
                if (count <= limit - (grain_ - 1))
                    capacity = (count + grain_ - 1) / grain_ * grain_;
            }
            try { buffer = Allocation::allocate(capacity); }
            catch (const std::bad_alloc&) {
                if (capacity == count) throw;
                // Rounding must not fail an allocation that fits exactly.
                buffer = Allocation::allocate(count); capacity = count;
            }
        }
        leased_capacity_ = capacity; leased_ = true;
        return buffer;
    }
    void recycle(std::unique_ptr<T[]>&& buffer) noexcept {
        if (!leased_) std::terminate();
        if (leased_capacity_ > spare_capacity_) {
            spare_ = std::move(buffer); spare_capacity_ = leased_capacity_;
        } else buffer.reset();
        leased_capacity_ = 0; leased_ = false;
    }
    size_t spare_capacity() const noexcept { return spare_capacity_; }
    size_t leased_capacity() const noexcept { return leased_capacity_; }
};
#if FINDHAP_PHASE_STD_SNAPSHOT_REUSE > 0
namespace detail {
template<class T, bool Mapped>
struct SnapshotPhaseAllocation {
    using Owner = std::conditional_t<Mapped, PhaseBuffer<T>, std::unique_ptr<T[]>>;
    static Owner allocate(size_t count) {
        if constexpr (Mapped) return make_phase_buffer<T>(count);
        else return Owner(new T[count]);
    }
};
} // namespace detail

// Only completed previous-step snapshots reuse this storage. prepare() may
// discard old contents; clients reconstruct every consumed byte before use.
// Rounded spare capacity is virtual storage, not a physical-residency query.
template<class T, bool Mapped, class Allocation = detail::SnapshotPhaseAllocation<T, Mapped>,
         size_t GrainBytes = 64 * 1024 * 1024>
class ReusableSnapshotBuffer {
    static_assert(std::is_trivial<T>::value && !std::is_const<T>::value &&
                  !std::is_volatile<T>::value, "snapshots require trivial mutable elements");
    typename Allocation::Owner data_;
    size_t capacity_ = 0;
    static constexpr size_t limit = size_t(std::numeric_limits<std::ptrdiff_t>::max()) / sizeof(T);
    static constexpr size_t grain = std::min(limit, std::max(size_t(1), GrainBytes / sizeof(T)));
public:
    ReusableSnapshotBuffer() noexcept = default;
    ReusableSnapshotBuffer(const ReusableSnapshotBuffer&) = delete;
    ReusableSnapshotBuffer& operator=(const ReusableSnapshotBuffer&) = delete;
    ReusableSnapshotBuffer(ReusableSnapshotBuffer&&) noexcept = default;
    ReusableSnapshotBuffer& operator=(ReusableSnapshotBuffer&&) noexcept = default;
    void prepare(size_t count) {
        if (count > limit) throw std::bad_array_new_length();
        if (data_ && capacity_ >= count) return;
        data_.reset(); capacity_ = 0;
        size_t capacity = count;
        if (count && count <= limit - (grain - 1)) capacity = (count + grain - 1) / grain * grain;
        try { data_ = Allocation::allocate(capacity); }
        catch (const std::bad_alloc&) {
            if (capacity == count) throw;
            data_ = Allocation::allocate(count); capacity = count;
        }
        capacity_ = capacity;
    }
    void reset() noexcept { data_.reset(); capacity_ = 0; }
    size_t capacity() const noexcept { return data_ ? capacity_ : 0; }
    T* get() const noexcept { return data_.get(); }
    T& operator[](size_t index) const noexcept { return data_[index]; }
    explicit operator bool() const noexcept { return bool(data_); }
};
#endif

#if FINDHAP_PHASE_LM_WORKER_REUSE
// Access only while preparing/finishing serial population regions. Pool
// objects have stable addresses even when another worker slot is appended.
// Each worker keeps native ownership and the original requested IO extent.
template<class T>
class NativePhaseBufferBank {
    using Pool = NativePhaseBufferPool<T>;
    std::vector<std::unique_ptr<Pool>> pools_;
    size_t grain_;
public:
    explicit NativePhaseBufferBank(size_t grain_elements = (1024 * 1024) / sizeof(T))
        : grain_(grain_elements) {}
    Pool& at(size_t index) {
        if (index >= pools_.max_size()) throw std::length_error("too many phase-buffer workers");
        while (pools_.size() <= index) pools_.push_back(std::make_unique<Pool>(grain_));
        return *pools_[index];
    }
    size_t pool_count() const noexcept { return pools_.size(); }
};
#endif
#endif
} // namespace findhap
