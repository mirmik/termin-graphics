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
#include <cstdio>
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
    if (request.pass_contract.has_value()) {
        key += request.pass_contract->debug_name;
    }
    key += '|';
    key += std::to_string(static_cast<unsigned>(request.shader_variant_op));
    key += '|';
    key += reason ? reason : "";
    return logged_keys.insert(key).second;
}

const char* material_shader_override_context(const MaterialShaderOverrideRequest& request) {
    return request.debug_context ? request.debug_context : "MaterialShaderOverride";
}

uint64_t fnv1a_append(const char* text, uint64_t hash)
{
    const unsigned char* p = reinterpret_cast<const unsigned char*>(text ? text : "");
    while (*p) {
        hash ^= static_cast<uint64_t>(*p++);
        hash *= 0x100000001b3ULL;
    }
    return hash;
}

uint64_t fnv1a_append_u32(uint32_t value, uint64_t hash)
{
    char buffer[16];
    std::snprintf(buffer, sizeof(buffer), "%u", value);
    return fnv1a_append(buffer, hash);
}

void append_semantics_to_hash(
    const std::vector<MaterialPipelineSemantic>& semantics,
    uint64_t& hash)
{
    for (const MaterialPipelineSemantic& semantic : semantics) {
        hash = fnv1a_append(semantic.name.c_str(), hash);
        hash = fnv1a_append(":", hash);
        hash = fnv1a_append_u32(static_cast<uint32_t>(semantic.type), hash);
        hash = fnv1a_append(";", hash);
    }
}

void append_resources_to_hash(
    const std::vector<MaterialPipelineResourceDecl>& resources,
    uint64_t& hash)
{
    for (const MaterialPipelineResourceDecl& resource : resources) {
        hash = fnv1a_append(resource.requirement.name.c_str(), hash);
        hash = fnv1a_append(":", hash);
        hash = fnv1a_append_u32(resource.requirement.kind, hash);
        hash = fnv1a_append(":", hash);
        hash = fnv1a_append_u32(resource.requirement.scope, hash);
        hash = fnv1a_append(":", hash);
        hash = fnv1a_append_u32(resource.requirement.stage_mask, hash);
        hash = fnv1a_append(";", hash);
    }
}

