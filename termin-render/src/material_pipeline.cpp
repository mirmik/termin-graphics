#include "termin/render/material_pipeline.hpp"

#include "termin/render/material_ubo_apply.hpp"
#include "termin/render/material_pipeline_shader_assembler.hpp"
#include "termin/render/shader_contract.hpp"
#include "termin/render/shader_resource_apply.hpp"
#include "termin/render/tgfx2_bridge.hpp"
#include "tgfx2/render_context.hpp"
#include "tgfx2/tc_shader_bridge.hpp"
#include "tcbase/tc_log.hpp"

#include <cstring>
#include <string>
#include <unordered_set>

namespace termin {
namespace {

bool should_log_material_shader_override_error(
    const MaterialShaderOverrideRequest& request,
    const char* reason)
{
    static std::unordered_set<std::string> logged_keys;
    std::string key = request.debug_context ? request.debug_context : "MaterialShaderOverride";
    key += '|';
    key += request.original_shader.uuid();
    key += '|';
    key += std::to_string(static_cast<unsigned>(request.vertex_transform_kind));
    key += '|';
    key += std::to_string(static_cast<unsigned>(request.pass_kind));
    key += '|';
    key += std::to_string(static_cast<unsigned>(request.shader_variant_op));
    key += '|';
    key += reason ? reason : "";
    return logged_keys.insert(key).second;
}

const char* material_shader_override_context(const MaterialShaderOverrideRequest& request) {
    return request.debug_context ? request.debug_context : "MaterialShaderOverride";
}

bool material_shader_override_supported(VertexTransformKind kind)
{
    switch (kind) {
    case VertexTransformKind::SkinnedMesh:
    case VertexTransformKind::Foliage:
    case VertexTransformKind::FoliageShadow:
        return true;
    case VertexTransformKind::StaticMesh:
        return false;
    }
    return false;
}

tc_shader_variant_op default_shader_variant_op_for_transform(VertexTransformKind kind)
{
    switch (kind) {
    case VertexTransformKind::SkinnedMesh:
        return TC_SHADER_VARIANT_SKINNING;
    case VertexTransformKind::Foliage:
        return TC_SHADER_VARIANT_FOLIAGE;
    case VertexTransformKind::FoliageShadow:
        return TC_SHADER_VARIANT_FOLIAGE_SHADOW;
    case VertexTransformKind::StaticMesh:
        return TC_SHADER_VARIANT_NONE;
    }
    return TC_SHADER_VARIANT_NONE;
}

const char* shader_override_suffix(VertexTransformKind kind)
{
    switch (kind) {
    case VertexTransformKind::SkinnedMesh:
        return "_Skinned";
    case VertexTransformKind::Foliage:
        return "_Foliage";
    case VertexTransformKind::FoliageShadow:
        return "_FoliageShadow";
    case VertexTransformKind::StaticMesh:
        return "_MaterialPipeline";
    default:
        return "_MaterialPipeline";
    }
}

MaterialFragmentInterface required_fragment_interface_for_pass(
    MaterialPipelinePassKind pass_kind)
{
    if (pass_kind == MaterialPipelinePassKind::Color) {
        return material_pipeline_standard_material_fragment_interface();
    }
    return {};
}

std::string shader_override_name_for_request(
    TcShader original_shader,
    const MaterialShaderOverrideRequest& request)
{
    std::string name = original_shader.name();
    if (name.empty()) {
        name = original_shader.uuid();
    }
    name += shader_override_suffix(request.vertex_transform_kind);
    const char* pass_name = material_pipeline_pass_kind_name(request.pass_kind);
    if (pass_name && pass_name[0] != '\0') {
        name += "_";
        name += pass_name;
    }
    return name;
}

void log_material_pipeline_assembly_diagnostics(
    const MaterialShaderOverrideRequest& request,
    const MaterialPipelineShaderAssemblyResult& assembly)
{
    const char* context = material_shader_override_context(request);
    for (const MaterialPipelineDiagnostic& diagnostic : assembly.diagnostics) {
        tc::Log::error(
            "[%s] shader contract assembly failed: %s: %s",
            context,
            material_pipeline_diagnostic_code_name(diagnostic.code),
            diagnostic.message.c_str());
    }
}

bool contract_has_vertex_input(
    const tc_shader_contract_view& contract,
    const char* semantic)
{
    if (!semantic || semantic[0] == '\0') {
        return false;
    }
    for (uint32_t i = 0; i < contract.vertex_input_count; ++i) {
        if (std::strncmp(
                contract.vertex_inputs[i].semantic,
                semantic,
                TC_SHADER_RESOURCE_NAME_MAX) == 0) {
            return true;
        }
    }
    return false;
}

bool shader_requires_material_pipeline_contract(const tc_shader* shader)
{
    if (!shader || !shader->is_variant) {
        return false;
    }
    return shader->variant_op == TC_SHADER_VARIANT_SKINNING ||
           shader->variant_op == TC_SHADER_VARIANT_FOLIAGE ||
           shader->variant_op == TC_SHADER_VARIANT_FOLIAGE_SHADOW;
}

} // namespace

TcShader assemble_material_shader_override(const MaterialShaderOverrideRequest& request) {
    const char* context = material_shader_override_context(request);
    TcShader original_shader = request.original_shader;
    if (!original_shader.is_valid()) {
        return TcShader();
    }
    const tc_shader_variant_op shader_variant_op =
        request.shader_variant_op != TC_SHADER_VARIANT_NONE
            ? request.shader_variant_op
            : default_shader_variant_op_for_transform(request.vertex_transform_kind);
    if (shader_variant_op != TC_SHADER_VARIANT_NONE &&
        original_shader.variant_op() == shader_variant_op) {
        return original_shader;
    }

    if (!material_shader_override_supported(request.vertex_transform_kind)) {
        if (should_log_material_shader_override_error(request, "unsupported_transform_kind")) {
            tc::Log::error(
                "[%s] cannot create material shader override for '%s': unsupported transform kind %u",
                context,
                original_shader.name(),
                static_cast<unsigned>(request.vertex_transform_kind));
        }
        return TcShader();
    }

    char variant_uuid[40];
    tc_shader_make_variant_uuid(
        variant_uuid,
        sizeof(variant_uuid),
        original_shader.uuid(),
        shader_variant_op);

    MaterialPipelineShaderAssemblyRequest assembly_request{};
    assembly_request.material = material_pipeline_material_contract_from_shader(
        original_shader,
        required_fragment_interface_for_pass(request.pass_kind));
    assembly_request.vertex_transform =
        material_pipeline_builtin_vertex_transform_contract(
            request.vertex_transform_kind,
            request.pass_kind);
    assembly_request.pass = material_pipeline_builtin_pass_contract(request.pass_kind);
    assembly_request.shader_name = shader_override_name_for_request(original_shader, request);
    assembly_request.shader_uuid = variant_uuid;
    assembly_request.language = original_shader.language();
    assembly_request.artifact_policy = original_shader.artifact_policy();

    MaterialPipelineShaderAssemblyResult assembly =
        material_pipeline_assemble_shader(assembly_request);
    if (!assembly.ok()) {
        if (should_log_material_shader_override_error(request, "shader_contract_assembler_failed")) {
            log_material_pipeline_assembly_diagnostics(request, assembly);
        }
        return TcShader();
    }

    if (shader_variant_op != TC_SHADER_VARIANT_NONE) {
        assembly.shader.set_variant_info(original_shader, shader_variant_op);
    }
    if (!tc_shader_has_contract(assembly.shader.get())) {
        tc::Log::error(
            "[%s] material shader override for '%s' was created without tc_shader_contract",
            context,
            original_shader.name());
        return TcShader();
    }
    return assembly.shader;
}

MaterialMeshVertexInput material_mesh_vertex_input_for_shader(
    const tc_shader* shader,
    MaterialMeshVertexInput static_input)
{
    if (!shader) {
        return static_input;
    }

    tc_shader_contract_view contract{};
    if (!tc_shader_get_contract_view(shader, &contract)) {
        return static_input;
    }

    const bool has_position = contract_has_vertex_input(contract, "position");
    const bool has_normal = contract_has_vertex_input(contract, "normal");
    const bool has_uv = contract_has_vertex_input(contract, "uv");
    const bool has_tangent = contract_has_vertex_input(contract, "tangent");
    const bool has_joints = contract_has_vertex_input(contract, "joints");
    const bool has_weights = contract_has_vertex_input(contract, "weights");

    if (!has_position) {
        return static_input;
    }

    if (has_joints && has_weights) {
        if (has_uv || has_tangent) {
            return MaterialMeshVertexInput::FullMaterial;
        }
        if (has_normal) {
            return MaterialMeshVertexInput::SkinnedPositionNormalJointsWeights;
        }
        return MaterialMeshVertexInput::SkinnedPositionJointsWeights;
    }

    if (has_uv || has_tangent) {
        return MaterialMeshVertexInput::FullMaterial;
    }
    if (has_normal) {
        return MaterialMeshVertexInput::PositionNormal;
    }
    return MaterialMeshVertexInput::Position;
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
    const bool requires_contract = shader_requires_material_pipeline_contract(shader);
    if (!validate_shader_contract(
            shader,
            ShaderContractValidationOptions{
                debug_context ? debug_context : "MaterialPipeline",
                requires_contract,
                true})) {
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
