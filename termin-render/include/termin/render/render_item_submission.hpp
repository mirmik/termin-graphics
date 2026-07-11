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

enum class RenderItemPassSemantic : uint32_t {
    Color = 0,
    Shadow = 1,
    Id = 2,
    Depth = 3,
    DepthOnly = 4,
    Normal = 5,
};

constexpr uint64_t render_item_pass_semantic_bit(RenderItemPassSemantic semantic)
{
    return 1ull << static_cast<uint32_t>(semantic);
}

struct RenderItemEncoderCapabilities {
    uint64_t pass_semantic_mask = 0;
    uint64_t vertex_transform_kind_mask = 0;
    bool requires_draw_context = false;
    bool consumes_common_resources = true;
};

struct RenderItemDrawEncoderDesc {
    RenderItemDrawEncoderFn encode = nullptr;
    void* user_data = nullptr;
    const char* debug_name = nullptr;
    RenderItemEncoderCapabilities capabilities{};
};

RENDER_API bool set_render_item_inline_uniform(
    tc_render_item& item,
    const char* name,
    const void* data,
    uint32_t size);

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

RENDER_API bool get_render_item_encoder_capabilities(
    uint32_t item_kind,
    RenderItemEncoderCapabilities& out);

RENDER_API bool render_item_encoder_supports_pass(
    uint32_t item_kind,
    RenderItemPassSemantic semantic);

RENDER_API bool bind_render_item_common_resources(
    tgfx::RenderContext2& ctx,
    const tc_shader* shader,
    const RenderItemDrawSubmitRequest& request);

// Bind small item-local constant-buffer data carried directly by a typed
// RenderItem. This replaces backend-specific Drawable upload callbacks while
// keeping the payload valid across deferred collection and submission.
RENDER_API bool bind_render_item_inline_uniform(
    tgfx::RenderContext2& ctx,
    const tc_render_item& item,
    const tc_shader* shader,
    const char* debug_pass_name,
    const char* debug_entity_name);

RENDER_API bool submit_render_item_draw(
    tgfx::RenderContext2& ctx,
    const tc_render_item& item,
    const RenderItemDrawSubmitRequest& request);

} // namespace termin
