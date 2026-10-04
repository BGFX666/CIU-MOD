#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace ciu {

// ARM64 descriptor used by 0x109BCC0..0x109BCE0, not a Windows struct.
struct NativeGlowDesc {
    uintptr_t vtable;
    uint8_t authoritative;
    uint8_t replay;
    uint8_t immediate;
    uint8_t reserved0B[5];
    void* owner;
    uint16_t savedFlags;
    uint8_t reserved1A[6];
};

static_assert(sizeof(uintptr_t) == 8);
static_assert(std::is_standard_layout_v<NativeGlowDesc>);
static_assert(sizeof(NativeGlowDesc) == 0x20);
static_assert(offsetof(NativeGlowDesc, authoritative) == 8);
static_assert(offsetof(NativeGlowDesc, replay) == 9);
static_assert(offsetof(NativeGlowDesc, immediate) == 10);
static_assert(offsetof(NativeGlowDesc, owner) == 0x10);
static_assert(offsetof(NativeGlowDesc, savedFlags) == 0x18);

class LightupRequests {
public:
    static constexpr int kMaxPending = 4;

    bool enqueue() {
        int pending = pending_.load(std::memory_order_relaxed);
        while (pending < kMaxPending) {
            if (pending_.compare_exchange_weak(pending, pending + 1,
                                              std::memory_order_relaxed))
                return true;
        }
        return false;
    }

    bool consume() {
        int pending = pending_.load(std::memory_order_relaxed);
        while (pending > 0) {
            if (pending_.compare_exchange_weak(pending, pending - 1,
                                              std::memory_order_relaxed))
                return true;
        }
        return false;
    }

    int pending() const { return pending_.load(std::memory_order_relaxed); }
    int clear() { return pending_.exchange(0, std::memory_order_relaxed); }

private:
    std::atomic<int> pending_{0};
};

} // namespace ciu
