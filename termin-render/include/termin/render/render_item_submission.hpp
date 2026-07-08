#pragma once

#include <termin/render/material_pipeline.hpp>
#include <termin/render/render_export.hpp>

extern "C" {
#include <core/tc_render_item.h>
#include <tgfx/resources/tc_shader.h>
}

struct tc_material_phase;

namespace tgfx {
class IRenderDevice;
class RenderContext2;
}

namespace termin {

struct RenderContext;

struct RenderItemNamedTextureBinding {
    const char* name = nullptr;
    tgfx::TextureHandle texture;
    tgfx::SamplerHandle sampler;
};

struct RenderItemNamedUniformBinding {
    const char* name = nullptr;
    const void* data = nullptr;
    uint32_t size = 0;
    const char* only_if_shader_has_resource = nullptr;
    const char* only_if_shader_lacks_resource = nullptr;
};

struct RenderItemResourceBinding {
    const MaterialPipelineResourceView* material_resources = nullptr;
    const RenderItemNamedUniformBinding* named_uniforms = nullptr;
    uint32_t named_uniform_count = 0;
    const RenderItemNamedTextureBinding* named_textures = nullptr;
    uint32_t named_texture_count = 0;
};

struct RenderItemDrawSubmitRequest {
    const tc_shader* shader = nullptr;
    tc_shader_handle shader_handle = tc_shader_handle_invalid();
    tgfx::IRenderDevice* device = nullptr;
    MaterialMeshVertexInput mesh_vertex_input = MaterialMeshVertexInput::FullMaterial;
    const RenderContext* draw_context = nullptr;
    tc_material_phase* material_phase = nullptr;
    const char* phase_mark = nullptr;
    const char* debug_pass_name = nullptr;
    const char* debug_entity_name = nullptr;
    const RenderItemResourceBinding* resources = nullptr;
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

RENDER_API bool bind_render_item_common_resources(
    tgfx::RenderContext2& ctx,
    const tc_shader* shader,
    const RenderItemDrawSubmitRequest& request);

RENDER_API bool submit_render_item_draw(
    tgfx::RenderContext2& ctx,
    const tc_render_item& item,
    const RenderItemDrawSubmitRequest& request);

} // namespace termin
