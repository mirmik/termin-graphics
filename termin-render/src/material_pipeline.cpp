#include "termin/render/material_pipeline.hpp"

#include "termin/render/material_ubo_apply.hpp"
#include "termin/render/shader_resource_apply.hpp"
#include "termin/render/tgfx2_bridge.hpp"
#include "tgfx2/builtin_shader_sources.hpp"
#include "tgfx2/render_context.hpp"
#include "tgfx2/tc_shader_bridge.hpp"
#include "tcbase/tc_log.hpp"

#include <cstring>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

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

bool same_shader_resource_identity(
    const tc_shader_resource_binding& a,
    const tc_shader_resource_binding& b)
{
    return std::strncmp(a.name, b.name, TC_SHADER_RESOURCE_NAME_MAX) == 0 &&
           a.kind == b.kind &&
           a.scope == b.scope &&
           a.set == b.set &&
           a.binding == b.binding;
}

void merge_seed_resource_binding(
    std::vector<tc_shader_resource_binding>& bindings,
    const tc_shader_resource_binding& incoming,
    const char* context)
{
    for (tc_shader_resource_binding& existing : bindings) {
        if (same_shader_resource_identity(existing, incoming)) {
            existing.stage_mask |= incoming.stage_mask;
            if (incoming.size > existing.size) {
                existing.size = incoming.size;
            }
            return;
        }
        if (existing.set == incoming.set && existing.binding == incoming.binding &&
            (std::strncmp(existing.name, incoming.name, TC_SHADER_RESOURCE_NAME_MAX) != 0 ||
             existing.kind != incoming.kind ||
             existing.scope != incoming.scope)) {
            tc::Log::error(
                "[%s] material vertex variant seed has conflicting resources at set=%u binding=%u: "
                "'%s' kind=%u scope=%u vs '%s' kind=%u scope=%u",
                context,
                incoming.set,
                incoming.binding,
                existing.name,
                existing.kind,
                existing.scope,
                incoming.name,
                incoming.kind,
                incoming.scope);
            return;
        }
    }
    bindings.push_back(incoming);
}

void append_seed_resource_binding(
    std::vector<tc_shader_resource_binding>& bindings,
    const char* name,
    uint32_t kind,
    uint32_t scope,
    uint32_t binding,
    uint32_t stage_mask,
    uint32_t size,
    const char* context)
{
    tc_shader_resource_binding resource{};
    std::strncpy(resource.name, name, TC_SHADER_RESOURCE_NAME_MAX - 1);
    resource.name[TC_SHADER_RESOURCE_NAME_MAX - 1] = '\0';
    resource.kind = kind;
    resource.scope = scope;
    resource.set = TC_SHADER_RESOURCE_SET_DEFAULT;
    resource.binding = binding;
    resource.stage_mask = stage_mask;
    resource.size = size;
    merge_seed_resource_binding(bindings, resource, context);
}

