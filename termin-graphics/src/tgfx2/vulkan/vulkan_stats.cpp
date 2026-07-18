#ifdef TGFX2_HAS_VULKAN

#include "vulkan_stats.hpp"

#include <cstdlib>

namespace tgfx {

const bool g_vulkan_stats_enabled = [] {
#ifdef __ANDROID__
    return true;
#else
    const char* env = std::getenv("TGFX2_VULKAN_STATS");
    return env && env[0] == '1';
#endif
}();

// Vulkan hot-path counters — swept once per second from submit().
std::atomic<uint64_t> g_resource_set_count{0};
std::atomic<uint64_t> g_pipeline_count{0};
std::atomic<uint64_t> g_pipeline_cache_hit_count{0};
std::atomic<uint64_t> g_pipeline_cache_miss_count{0};
std::atomic<uint64_t> g_pipeline_cache_unique_vertex_layout_count{0};
std::atomic<uint64_t> g_shader_count{0};
std::atomic<uint64_t> g_shader_preprocess_us{0};
std::atomic<uint64_t> g_shader_compile_us{0};
std::atomic<uint64_t> g_shader_reflect_us{0};
std::atomic<uint64_t> g_shader_module_us{0};
std::atomic<uint64_t> g_pipeline_renderpass_us{0};
std::atomic<uint64_t> g_pipeline_create_us{0};

// Incremented from vulkan_command_list.cpp's per-command methods.
std::atomic<uint64_t> g_draw_count{0};
std::atomic<uint64_t> g_bind_pipeline_count{0};
std::atomic<uint64_t> g_bind_rset_count{0};
std::atomic<uint64_t> g_bind_vbo_count{0};
std::atomic<uint64_t> g_bind_ibo_count{0};
std::atomic<uint64_t> g_push_constants_count{0};
std::atomic<uint64_t> g_record_us{0};
std::atomic<uint64_t> g_submit_us{0};

} // namespace tgfx

#endif // TGFX2_HAS_VULKAN
