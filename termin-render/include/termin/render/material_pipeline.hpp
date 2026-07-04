#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>

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
    const MaterialPipelineResourceContext& resources);

} // namespace termin
