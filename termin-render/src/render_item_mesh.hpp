#pragma once

#include <termin/render/material_pipeline.hpp>

extern "C" {
#include <core/tc_render_item.h>
#include <tgfx/resources/tc_shader.h>
}

namespace tgfx {
class RenderContext2;
}

namespace termin {

struct MeshRenderItemEncodeRequest {
    const tc_shader* shader = nullptr;
    MaterialMeshVertexInput vertex_input = MaterialMeshVertexInput::FullMaterial;
    const char* debug_pass_name = nullptr;
    const char* debug_entity_name = nullptr;
};

bool encode_mesh_render_item_draw(
    tgfx::RenderContext2& ctx,
    const tc_render_item& item,
    const MeshRenderItemEncodeRequest& request);

} // namespace termin
