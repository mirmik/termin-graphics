#pragma once

#include <termin/geom/mat44.hpp>
#include <termin/render/execute_context.hpp>
#include <termin/render/render_export.hpp>

// Forward declare — tc_shader.h is C, we don't want to pull it into every
// translation unit that includes frame_uniforms.hpp.
struct tc_shader;

namespace tgfx {
class RenderContext2;
}

namespace termin {

// Fallback for shaders that don't declare per_frame in resource_bindings.
constexpr uint32_t ENGINE_PER_FRAME_UBO_BINDING = 2;

struct EnginePerFrameStd140 {
    float u_view[16];
    float u_projection[16];
    float u_view_projection[16];
    float u_inv_view[16];
    float u_inv_proj[16];
    float u_camera_position[4];
    float u_resolution[2];
    float u_near;
    float u_far;
};

static_assert(sizeof(EnginePerFrameStd140) == 352,
              "EnginePerFrameStd140 layout must match shader_parser PerFrame");

RENDER_API EnginePerFrameStd140 make_engine_per_frame_uniforms(
    const Mat44f& view,
    const Mat44f& projection,
    const Vec3& camera_position,
    float width,
    float height,
    float near_clip,
    float far_clip
);

RENDER_API EnginePerFrameStd140 make_engine_per_frame_uniforms(
    const ExecuteContext& ctx
);

RENDER_API void bind_engine_per_frame_uniforms(
    tgfx::RenderContext2& ctx2,
    const EnginePerFrameStd140& uniforms
);

RENDER_API void bind_engine_per_frame_uniforms(
    tgfx::RenderContext2& ctx2,
    const ExecuteContext& ctx
);

// Resolve per_frame binding from shader resource_bindings[]. Falls back to
// ENGINE_PER_FRAME_UBO_BINDING when the shader has no "per_frame" entry.
RENDER_API uint32_t resolve_per_frame_binding(const tc_shader* shader,
                                              uint32_t fallback);

RENDER_API void bind_engine_per_frame_uniforms(
    tgfx::RenderContext2& ctx2,
    const EnginePerFrameStd140& uniforms,
    const tc_shader* shader);

} // namespace termin
