#include "../src/LightupState.hpp"

#include <array>
#include <cassert>
#include <cstdio>
#include <thread>

int main() {
    ciu::NativeGlowDesc desc{};
    assert(sizeof(desc) == 0x20);
    assert(alignof(decltype(desc)) == 8);
    assert(desc.vtable == 0 && desc.owner == nullptr && desc.savedFlags == 0);
    desc.authoritative = 1;
    desc.replay = 1;
    const auto* bytes = reinterpret_cast<const uint8_t*>(&desc);
    assert(bytes[8] == 1 && bytes[9] == 1 && bytes[10] == 0);

    ciu::LightupRequests queue;
    assert(queue.pending() == 0 && !queue.consume());
    for (int i = 0; i < ciu::LightupRequests::kMaxPending; ++i) assert(queue.enqueue());
    assert(!queue.enqueue() && queue.pending() == 4);
    assert(queue.consume() && queue.pending() == 3);
    assert(queue.enqueue() && queue.pending() == 4);
    assert(queue.clear() == 4 && queue.pending() == 0);

    constexpr int count = 5000;
    std::atomic<int> produced{0};
    std::atomic<int> consumed{0};
    std::array<std::thread, 4> producers;
    std::array<std::thread, 2> consumers;
    for (auto& producer : producers) {
        producer = std::thread([&] {
            for (int i = 0; i < count; ++i) {
                while (!queue.enqueue()) std::this_thread::yield();
                produced.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }
    for (auto& consumer : consumers) {
        consumer = std::thread([&] {
            while (consumed.load(std::memory_order_relaxed) < count * 4) {
                if (queue.consume()) consumed.fetch_add(1, std::memory_order_relaxed);
                else std::this_thread::yield();
            }
        });
    }
    for (auto& producer : producers) producer.join();
    for (auto& consumer : consumers) consumer.join();
    assert(produced == count * 4 && consumed == count * 4 && queue.pending() == 0);
    std::puts("PASS: descriptor layout, bounded queue, 20000 concurrent requests");
}