void make_shader_override_variant_uuid(
    char* out_uuid,
    size_t out_size,
    TcShader original_shader,
    tc_shader_variant_op variant_op,
    const VertexTransformContract& vertex_transform,
    const MaterialPipelinePassContract& pass_contract)
{
    if (!out_uuid || out_size == 0) {
        return;
    }

    uint64_t hash = 0xcbf29ce484222325ULL;
    hash = fnv1a_append(original_shader.uuid(), hash);
    hash = fnv1a_append("::material_override::", hash);
    hash = fnv1a_append_u32(static_cast<uint32_t>(variant_op), hash);
    hash = fnv1a_append("::vertex::", hash);
    hash = fnv1a_append_u32(static_cast<uint32_t>(vertex_transform.kind), hash);
    hash = fnv1a_append(vertex_transform.debug_name.c_str(), hash);
    if (vertex_transform.template_uuid.has_value()) {
        hash = fnv1a_append(vertex_transform.template_uuid->c_str(), hash);
    }
    append_semantics_to_hash(vertex_transform.vertex_inputs.mesh_attributes, hash);
    append_semantics_to_hash(vertex_transform.produced_fragment_input.semantics, hash);
    append_resources_to_hash(vertex_transform.resources, hash);
    hash = fnv1a_append("::pass::", hash);
    hash = fnv1a_append(pass_contract.debug_name.c_str(), hash);
    hash = fnv1a_append(pass_contract.uses_material_fragment ? ":matfrag:1" : ":matfrag:0", hash);
    append_semantics_to_hash(
        pass_contract.required_material_fragment_input.semantics,
        hash);
    append_resources_to_hash(pass_contract.resources, hash);

    std::snprintf(out_uuid, out_size, "shv_%016llx", static_cast<unsigned long long>(hash));
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

std::string shader_override_name_for_request(
    TcShader original_shader,
    const MaterialShaderOverrideRequest& request,
    const VertexTransformContract& vertex_transform,
    const MaterialPipelinePassContract& pass)
{
    std::string name = original_shader.name();
    if (name.empty()) {
        name = original_shader.uuid();
    }
    name += shader_override_suffix(request.vertex_transform_kind);
    if (!vertex_transform.debug_name.empty()) {
        name += "_";
        name += vertex_transform.debug_name;
    }
    if (!pass.debug_name.empty()) {
        name += "_";
        name += pass.debug_name;
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

const std::optional<VertexTransformContract>& vertex_transform_for_request(
    const MaterialPipelinePassContract& pass_contract,
    VertexTransformKind kind)
{
    switch (kind) {
    case VertexTransformKind::SkinnedMesh:
        return pass_contract.skinned_vertex_transform;
    case VertexTransformKind::Foliage:
    case VertexTransformKind::FoliageShadow:
        return pass_contract.foliage_vertex_transform;
    case VertexTransformKind::StaticMesh:
        return pass_contract.static_vertex_transform;
    }
    return pass_contract.static_vertex_transform;
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

    if (!request.pass_contract.has_value()) {
        if (should_log_material_shader_override_error(request, "missing_pass_contract")) {
            tc::Log::error(
                "[%s] cannot create material shader override for '%s': pass contract is required",
                context,
                original_shader.name());
        }
        return TcShader();
    }

    MaterialPipelinePassContract pass_contract = *request.pass_contract;

    std::optional<VertexTransformContract> vertex_transform_contract_opt =
        request.vertex_transform_contract.has_value()
            ? request.vertex_transform_contract
            : vertex_transform_for_request(
                pass_contract,
                request.vertex_transform_kind);
    if (!vertex_transform_contract_opt.has_value()) {
        if (should_log_material_shader_override_error(request, "missing_vertex_transform_contract")) {
            tc::Log::error(
                "[%s] cannot create material shader override for '%s': pass '%s' has no %s vertex transform contract",
                context,
                original_shader.name(),
                pass_contract.debug_name.c_str(),
                vertex_transform_kind_name(request.vertex_transform_kind));
        }
        return TcShader();
    }
    VertexTransformContract vertex_transform_contract =
        std::move(*vertex_transform_contract_opt);

    char variant_uuid[40];
    make_shader_override_variant_uuid(
        variant_uuid,
        sizeof(variant_uuid),
        original_shader,
        shader_variant_op,
        vertex_transform_contract,
        pass_contract);

    MaterialPipelineShaderAssemblyRequest assembly_request{};
    assembly_request.material = material_pipeline_material_contract_from_shader(
        original_shader,
        pass_contract.required_material_fragment_input);
    assembly_request.vertex_transform = vertex_transform_contract;
    assembly_request.pass = pass_contract;
    assembly_request.shader_name = shader_override_name_for_request(
        original_shader,
        request,
        vertex_transform_contract,
        pass_contract);
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

bool draw_material_pipeline_submesh(
    tgfx::RenderContext2& ctx,
    tc_mesh* mesh,
    size_t submesh_index,
    MaterialMeshVertexInput input)
{
    switch (input) {
        case MaterialMeshVertexInput::FullMaterial:
            return ::termin::draw_tc_submesh(ctx, mesh, submesh_index);
        case MaterialMeshVertexInput::Position:
            return ::termin::draw_tc_submesh(ctx, mesh, submesh_index, {"position"});
        case MaterialMeshVertexInput::PositionNormal:
            return ::termin::draw_tc_submesh(ctx, mesh, submesh_index, {"position", "normal"});
        case MaterialMeshVertexInput::SkinnedPositionJointsWeights:
            return ::termin::draw_tc_submesh(
                ctx,
                mesh,
                submesh_index,
                {"position", "joints", "weights"},
                true);
        case MaterialMeshVertexInput::SkinnedPositionNormalJointsWeights:
            return ::termin::draw_tc_submesh(
                ctx,
                mesh,
                submesh_index,
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

    if (resources.lighting_ubo) {
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
