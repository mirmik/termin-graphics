#include "termin/render/material_pipeline.hpp"

#include "termin/render/material_ubo_apply.hpp"
#include "termin/render/material_pipeline_variant.hpp"
#include "termin/render/shader_resource_apply.hpp"
#include "termin/render/tgfx2_bridge.hpp"
#include "tgfx2/render_context.hpp"
#include "tgfx2/tc_shader_bridge.hpp"
#include "tcbase/tc_log.hpp"

#include <string>
#include <unordered_map>
#include <unordered_set>

extern "C" {
#include "tgfx/resources/tc_shader_registry.h"
}

namespace termin {
namespace {

struct MaterialVertexVariantKey {
    uint32_t shader_index = 0;
    uint32_t shader_generation = 0;
    uint8_t variant_op = TC_SHADER_VARIANT_NONE;
    std::string vertex_template_uuid;
    std::string vertex_entry;
    std::string fragment_source_override;
    std::string fragment_entry_override;
};

struct MaterialVertexVariantKeyHash {
    size_t operator()(const MaterialVertexVariantKey& key) const {
        size_t h = std::hash<uint32_t>()(key.shader_index);
        h ^= std::hash<uint32_t>()(key.shader_generation) << 1;
        h ^= std::hash<uint8_t>()(key.variant_op) << 2;
        h ^= std::hash<std::string>()(key.vertex_template_uuid) << 3;
        h ^= std::hash<std::string>()(key.vertex_entry) << 4;
        h ^= std::hash<std::string>()(key.fragment_source_override) << 5;
        h ^= std::hash<std::string>()(key.fragment_entry_override) << 6;
        return h;
    }
};

struct MaterialVertexVariantKeyEqual {
    bool operator()(const MaterialVertexVariantKey& a, const MaterialVertexVariantKey& b) const {
        return a.shader_index == b.shader_index
            && a.shader_generation == b.shader_generation
            && a.variant_op == b.variant_op
            && a.vertex_template_uuid == b.vertex_template_uuid
            && a.vertex_entry == b.vertex_entry
            && a.fragment_source_override == b.fragment_source_override
            && a.fragment_entry_override == b.fragment_entry_override;
    }
};

std::unordered_map<
    MaterialVertexVariantKey,
    TcShader,
    MaterialVertexVariantKeyHash,
    MaterialVertexVariantKeyEqual>& material_vertex_variant_cache()
{
    static std::unordered_map<
        MaterialVertexVariantKey,
        TcShader,
        MaterialVertexVariantKeyHash,
        MaterialVertexVariantKeyEqual> cache;
    return cache;
}

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
    key += request.vertex_template_uuid ? request.vertex_template_uuid : "";
    key += '|';
    key += reason ? reason : "";
    return logged_keys.insert(key).second;
}

const char* material_vertex_variant_context(const MaterialVertexVariantRequest& request) {
    return request.debug_context ? request.debug_context : "MaterialVertexVariant";
}

MaterialPipelinePassKind pass_kind_for_skinned_template(const char* template_uuid)
{
    if (!template_uuid) {
        return MaterialPipelinePassKind::Color;
    }
    const std::string uuid = template_uuid;
    if (uuid == "termin-engine-skinned-shadow") {
        return MaterialPipelinePassKind::Shadow;
    }
    if (uuid == "termin-engine-skinned-depth") {
        return MaterialPipelinePassKind::Depth;
    }
    if (uuid == "termin-engine-skinned-id") {
        return MaterialPipelinePassKind::Id;
    }
    if (uuid == "termin-engine-skinned-normal") {
        return MaterialPipelinePassKind::Normal;
    }
    return MaterialPipelinePassKind::Color;
}

bool legacy_variant_contract_kind(
    const MaterialVertexVariantRequest& request,
    VertexTransformKind& transform_kind,
    MaterialPipelinePassKind& pass_kind)
{
    switch (request.variant_op) {
    case TC_SHADER_VARIANT_SKINNING:
        transform_kind = VertexTransformKind::SkinnedMesh;
        pass_kind = pass_kind_for_skinned_template(request.vertex_template_uuid);
        return true;
    case TC_SHADER_VARIANT_FOLIAGE:
        transform_kind = VertexTransformKind::Foliage;
        pass_kind = MaterialPipelinePassKind::Color;
        return true;
    case TC_SHADER_VARIANT_FOLIAGE_SHADOW:
        transform_kind = VertexTransformKind::FoliageShadow;
        pass_kind = MaterialPipelinePassKind::Shadow;
        return true;
    default:
        return false;
    }
}

std::string variant_name_for_request(
    TcShader original_shader,
    const MaterialVertexVariantRequest& request)
{
    std::string variant_name = original_shader.name();
    if (variant_name.empty()) {
        variant_name = std::string("Variant_") + original_shader.uuid();
    }
    if (request.variant_name_suffix && request.variant_name_suffix[0] != '\0') {
        variant_name += request.variant_name_suffix;
    }
    return variant_name;
}

void log_material_pipeline_variant_diagnostics(
    const MaterialVertexVariantRequest& request,
    const MaterialPipelineVariantPlan& plan)
{
    const char* context = material_vertex_variant_context(request);
    for (const MaterialPipelineDiagnostic& diagnostic : plan.diagnostics) {
        tc::Log::error(
            "[%s] material pipeline variant failed: %s: %s",
            context,
            material_pipeline_diagnostic_code_name(diagnostic.code),
            diagnostic.message.c_str());
    }
}

MaterialPipelineVariantRequest make_contract_variant_request(
    const MaterialVertexVariantRequest& request,
    TcShader original_shader,
    VertexTransformKind transform_kind,
    MaterialPipelinePassKind pass_kind,
    const std::string& variant_name,
    const char* variant_uuid)
{
    MaterialPipelineVariantRequest contract_request{};
    contract_request.material = material_pipeline_material_contract_from_shader(
        original_shader,
        material_pipeline_standard_material_fragment_interface());
    contract_request.vertex_transform =
        material_pipeline_builtin_vertex_transform_contract(
            transform_kind,
            pass_kind);

    if (request.vertex_template_uuid && request.vertex_template_uuid[0] != '\0') {
        contract_request.vertex_transform.template_uuid = request.vertex_template_uuid;
    }
    if (request.vertex_entry && request.vertex_entry[0] != '\0') {
        contract_request.vertex_transform.vertex_entry = request.vertex_entry;
    }

    contract_request.pass = material_pipeline_builtin_pass_contract(pass_kind);
    if (request.fragment_source_override && request.fragment_source_override[0] != '\0') {
        contract_request.pass.uses_material_fragment = false;
        contract_request.pass.fragment_source_override =
            request.fragment_source_override;
        contract_request.pass.fragment_entry_override =
            request.fragment_entry_override && request.fragment_entry_override[0] != '\0'
                ? request.fragment_entry_override
                : "fs_main";
    } else {
        contract_request.pass.uses_material_fragment = true;
    }

    contract_request.shader_name = variant_name;
    contract_request.shader_uuid = variant_uuid ? variant_uuid : "";
    contract_request.artifact_policy = TC_SHADER_ARTIFACT_REQUIRED;
    return contract_request;
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
    VertexTransformKind transform_kind = VertexTransformKind::StaticMesh;
    MaterialPipelinePassKind pass_kind = MaterialPipelinePassKind::Color;
    if (!legacy_variant_contract_kind(request, transform_kind, pass_kind)) {
        if (should_log_material_vertex_variant_error(request, "unsupported_variant_op")) {
            tc::Log::error(
                "[%s] cannot create material vertex variant: unsupported variant op %u",
                context,
                static_cast<unsigned>(request.variant_op));
        }
        return TcShader();
    }
    if (!request.vertex_template_uuid || request.vertex_template_uuid[0] == '\0') {
        if (should_log_material_vertex_variant_error(request, "missing_vertex_template")) {
            tc::Log::error("[%s] cannot create material vertex variant: vertex template uuid is empty", context);
        }
        return TcShader();
    }
    if (original_shader.variant_op() == request.variant_op) {
        return original_shader;
    }
    if (request.require_slang_original && original_shader.language() != TC_SHADER_LANGUAGE_SLANG) {
        if (should_log_material_vertex_variant_error(request, "unsupported_language")) {
            tc::Log::error(
                "[%s] cannot create material vertex variant for '%s': "
                "variant requires Slang material shaders, got language=%u",
                context,
                original_shader.name(),
                static_cast<unsigned>(original_shader.language()));
        }
        return TcShader();
    }

    MaterialVertexVariantKey key{};
    key.shader_index = original_shader.handle.index;
    key.shader_generation = original_shader.handle.generation;
    key.variant_op = static_cast<uint8_t>(request.variant_op);
    key.vertex_template_uuid = request.vertex_template_uuid;
    key.vertex_entry = request.vertex_entry ? request.vertex_entry : "";
    key.fragment_source_override = request.fragment_source_override ? request.fragment_source_override : "";
    key.fragment_entry_override = request.fragment_entry_override ? request.fragment_entry_override : "";

    auto& cache = material_vertex_variant_cache();
    auto it = cache.find(key);
    if (it != cache.end()) {
        TcShader& cached = it->second;
        if (!cached.variant_is_stale()) {
            return cached;
        }
        cache.erase(it);
    }

    const char* geometry_source = original_shader.geometry_source();
    if (geometry_source && geometry_source[0] != '\0') {
        if (should_log_material_vertex_variant_error(request, "geometry_unsupported")) {
            tc::Log::error(
                "[%s] cannot create material vertex variant for '%s': geometry shaders are not supported",
                context,
                original_shader.name());
        }
        return TcShader();
    }

    const std::string variant_name =
        variant_name_for_request(original_shader, request);

    char variant_uuid[40];
    tc_shader_make_variant_uuid(
        variant_uuid,
        sizeof(variant_uuid),
        original_shader.uuid(),
        request.variant_op);

    MaterialPipelineVariantRequest contract_request =
        make_contract_variant_request(
            request,
            original_shader,
            transform_kind,
            pass_kind,
            variant_name,
            variant_uuid);

    MaterialPipelineCompiledVariant compiled =
        material_pipeline_create_variant(contract_request);
    if (!compiled.ok()) {
        log_material_pipeline_variant_diagnostics(request, compiled.plan);
        return TcShader();
    }

    TcShader variant = compiled.shader;
    variant.set_variant_info(original_shader, request.variant_op);
    cache[key] = variant;
    return variant;
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
