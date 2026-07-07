#include <termin/render/render_item_submission.hpp>

#include "render_item_mesh.hpp"

#include <tcbase/tc_log.hpp>

namespace termin {

bool submit_render_item_draw(
    tgfx::RenderContext2& ctx,
    const tc_render_item& item,
    const RenderItemDrawSubmitRequest& request)
{
    const char* pass_name = request.debug_pass_name
        ? request.debug_pass_name
        : "RenderItemSubmit";
    const char* entity_name = request.debug_entity_name
        ? request.debug_entity_name
        : "<unnamed>";

    switch (item.kind) {
        case TC_RENDER_ITEM_KIND_MESH: {
            MeshRenderItemEncodeRequest mesh_request{};
            mesh_request.shader = request.shader;
            mesh_request.vertex_input = request.mesh_vertex_input;
            mesh_request.debug_pass_name = pass_name;
            mesh_request.debug_entity_name = entity_name;
            return encode_mesh_render_item_draw(ctx, item, mesh_request);
        }
        default:
            tc::Log::error(
                "[%s] skip RenderItem draw for '%s': unsupported item kind %u",
                pass_name,
                entity_name,
                item.kind);
            return false;
    }
}

} // namespace termin
