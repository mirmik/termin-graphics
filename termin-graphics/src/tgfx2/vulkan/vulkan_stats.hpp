#pragma once

#ifdef TGFX2_HAS_VULKAN

#include <atomic>
#include <chrono>
#include <cstdint>

namespace tgfx {

// Process-wide diagnostics gate. The environment is sampled once so disabled
// instrumentation costs only an inline load/branch and never performs an
// atomic RMW or reads the clock in a backend hot path.
extern const bool g_vulkan_stats_enabled;

inline bool vulkan_stats_enabled() noexcept {
    return g_vulkan_stats_enabled;
}

inline void vulkan_stats_increment(std::atomic<uint64_t>& counter,
                                   uint64_t value = 1) {
    if (vulkan_stats_enabled()) {
        counter.fetch_add(value, std::memory_order_relaxed);
    }
}

class VulkanStatsTimer {
public:
    explicit VulkanStatsTimer(std::atomic<uint64_t>& counter) {
        if (vulkan_stats_enabled()) {
            counter_ = &counter;
            start_ = std::chrono::steady_clock::now();
        }
    }

    ~VulkanStatsTimer() {
        if (!counter_) return;
        counter_->fetch_add(
            std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now() - start_).count(),
            std::memory_order_relaxed);
    }

    VulkanStatsTimer(const VulkanStatsTimer&) = delete;
    VulkanStatsTimer& operator=(const VulkanStatsTimer&) = delete;

private:
    std::atomic<uint64_t>* counter_ = nullptr;
    std::chrono::steady_clock::time_point start_{};
};

extern std::atomic<uint64_t> g_resource_set_count;
extern std::atomic<uint64_t> g_pipeline_count;
extern std::atomic<uint64_t> g_pipeline_cache_hit_count;
extern std::atomic<uint64_t> g_pipeline_cache_miss_count;
extern std::atomic<uint64_t> g_pipeline_cache_unique_vertex_layout_count;
extern std::atomic<uint64_t> g_shader_count;
extern std::atomic<uint64_t> g_shader_compile_us;
extern std::atomic<uint64_t> g_shader_reflect_us;
extern std::atomic<uint64_t> g_shader_module_us;
extern std::atomic<uint64_t> g_pipeline_renderpass_us;
extern std::atomic<uint64_t> g_pipeline_create_us;

extern std::atomic<uint64_t> g_draw_count;
extern std::atomic<uint64_t> g_bind_pipeline_count;
extern std::atomic<uint64_t> g_bind_rset_count;
extern std::atomic<uint64_t> g_bind_vbo_count;
extern std::atomic<uint64_t> g_bind_ibo_count;
extern std::atomic<uint64_t> g_push_constants_count;
extern std::atomic<uint64_t> g_record_us;
extern std::atomic<uint64_t> g_submit_us;

} // namespace tgfx

#endif // TGFX2_HAS_VULKAN
