#include <termin/render/shader_usage_collector.hpp>

#include <string>
#include <cstdint>
#include <unordered_set>
#include <vector>

#include <tcbase/tc_log.hpp>
#include <core/tc_drawable_protocol.h>
#include <core/tc_scene.h>
#include <core/tc_scene_drawable.h>
#include <termin/render/drawable.hpp>
#include <termin/render/frame_pass.hpp>
#include <termin/render/render_pipeline.hpp>
#include <tgfx/resources/tc_material.h>

namespace termin {

namespace {

struct ShaderUsageCollectContext {
    std::vector<TcShader>* shaders = nullptr;
    std::unordered_set<std::string>* seen = nullptr;
    const char* log_context = "collect_scene_shader_usages";
};

static void append_shader_usage(TcShader shader, ShaderUsageCollectContext* ctx) {
    if (!ctx || !ctx->shaders || !ctx->seen) {
        return;
    }

    if (!shader.is_valid()) {
        tc::Log::warn("%s: drawable emitted invalid shader", ctx->log_context);
        return;
    }

    const char* uuid = shader.uuid();
    if (!uuid || uuid[0] == '\0') {
        tc::Log::warn("%s: shader '%s' has empty uuid",
                      ctx->log_context,
                      shader.name());
        return;
    }

    if (!shader.fragment_source() || shader.fragment_source()[0] == '\0') {
        tc::Log::warn("%s: shader '%s' [%s] has no fragment source",
                      ctx->log_context,
                      shader.name(), uuid);
        return;
    }

    if (ctx->seen->insert(uuid).second) {
        ctx->shaders->push_back(shader);
    }
}

static void emit_shader_usage(tc_component* self, tc_shader_handle shader_handle, void* user_data) {
    (void)self;
    auto* ctx = static_cast<ShaderUsageCollectContext*>(user_data);
    append_shader_usage(TcShader(shader_handle), ctx);
}

static bool collect_drawable_shader_usages(tc_component* component, void* user_data) {
    auto* ctx = static_cast<ShaderUsageCollectContext*>(user_data);
    if (!ctx) {
        return true;
    }

    RenderContext render_context;
    render_context.layer_mask = UINT64_MAX;
    render_context.render_category_mask = UINT64_MAX;
    void* draws_ptr = tc_component_get_geometry_draws(component, &render_context, nullptr);
    if (!draws_ptr) {
        return true;
    }

    auto* geometry_draws = static_cast<std::vector<GeometryDrawCall>*>(draws_ptr);
    for (const auto& draw : *geometry_draws) {
        tc_material_phase* phase = draw.resolve_phase();
        if (!phase) {
            continue;
        }

        tc_component_collect_shader_usages(
            component,
            phase->phase_mark,
            draw.geometry_id,
            phase->shader,
            emit_shader_usage,
            ctx
        );
    }

    return true;
}

} // namespace

std::vector<TcShader> collect_scene_shader_usages(tc_scene_handle scene) {
    std::vector<TcShader> shaders;
    std::unordered_set<std::string> seen;

    if (!tc_scene_handle_valid(scene)) {
        tc::Log::error("collect_scene_shader_usages: invalid scene handle");
        return shaders;
    }

    ShaderUsageCollectContext ctx{&shaders, &seen, "collect_scene_shader_usages"};
    // Build artifacts must cover potential scene usage, not just the current
    // runtime-visible subset.
    tc_scene_foreach_drawable(scene, collect_drawable_shader_usages, &ctx, TC_SCENE_FILTER_NONE, 0);
    return shaders;
}

std::vector<TcShader> collect_shader_usages_for_pipeline(
    tc_scene_handle scene,
    const RenderPipeline& pipeline)
{
    std::vector<TcShader> shaders;
    std::unordered_set<std::string> seen;

    if (!tc_scene_handle_valid(scene)) {
        tc::Log::error("collect_shader_usages_for_pipeline: invalid scene handle");
        return shaders;
    }
    if (!pipeline.is_valid()) {
        tc::Log::error("collect_shader_usages_for_pipeline: invalid render pipeline");
        return shaders;
    }

    ShaderUsageCollectContext ctx{&shaders, &seen, "collect_shader_usages_for_pipeline"};
    const size_t pass_count = pipeline.pass_count();
    for (size_t i = 0; i < pass_count; ++i) {
        const tc_pass* pass = pipeline.get_pass_at(i);
        if (!pass || !pass->enabled || pass->passthrough) {
            continue;
        }

        const CxxFramePass* cxx_pass = CxxFramePass::from_tc(pass);
        if (!cxx_pass) {
            tc::Log::warn(
                "collect_shader_usages_for_pipeline: skipping non-C++ pass '%s'",
                tc_pass_type_name(pass));
            continue;
        }

        cxx_pass->collect_shader_usages(
            scene,
            [&](TcShader shader) {
                append_shader_usage(shader, &ctx);
            });
    }

    return shaders;
}

} // namespace termin
