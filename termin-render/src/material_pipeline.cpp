#include "termin/render/material_pipeline.hpp"

#include "termin/render/material_ubo_apply.hpp"
#include "termin/render/shader_resource_apply.hpp"
#include "tgfx2/render_context.hpp"
#include "tgfx2/tc_shader_bridge.hpp"
#include "tcbase/tc_log.hpp"

extern "C" {
#include "tgfx/resources/tc_shader_registry.h"
}

namespace termin {

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
    const MaterialPipelineResourceContext& resources,
    const MaterialPipelineFallbackBindings& fallback)
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
            resources.shadow_block_size,
            fallback.shadow_block);
    }

    if (resources.lighting_ubo &&
        tc_shader_has_feature(shader, TC_SHADER_FEATURE_LIGHTING_UBO)) {
        bound_any |= bind_lighting_ubo_for_shader(
            ctx,
            shader,
            resources.lighting_ubo,
            fallback.lighting_ubo);
    }

    if (phase) {
        bound_any |= apply_material_phase_ubo(
            phase,
            shader,
            fallback.material_ubo,
            fallback.material_texture_base,
            device,
            ctx);
    }

    if (!resources.shadow_maps.empty() && resources.shadow_sampler) {
        bound_any |= bind_shadow_maps_for_shader(
            ctx,
            shader,
            resources.shadow_maps,
            resources.shadow_sampler,
            fallback.shadow_map_base,
            fallback.max_shadow_maps);
    }

    return bound_any;
}

} // namespace termin
