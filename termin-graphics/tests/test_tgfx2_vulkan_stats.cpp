#include "vulkan_stats.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <thread>

int main(int argc, char** argv) {
    if (argc != 2 ||
        (std::strcmp(argv[1], "enabled") != 0 &&
         std::strcmp(argv[1], "disabled") != 0)) {
        std::fprintf(stderr, "usage: %s enabled|disabled\n", argv[0]);
        return 2;
    }

    const bool expected_enabled = std::strcmp(argv[1], "enabled") == 0;
    if (tgfx::vulkan_stats_enabled() != expected_enabled) {
        std::fprintf(stderr, "unexpected TGFX2_VULKAN_STATS gate state\n");
        return 1;
    }

    std::atomic<uint64_t> counter{0};
    tgfx::vulkan_stats_increment(counter, 7);
    if (counter.load(std::memory_order_relaxed) !=
        (expected_enabled ? 7u : 0u)) {
        std::fprintf(stderr, "counter gate did not match expected state\n");
        return 1;
    }

    std::atomic<uint64_t> elapsed_us{0};
    {
        tgfx::VulkanStatsTimer timer(elapsed_us);
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    const uint64_t measured_us = elapsed_us.load(std::memory_order_relaxed);
    if ((expected_enabled && measured_us == 0) ||
        (!expected_enabled && measured_us != 0)) {
        std::fprintf(stderr, "timer gate did not match expected state\n");
        return 1;
    }
    return 0;
}
