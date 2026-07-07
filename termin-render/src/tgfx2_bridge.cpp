#include "termin/render/tgfx2_bridge.hpp"

#include <string>
#include <string_view>

#include <tcbase/tc_log.hpp>

#include "tgfx/resources/tc_texture.h"
#include "tgfx/resources/tc_texture_registry.h"
#include "tgfx2/i_render_device.hpp"
#include "tgfx2/descriptors.hpp"

namespace termin {

tgfx::TextureHandle wrap_tc_texture_as_tgfx2(
    tgfx::IRenderDevice& device,
    tc_texture_handle handle
) {
    if (tc_texture_handle_is_invalid(handle)) return {};

    tc_texture* tex = tc_texture_get(handle);
    if (!tex) {
        tc::Log::error("wrap_tc_texture_as_tgfx2: tc_texture not found "
                       "for handle (index=%u gen=%u)",
                       handle.index, handle.generation);
        return {};
    }

    return device.ensure_tc_texture(tex);
}

void release_texture_binding(
    tgfx::IRenderDevice& device,
    tgfx::TextureHandle binding
) {
    (void)device;
    (void)binding;
    // The active IRenderDevice owns tc_texture bridge handles through
    // its per-device cache. Destroy happens on cache invalidation,
    // device teardown, or tc_texture destroy-hook.
}

Tgfx2MeshBinding wrap_mesh_as_tgfx2(
    tgfx::IRenderDevice& device,
    tc_mesh* mesh
) {
    return tgfx::wrap_mesh_as_tgfx2(device, mesh);
}

void release_mesh_binding(
    tgfx::IRenderDevice& device,
    const Tgfx2MeshBinding& binding
) {
    tgfx::release_mesh_binding(device, binding);
}

bool draw_tc_mesh(
    tgfx::RenderContext2& ctx,
    tc_mesh* mesh,
    const tgfx::VertexBufferLayout* layout_override
) {
    return tgfx::draw_tc_mesh(ctx, mesh, layout_override);
}

bool draw_tc_submesh(
    tgfx::RenderContext2& ctx,
    tc_mesh* mesh,
    size_t submesh_index,
    const tgfx::VertexBufferLayout* layout_override
) {
    return tgfx::draw_tc_submesh(ctx, mesh, submesh_index, layout_override);
}

bool draw_tc_mesh(
    tgfx::RenderContext2& ctx,
    tc_mesh* mesh,
    std::initializer_list<uint32_t> used_locations,
    bool use_shader_input_locations
) {
    return tgfx::draw_tc_mesh(ctx, mesh, used_locations, use_shader_input_locations);
}

bool draw_tc_submesh(
    tgfx::RenderContext2& ctx,
    tc_mesh* mesh,
    size_t submesh_index,
    std::initializer_list<uint32_t> used_locations,
    bool use_shader_input_locations
) {
    return tgfx::draw_tc_submesh(
        ctx,
        mesh,
        submesh_index,
        used_locations,
        use_shader_input_locations);
}

bool draw_tc_mesh(
    tgfx::RenderContext2& ctx,
    tc_mesh* mesh,
    std::initializer_list<std::string_view> used_semantics,
    bool use_shader_input_locations
) {
    return tgfx::draw_tc_mesh(ctx, mesh, used_semantics, use_shader_input_locations);
}

bool draw_tc_submesh(
    tgfx::RenderContext2& ctx,
    tc_mesh* mesh,
    size_t submesh_index,
    std::initializer_list<std::string_view> used_semantics,
    bool use_shader_input_locations
) {
    return tgfx::draw_tc_submesh(
        ctx,
        mesh,
        submesh_index,
        used_semantics,
        use_shader_input_locations);
}

} // namespace termin
