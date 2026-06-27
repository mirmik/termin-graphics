#include "termin/render/material_pipeline.hpp"

#include "termin/render/material_ubo_apply.hpp"
#include "termin/render/shader_resource_apply.hpp"
#include "termin/render/tgfx2_bridge.hpp"
#include "tgfx2/render_context.hpp"
#include "tgfx2/tc_shader_bridge.hpp"
#include "tcbase/tc_log.hpp"

#include <string>
#include <unordered_set>

namespace termin {
namespace {

bool should_log_material_vertex_variant_error(
    const MaterialVertexVariantRequest& request,
    const char* reason)
{
    static std::unordered_set<std::string> logged_keys;
    std::string key = request.debug_context ? request.debug_context : "MaterialVertexVariant";
    key += '|';
    key += request.original_shader.uuid();
    key += '|';
    key += std::to_string(static_cast<unsigned>(request.variant_op));
    key += '|';
    key += reason ? reason : "";
    return logged_keys.insert(key).second;
}

const char* material_vertex_variant_context(const MaterialVertexVariantRequest& request) {
    return request.debug_context ? request.debug_context : "MaterialVertexVariant";
}

} // namespace

TcShader get_material_vertex_variant(const MaterialVertexVariantRequest& request) {
    const char* context = material_vertex_variant_context(request);
    TcShader original_shader = request.original_shader;
    if (!original_shader.is_valid()) {
        return TcShader();
    }
    if (request.variant_op == TC_SHADER_VARIANT_NONE) {
        if (should_log_material_vertex_variant_error(request, "missing_variant_op")) {
            tc::Log::error("[%s] cannot create material vertex variant: variant op is NONE", context);
        }
        return TcShader();
    }
    if (original_shader.variant_op() == request.variant_op) {
        return original_shader;
    }
    if (should_log_material_vertex_variant_error(request, "shader_contract_assembler_missing")) {
        tc::Log::error(
            "[%s] cannot create material vertex variant for '%s': "
            "the transitional vertex-variant assembler was removed; "
            "shader variants must be produced by the C tc_shader_contract assembler",
            context,
            original_shader.name());
    }
    return TcShader();
}

MaterialMeshVertexInput material_mesh_vertex_input_for_shader(
    const tc_shader* shader,
    MaterialMeshVertexInput static_input)
{
    if (!shader) {
        return static_input;
    }

    if (shader->variant_op != TC_SHADER_VARIANT_SKINNING) {
        return static_input;
    }

    switch (static_input) {
        case MaterialMeshVertexInput::Position:
            return MaterialMeshVertexInput::SkinnedPositionJointsWeights;
        case MaterialMeshVertexInput::PositionNormal:
            return MaterialMeshVertexInput::SkinnedPositionNormalJointsWeights;
        case MaterialMeshVertexInput::FullMaterial:
        case MaterialMeshVertexInput::SkinnedPositionJointsWeights:
        case MaterialMeshVertexInput::SkinnedPositionNormalJointsWeights:
            return static_input;
    }
    return static_input;
}

bool draw_material_pipeline_mesh(
    tgfx::RenderContext2& ctx,
    tc_mesh* mesh,
    MaterialMeshVertexInput input)
{
    switch (input) {
        case MaterialMeshVertexInput::FullMaterial:
            return ::termin::draw_tc_mesh(ctx, mesh);
        case MaterialMeshVertexInput::Position:
            return ::termin::draw_tc_mesh(ctx, mesh, {"position"});
        case MaterialMeshVertexInput::PositionNormal:
            return ::termin::draw_tc_mesh(ctx, mesh, {"position", "normal"});
        case MaterialMeshVertexInput::SkinnedPositionJointsWeights:
            return ::termin::draw_tc_mesh(
                ctx,
                mesh,
                {"position", "joints", "weights"},
                true);
        case MaterialMeshVertexInput::SkinnedPositionNormalJointsWeights:
            return ::termin::draw_tc_mesh(
                ctx,
                mesh,
                {"position", "normal", "joints", "weights"},
                true);
    }
    return false;
}

bool ensure_material_pipeline_shader(
    tgfx::RenderContext2& ctx,
    tgfx::IRenderDevice& device,
    tc_shader_handle shader_handle,
    const char* debug_context,
    MaterialPipelineShaderBinding& out)
{
    out = {};

    tc_shader* shader = tc_shader_get(shader_handle);
    if (!shader) {
        tc::Log::error(
            "[MaterialPipeline] %s shader handle is stale (index=%u gen=%u)",
            debug_context ? debug_context : "material",
            shader_handle.index,
            shader_handle.generation);
        return false;
    }

    tgfx::ShaderHandle vs;
    tgfx::ShaderHandle fs;
    if (!tc_shader_ensure_tgfx2(shader, &device, &vs, &fs)) {
        tc::Log::error(
            "[MaterialPipeline] %s tc_shader_ensure_tgfx2 failed for '%s'",
            debug_context ? debug_context : "material",
            shader->name ? shader->name : shader->uuid);
        return false;
    }

    ctx.bind_shader(vs, fs);
    ctx.use_shader_resource_layout(shader);

    out.shader = shader;
    out.vertex = vs;
    out.fragment = fs;
    return true;
}

bool prepare_material_pipeline_resources(
    tgfx::RenderContext2& ctx,
    tgfx::IRenderDevice& device,
    const tc_shader* shader,
    tc_material_phase* phase,
    const MaterialPipelineResourceContext& resources)
{
    if (!shader) {
        return false;
    }

    bool bound_any = false;

    if (resources.per_frame) {
        bind_engine_per_frame_uniforms(ctx, *resources.per_frame, shader);
        bound_any = true;
    }

    for (const MaterialPipelineUniformData& uniform : resources.uniforms) {
        if (!uniform.name || !uniform.data || uniform.size == 0) {
            continue;
        }
        ctx.bind_uniform_data(uniform.name, uniform.data, uniform.size);
        bound_any = true;
    }

    if (resources.shadow_block && resources.shadow_block_size > 0) {
        bound_any |= bind_shadow_block_for_shader(
            ctx,
            shader,
            resources.shadow_block,
            resources.shadow_block_size);
    }

    if (resources.lighting_ubo &&
        tc_shader_has_feature(shader, TC_SHADER_FEATURE_LIGHTING_UBO)) {
        bound_any |= bind_lighting_ubo_for_shader(
            ctx,
            shader,
            resources.lighting_ubo);
    }

    if (phase) {
        bound_any |= apply_material_phase_ubo(
            phase,
            shader,
            device,
            ctx);
    }

    if (!resources.shadow_maps.empty() && resources.shadow_sampler) {
        bound_any |= bind_shadow_maps_for_shader(
            ctx,
            shader,
            resources.shadow_maps,
            resources.shadow_sampler,
            resources.max_shadow_maps);
    }

    return bound_any;
}

} // namespace termin
