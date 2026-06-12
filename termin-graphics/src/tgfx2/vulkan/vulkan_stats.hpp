#pragma once

#ifdef TGFX2_HAS_VULKAN

#include <atomic>
#include <cstdint>

namespace tgfx {

extern std::atomic<uint64_t> g_resource_set_count;
extern std::atomic<uint64_t> g_pipeline_count;
extern std::atomic<uint64_t> g_shader_count;
extern std::atomic<uint64_t> g_shader_preprocess_us;
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
