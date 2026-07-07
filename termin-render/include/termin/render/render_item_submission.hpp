#pragma once

#include <termin/render/material_pipeline.hpp>
#include <termin/render/render_export.hpp>

extern "C" {
#include <core/tc_render_item.h>
#include <tgfx/resources/tc_shader.h>
}

namespace tgfx {
class RenderContext2;
}

namespace termin {

struct RenderItemDrawSubmitRequest {
    const tc_shader* shader = nullptr;
    MaterialMeshVertexInput mesh_vertex_input = MaterialMeshVertexInput::FullMaterial;
    const char* debug_pass_name = nullptr;
    const char* debug_entity_name = nullptr;
};

RENDER_API bool submit_render_item_draw(
    tgfx::RenderContext2& ctx,
    const tc_render_item& item,
    const RenderItemDrawSubmitRequest& request);

} // namespace termin
