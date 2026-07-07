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

using RenderItemDrawEncoderFn = bool (*)(
    tgfx::RenderContext2& ctx,
    const tc_render_item& item,
    const RenderItemDrawSubmitRequest& request,
    void* user_data);

struct RenderItemDrawEncoderDesc {
    RenderItemDrawEncoderFn encode = nullptr;
    void* user_data = nullptr;
    const char* debug_name = nullptr;
};

// Registers an encoder for a non-mesh RenderItem kind owned by another package.
// Passes bind pass/material resources before submit; encoders bind only
// payload-kind-specific resources and issue backend draw commands.
RENDER_API bool register_render_item_draw_encoder(
    uint32_t item_kind,
    const RenderItemDrawEncoderDesc& desc);

RENDER_API bool unregister_render_item_draw_encoder(
    uint32_t item_kind,
    RenderItemDrawEncoderFn encode,
    void* user_data = nullptr);

RENDER_API bool submit_render_item_draw(
    tgfx::RenderContext2& ctx,
    const tc_render_item& item,
    const RenderItemDrawSubmitRequest& request);

} // namespace termin