void seed_material_vertex_variant_resource_layout(
    tc_shader* variant,
    const tc_shader* original,
    const MaterialVertexVariantRequest& request)
{
    if (!variant) {
        return;
    }

    const char* context = material_vertex_variant_context(request);
    std::vector<tc_shader_resource_binding> bindings;
    if (original) {
        const tc_shader_resource_binding* original_bindings = tc_shader_resource_bindings(original);
        const uint32_t original_count = tc_shader_resource_binding_count(original);
        bindings.reserve(original_count + 4u);
        for (uint32_t i = 0; i < original_count; ++i) {
            merge_seed_resource_binding(bindings, original_bindings[i], context);
        }
    }

    switch (request.variant_op) {
    case TC_SHADER_VARIANT_SKINNING:
        append_seed_resource_binding(
            bindings,
            "bone_block",
            TC_SHADER_RESOURCE_CONSTANT_BUFFER,
            TC_SHADER_RESOURCE_SCOPE_DRAW,
            16u,
            TC_SHADER_STAGE_VERTEX,
            0u,
            context);
        break;
    case TC_SHADER_VARIANT_FOLIAGE:
    case TC_SHADER_VARIANT_FOLIAGE_SHADOW:
        append_seed_resource_binding(
            bindings,
            TC_SHADER_RESOURCE_PER_FRAME,
            TC_SHADER_RESOURCE_CONSTANT_BUFFER,
            TC_SHADER_RESOURCE_SCOPE_FRAME,
            2u,
            TC_SHADER_STAGE_VERTEX,
            0u,
            context);
        append_seed_resource_binding(
            bindings,
            "foliage_draw",
            TC_SHADER_RESOURCE_CONSTANT_BUFFER,
            TC_SHADER_RESOURCE_SCOPE_DRAW,
            24u,
            TC_SHADER_STAGE_VERTEX,
            128u,
            context);
        append_seed_resource_binding(
            bindings,
            "foliage_instances",
            TC_SHADER_RESOURCE_STORAGE_BUFFER,
            TC_SHADER_RESOURCE_SCOPE_DRAW,
            25u,
            TC_SHADER_STAGE_VERTEX,
            0u,
            context);
        break;
    default:
        break;
    }

    if (!bindings.empty()) {
        tc_shader_set_resource_layout(
            variant,
            bindings.data(),
            static_cast<uint32_t>(bindings.size()));
    }
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

    const std::string vertex_source =
        tgfx::load_builtin_shader_stage_source_from_catalog(request.vertex_template_uuid, "vertex");
    if (vertex_source.empty()) {
        if (should_log_material_vertex_variant_error(request, "missing_vertex_template_source")) {
            tc::Log::error(
                "[%s] failed to load material vertex variant template '%s'",
                context,
                request.vertex_template_uuid);
        }
        return TcShader();
    }

    const char* fragment_source = request.fragment_source_override
        ? request.fragment_source_override
        : original_shader.fragment_source();
    if (!fragment_source || fragment_source[0] == '\0') {
        if (should_log_material_vertex_variant_error(request, "missing_fragment_source")) {
            tc::Log::error(
                "[%s] cannot create material vertex variant for '%s': fragment source is empty",
                context,
                original_shader.name());
        }
        return TcShader();
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

    std::string variant_name = original_shader.name();
    if (variant_name.empty()) {
        variant_name = std::string("Variant_") + original_shader.uuid();
    }
    if (request.variant_name_suffix && request.variant_name_suffix[0] != '\0') {
        variant_name += request.variant_name_suffix;
    }

    char variant_uuid[40];
    tc_shader_make_variant_uuid(
        variant_uuid,
        sizeof(variant_uuid),
        original_shader.uuid(),
        request.variant_op);

    const char* vertex_entry = request.vertex_entry && request.vertex_entry[0] != '\0'
        ? request.vertex_entry
        : "vs_main";
    tc_shader* original_raw = original_shader.get();
    const char* fragment_entry = request.fragment_entry_override
        ? request.fragment_entry_override
        : (original_raw ? original_raw->fragment_entry : nullptr);
    if (!fragment_entry || fragment_entry[0] == '\0') {
        fragment_entry = "main";
    }

    tc_shader_handle handle = tc_shader_from_sources_with_entries_ex(
        vertex_source.c_str(),
        fragment_source,
        nullptr,
        variant_name.c_str(),
        original_shader.source_path(),
        variant_uuid,
        TC_SHADER_LANGUAGE_SLANG,
        TC_SHADER_ARTIFACT_REQUIRED,
        vertex_entry,
        fragment_entry,
        nullptr);
    if (tc_shader_handle_is_invalid(handle)) {
        tc::Log::error(
            "[%s] failed to create material vertex variant for '%s'",
            context,
            original_shader.name());
        return TcShader();
    }

    TcShader variant(handle);
    variant.set_features(original_shader.features());
    variant.set_language(TC_SHADER_LANGUAGE_SLANG);
    variant.set_artifact_policy(TC_SHADER_ARTIFACT_REQUIRED);

    tc_shader* variant_raw = variant.get();
    if (original_raw && variant_raw) {
        // Material vertex variants must get field/resource metadata from
        // shaderc sidecar reflection. Parser-authored legacy material UBO
        // entries would create a second source of truth.
        tc_shader_set_material_ubo_layout(variant_raw, nullptr, 0, 0);
        seed_material_vertex_variant_resource_layout(variant_raw, original_raw, request);
    }

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
