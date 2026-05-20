#include <termin/render/shader_usage_collector.hpp>

#include <string>
#include <unordered_set>
#include <vector>

#include <tcbase/tc_log.hpp>
#include <core/tc_drawable_protocol.h>
#include <core/tc_scene.h>
#include <core/tc_scene_drawable.h>
#include <termin/render/drawable.hpp>
#include <tgfx/resources/tc_material.h>

namespace termin {

namespace {

struct ShaderUsageCollectContext {
    std::vector<TcShader>* shaders = nullptr;
    std::unordered_set<std::string>* seen = nullptr;
};

static void emit_shader_usage(tc_component* self, tc_shader_handle shader_handle, void* user_data) {
    (void)self;
    auto* ctx = static_cast<ShaderUsageCollectContext*>(user_data);
    if (!ctx || !ctx->shaders || !ctx->seen) {
        return;
    }

    TcShader shader(shader_handle);
    if (!shader.is_valid()) {
        tc::Log::warn("collect_scene_shader_usages: drawable emitted invalid shader");
        return;
    }

    const char* uuid = shader.uuid();
    if (!uuid || uuid[0] == '\0') {
        tc::Log::warn("collect_scene_shader_usages: shader '%s' has empty uuid",
                      shader.name());
        return;
    }

    if (!shader.fragment_source() || shader.fragment_source()[0] == '\0') {
        tc::Log::warn("collect_scene_shader_usages: shader '%s' [%s] has no fragment source",
                      shader.name(), uuid);
        return;
    }

    if (ctx->seen->insert(uuid).second) {
        ctx->shaders->push_back(shader);
    }
}

static bool collect_drawable_shader_usages(tc_component* component, void* user_data) {
    auto* ctx = static_cast<ShaderUsageCollectContext*>(user_data);
    if (!ctx) {
        return true;
    }

    void* draws_ptr = tc_component_get_geometry_draws(component, nullptr);
    if (!draws_ptr) {
        return true;
    }

    auto* geometry_draws = static_cast<std::vector<GeometryDrawCall>*>(draws_ptr);
    for (const auto& draw : *geometry_draws) {
        if (!draw.phase) {
            continue;
        }

        tc_component_collect_shader_usages(
            component,
            draw.phase->phase_mark,
            draw.geometry_id,
            draw.phase->shader,
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

    ShaderUsageCollectContext ctx{&shaders, &seen};
    // Build artifacts must cover potential scene usage, not just the current
    // runtime-visible subset.
    tc_scene_foreach_drawable(scene, collect_drawable_shader_usages, &ctx, TC_SCENE_FILTER_NONE, 0);
    return shaders;
}

} // namespace termin
