#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <type_traits>

#include "termin/render/frame_uniforms.hpp"
#include "termin/render/material_pipeline_shader_assembler.hpp"
#include "termin/render/vertex_transform_contracts.hpp"
#include "termin/render/render_export.hpp"
#include "tgfx/tgfx_shader_handle.hpp"
#include "tgfx2/handles.hpp"

extern "C" {
#include "tgfx/resources/tc_material.h"
#include "tgfx/resources/tc_shader.h"
}

struct tc_mesh;

namespace tgfx {
class IRenderDevice;
class RenderContext2;
}

namespace termin {

struct MaterialPipelineShaderBinding {
    tc_shader* shader = nullptr;
    tgfx::ShaderHandle vertex;
    tgfx::ShaderHandle fragment;
};

struct MaterialPipelineUniformData {
    const char* name = nullptr;
    const void* data = nullptr;
    uint32_t size = 0;
};

struct MaterialPipelineUniformUpload {
    const tc_shader_resource_binding* binding = nullptr;
    const void* data = nullptr;
    uint32_t size = 0;
};

struct MaterialPipelineTextureUpload {
    const tc_shader_resource_binding* binding = nullptr;
    tgfx::TextureHandle texture;
    tgfx::SamplerHandle sampler;
    uint32_t array_element = 0;
};

struct MaterialPipelineResourceView {
    const EnginePerFrameStd140* per_frame = nullptr;

    const MaterialPipelineUniformUpload* uniforms = nullptr;
    uint32_t uniform_count = 0;

    const MaterialPipelineTextureUpload* textures = nullptr;
    uint32_t texture_count = 0;

    const void* shadow_block = nullptr;
    uint32_t shadow_block_size = 0;

    tgfx::BufferHandle lighting_ubo;

    const tgfx::TextureHandle* shadow_maps = nullptr;
    uint32_t shadow_map_count = 0;
    tgfx::SamplerHandle shadow_sampler;
};

static_assert(std::is_standard_layout_v<MaterialPipelineUniformUpload>);
static_assert(std::is_trivially_copyable_v<MaterialPipelineUniformUpload>);
static_assert(std::is_standard_layout_v<MaterialPipelineTextureUpload>);
static_assert(std::is_trivially_copyable_v<MaterialPipelineTextureUpload>);
static_assert(std::is_standard_layout_v<MaterialPipelineResourceView>);
static_assert(std::is_trivially_copyable_v<MaterialPipelineResourceView>);

// Transitional C++ convenience wrapper. New render-task/submission paths should
// pass MaterialPipelineResourceView with resolved shader resource bindings.
struct MaterialPipelineResourceContext {
    const EnginePerFrameStd140* per_frame = nullptr;
    std::span<const MaterialPipelineUniformData> uniforms;
    const void* shadow_block = nullptr;
    uint32_t shadow_block_size = 0;
    tgfx::BufferHandle lighting_ubo;
    std::span<const tgfx::TextureHandle> shadow_maps;
    tgfx::SamplerHandle shadow_sampler;
    size_t max_shadow_maps = 0;
};

struct MaterialShaderOverrideRequest {
    TcShader original_shader;
    VertexTransformKind vertex_transform_kind = VertexTransformKind::StaticMesh;
    std::optional<VertexTransformContract> vertex_transform_contract;
    std::optional<MaterialPipelinePassContract> pass_contract;
    tc_shader_variant_op shader_variant_op = TC_SHADER_VARIANT_NONE;
    const char* debug_context = nullptr;
};

// Returns a registry-owned canonical variant for the complete shader/pass/
// transform intent. Equivalent requests reuse the same handle across frames;
// changing the original shader version refreshes the stale variant in place.
RENDER_API TcShader assemble_material_shader_override(const MaterialShaderOverrideRequest& request);

RENDER_API std::string material_pipeline_shader_intent_fingerprint(
    TcShader original_shader,
    tc_shader_variant_op variant_op,
    const VertexTransformContract& vertex_transform,
    const MaterialPipelinePassContract& pass_contract);

enum class MaterialMeshVertexInput {
    FullMaterial,
    Position,
    PositionNormal,
    SkinnedFullMaterial,
    SkinnedPositionJointsWeights,
    SkinnedPositionNormalJointsWeights,
};

RENDER_API MaterialMeshVertexInput material_mesh_vertex_input_for_shader(
    const tc_shader* shader,
    MaterialMeshVertexInput static_input);

RENDER_API bool draw_material_pipeline_mesh(
    tgfx::RenderContext2& ctx,
    tc_mesh* mesh,
    MaterialMeshVertexInput input);

RENDER_API bool draw_material_pipeline_submesh(
    tgfx::RenderContext2& ctx,
    tc_mesh* mesh,
    size_t submesh_index,
    MaterialMeshVertexInput input);

RENDER_API bool ensure_material_pipeline_shader(
    tgfx::RenderContext2& ctx,
    tgfx::IRenderDevice& device,
    tc_shader_handle shader_handle,
    const char* debug_context,
    MaterialPipelineShaderBinding& out);

RENDER_API bool prepare_material_pipeline_resources(
    tgfx::RenderContext2& ctx,
    tgfx::IRenderDevice& device,
    const tc_shader* shader,
    tc_material_phase* phase,
    const MaterialPipelineResourceView& resources);

RENDER_API bool prepare_material_pipeline_resources(
    tgfx::RenderContext2& ctx,
    tgfx::IRenderDevice& device,
    const tc_shader* shader,
    tc_material_phase* phase,
    const MaterialPipelineResourceContext& resources);

} // namespace termin
