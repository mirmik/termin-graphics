#include "termin/render/frame_uniforms.hpp"

#include "tcbase/tc_log.hpp"
#include "tgfx2/render_context.hpp"

#include <cstring>

extern "C" {
#include <tgfx/resources/tc_shader.h>
#include <tgfx/resources/tc_shader_registry.h>
}

namespace termin {

EnginePerFrameStd140 make_engine_per_frame_uniforms(
    const Mat44f& view,
    const Mat44f& projection,
    const Vec3& camera_position,
    float width,
    float height,
    float near_clip,
    float far_clip)
{
    EnginePerFrameStd140 pf{};
    Mat44f vp = projection * view;
    Mat44f inv_view = view.inverse();
    Mat44f inv_proj = projection.inverse();

    std::memcpy(pf.u_view, view.data, sizeof(pf.u_view));
    std::memcpy(pf.u_projection, projection.data, sizeof(pf.u_projection));
    std::memcpy(pf.u_view_projection, vp.data, sizeof(pf.u_view_projection));
    std::memcpy(pf.u_inv_view, inv_view.data, sizeof(pf.u_inv_view));
    std::memcpy(pf.u_inv_proj, inv_proj.data, sizeof(pf.u_inv_proj));

    pf.u_camera_position[0] = static_cast<float>(camera_position.x);
    pf.u_camera_position[1] = static_cast<float>(camera_position.y);
    pf.u_camera_position[2] = static_cast<float>(camera_position.z);
    pf.u_camera_position[3] = 1.0f;
    pf.u_resolution[0] = width;
    pf.u_resolution[1] = height;
    pf.u_near = near_clip;
    pf.u_far = far_clip;
    return pf;
}

EnginePerFrameStd140 make_engine_per_frame_uniforms(const ExecuteContext& ctx) {
    Mat44f view = Mat44f::identity();
    Mat44f projection = Mat44f::identity();
    Vec3 camera_position = Vec3::zero();
    float near_clip = 0.1f;
    float far_clip = 100.0f;

    if (ctx.camera) {
        view = ctx.camera->get_view_matrix().to_float();
        projection = ctx.camera->get_projection_matrix().to_float();
        camera_position = ctx.camera->get_position();
        near_clip = static_cast<float>(ctx.camera->near_clip);
        far_clip = static_cast<float>(ctx.camera->far_clip);
    }

    return make_engine_per_frame_uniforms(
        view,
        projection,
        camera_position,
        static_cast<float>(ctx.render_rect.width),
        static_cast<float>(ctx.render_rect.height),
        near_clip,
        far_clip);
}

void bind_engine_per_frame_uniforms(
    tgfx::RenderContext2& ctx2,
    const EnginePerFrameStd140& uniforms,
    const tc_shader* shader)
{
    const tc_shader_resource_binding* rb =
        shader ? tc_shader_find_resource_binding(shader, TC_SHADER_RESOURCE_PER_FRAME) : nullptr;
    if (!rb && shader) {
        rb = tc_shader_find_resource_binding(shader, "u_per_frame");
    }
    if (rb && rb->kind == TC_SHADER_RESOURCE_CONSTANT_BUFFER) {
        ctx2.bind_uniform_data(rb->name, &uniforms, sizeof(uniforms));
        return;
    }
    if (shader && tc_shader_has_resource_layout(shader)) {
        return;
    }
    tc::Log::error(
        "[FrameUniforms] shader '%s' has no per_frame constant-buffer resource layout entry",
        shader && shader->name ? shader->name : "<unnamed>");
}

} // namespace termin
