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

} // namespace

std::vector<TcShader> collect_scene_shader_usages(tc_scene_handle scene) {
    std::vector<TcShader> shaders;

    if (!tc_scene_handle_valid(scene)) {
        tc::Log::error("collect_scene_shader_usages: invalid scene handle");
        return shaders;
    }

    tc::Log::error(
        "collect_scene_shader_usages: scene-only drawable shader collection was removed; "
        "use collect_shader_usages_for_pipeline(scene, pipeline) so passes provide RenderItem phases and shader contracts");
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
