#include <termin/render/render_engine.hpp>

#include <tcbase/tc_log.hpp>
#include "tc_profiler.h"
#include "tc_project_settings.h"

extern "C" {
#include "render/tc_frame_graph.h"
#include "render/tc_pass.h"
#include "render/tc_pipeline.h"
#include "core/tc_scene.h"
#include "core/tc_component.h"
}

namespace termin {

RenderEngine::RenderEngine(GraphicsBackend* graphics)
    : graphics(graphics)
{
}

void RenderEngine::render_to_screen(
    RenderPipeline& pipeline,
    int width,
    int height,
    tc_scene_handle scene,
    const RenderCamera& camera
) {
    if (!pipeline.is_valid()) {
        tc::Log::error("[render_to_screen] pipeline is NULL");
        return;
    }
    if (!tc_scene_handle_valid(scene)) {
        tc::Log::error("[render_to_screen] scene is invalid");
        return;
    }
    std::vector<Light> empty_lights;
    render_view_to_fbo(
        pipeline,
        nullptr,
        width,
        height,
        scene,
        camera,
        "",
        TC_ENTITY_HANDLE_INVALID,
        empty_lights,
        0xFFFFFFFFFFFFFFFFULL
    );
}

void RenderEngine::present_to_screen(
    RenderPipeline& pipeline,
    int width,
    int height,
    const std::string& resource_name
) {
    if (!pipeline.is_valid() || !graphics) {
        tc::Log::warn("[present_to_screen] pipeline=%p graphics=%p", pipeline, graphics);
        return;
    }

    FramebufferHandle* src_fbo = pipeline.fbo_pool().get(resource_name);
    if (!src_fbo) {
        tc::Log::warn("[present_to_screen] FBO '%s' not found in pipeline. Available FBOs:", resource_name.c_str());
        auto& pool = pipeline.fbo_pool();
        for (const auto& key : pool.keys()) {
            auto* fbo = pool.get(key);
            tc::Log::warn("  - '%s': %p", key.c_str(), fbo);
        }
        return;
    }

    graphics->blit_framebuffer(
        src_fbo,
        nullptr,
        0, 0, src_fbo->get_width(), src_fbo->get_height(),
        0, 0, width, height,
        true,
        false
    );
}

void RenderEngine::render_view_to_fbo(
    RenderPipeline& pipeline,
    FramebufferHandle* target_fbo,
    int width,
    int height,
    tc_scene_handle scene,
    const RenderCamera& camera,
    const std::string& viewport_name,
    tc_entity_handle internal_entities,
    const std::vector<Light>& lights,
    uint64_t layer_mask
) {
    if (!pipeline.is_valid()) {
        tc::Log::error("RenderEngine::render_view_to_fbo: pipeline is null");
        return;
    }
    if (!pipeline.is_valid()) {
        tc::Log::error("RenderEngine::render_view_to_fbo: pipeline is invalid");
        return;
    }
    if (!graphics) {
        tc::Log::error("RenderEngine::render_view_to_fbo: graphics is null");
        return;
    }

    tc_frame_graph* fg = tc_pipeline_get_frame_graph(pipeline.handle());
    if (!fg) {
        tc::Log::error("RenderEngine::render_view_to_fbo: failed to get frame graph");
        return;
    }

    if (tc_frame_graph_get_error(fg) != TC_FG_OK) {
        tc::Log::error("RenderEngine::render_view_to_fbo: frame graph error: %s",
                       tc_frame_graph_get_error_message(fg));
        return;
    }

    auto specs = pipeline.collect_specs();

    std::unordered_map<std::string, ResourceSpec> spec_map;
    for (const auto& spec : specs) {
        auto it = spec_map.find(spec.resource);
        if (it == spec_map.end()) {
            spec_map[spec.resource] = spec;
        } else {
            ResourceSpec& existing = it->second;
            if (spec.samples > 1 && existing.samples == 1) {
                existing.samples = spec.samples;
            }
            if (spec.format && !existing.format) {
                existing.format = spec.format;
            }
            if (spec.clear_color && !existing.clear_color) {
                existing.clear_color = spec.clear_color;
            }
            if (spec.clear_depth && !existing.clear_depth) {
                existing.clear_depth = spec.clear_depth;
            }
        }
    }

    FBOMap resources;
    resources["OUTPUT"] = target_fbo;
    resources["DISPLAY"] = target_fbo;

    const char* canonical_names[256];
    size_t canon_count = tc_frame_graph_get_canonical_resources(fg, canonical_names, 256);

    for (size_t i = 0; i < canon_count; i++) {
        const char* canon = canonical_names[i];

        if (strcmp(canon, "OUTPUT") == 0 || strcmp(canon, "DISPLAY") == 0) {
            const char* aliases[64];
            size_t alias_count = tc_frame_graph_get_alias_group(fg, canon, aliases, 64);
            for (size_t j = 0; j < alias_count; j++) {
                resources[aliases[j]] = target_fbo;
            }
            continue;
        }

        const ResourceSpec* spec = nullptr;
        auto it = spec_map.find(canon);
        if (it != spec_map.end()) {
            spec = &it->second;
        } else {
            const char* aliases[64];
            size_t alias_count = tc_frame_graph_get_alias_group(fg, canon, aliases, 64);
            for (size_t j = 0; j < alias_count && !spec; j++) {
                auto ait = spec_map.find(aliases[j]);
                if (ait != spec_map.end()) {
                    spec = &ait->second;
                }
            }
        }

        std::string resource_type = "fbo";
        if (spec && !spec->resource_type.empty()) {
            resource_type = spec->resource_type;
        }

        if (resource_type == "shadow_map_array") {
            auto& shadow_array = pipeline.shadow_arrays()[canon];
            if (!shadow_array) {
                int resolution = 1024;
                if (spec && spec->size) {
                    resolution = spec->size->first;
                }
                shadow_array = std::make_unique<ShadowMapArrayResource>(resolution);
            }

            const char* aliases[64];
            size_t alias_count = tc_frame_graph_get_alias_group(fg, canon, aliases, 64);
            for (size_t j = 0; j < alias_count; j++) {
                resources[aliases[j]] = shadow_array.get();
            }
            continue;
        }

        if (resource_type != "fbo") {
            const char* aliases[64];
            size_t alias_count = tc_frame_graph_get_alias_group(fg, canon, aliases, 64);
            for (size_t j = 0; j < alias_count; j++) {
                resources[aliases[j]] = nullptr;
            }
            continue;
        }

        int fbo_width = width;
        int fbo_height = height;
        int samples = 1;
        std::string format;
        TextureFilter filter = TextureFilter::LINEAR;

        if (spec) {
            if (spec->size) {
                fbo_width = spec->size->first;
                fbo_height = spec->size->second;
            }
            samples = spec->samples > 0 ? spec->samples : 1;
            if (spec->format) format = *spec->format;
            filter = spec->filter;
        }

        FBOPool& fbo_pool = pipeline.fbo_pool();
        FramebufferHandle* fbo = fbo_pool.ensure(graphics, canon, fbo_width, fbo_height, samples, format, filter);

        const char* aliases[64];
        size_t alias_count = tc_frame_graph_get_alias_group(fg, canon, aliases, 64);
        for (size_t j = 0; j < alias_count; j++) {
            resources[aliases[j]] = fbo;
            fbo_pool.add_alias(aliases[j], canon);
        }
    }

    for (const auto& spec : specs) {
        if (spec.resource_type != "fbo" && !spec.resource_type.empty()) {
            continue;
        }
        if (!spec.clear_color && !spec.clear_depth) {
            continue;
        }

        auto it = resources.find(spec.resource);
        if (it == resources.end() || it->second == nullptr) {
            continue;
        }

        FrameGraphResource* resource = it->second;
        FramebufferHandle* fbo = nullptr;
        try {
            fbo = dynamic_cast<FramebufferHandle*>(resource);
        } catch (const std::exception& e) {
            tc::Log::error("[render_view_to_fbo] dynamic_cast failed: %s", e.what());
            continue;
        }

        if (!fbo) {
            tc::Log::warn("[render_view_to_fbo] dynamic_cast returned nullptr for resource='%s'",
                          spec.resource.c_str());
            continue;
        }

        graphics->bind_framebuffer(fbo);

        int fb_w = spec.size ? spec.size->first : width;
        int fb_h = spec.size ? spec.size->second : height;
        graphics->set_viewport(0, 0, fb_w, fb_h);

        if (spec.clear_color && spec.clear_depth) {
            const auto& cc = *spec.clear_color;
            graphics->clear_color_depth(
                static_cast<float>(cc[0]), static_cast<float>(cc[1]),
                static_cast<float>(cc[2]), static_cast<float>(cc[3])
            );
        } else if (spec.clear_color) {
            const auto& cc = *spec.clear_color;
            graphics->clear_color(
                static_cast<float>(cc[0]), static_cast<float>(cc[1]),
                static_cast<float>(cc[2]), static_cast<float>(cc[3])
            );
        } else if (spec.clear_depth) {
            graphics->clear_depth(*spec.clear_depth);
        }
    }

    size_t schedule_count = tc_frame_graph_schedule_count(fg);

    tc_profiler_begin_section("Execute Passes");
    for (size_t i = 0; i < schedule_count; i++) {
        tc_pass* pass = tc_frame_graph_schedule_at(fg, i);
        if (!pass || !pass->enabled || pass->passthrough) {
            continue;
        }

        const char* pass_name = pass->pass_name ? pass->pass_name : "UnnamedPass";
        tc_profiler_begin_section(pass_name);

        graphics->reset_state();

        const char* reads[16];
        const char* writes[8];
        size_t read_count = tc_pass_get_reads(pass, reads, 16);
        size_t write_count = tc_pass_get_writes(pass, writes, 8);

        FBOMap pass_reads;
        FBOMap pass_writes;

        for (size_t j = 0; j < read_count; j++) {
            auto it = resources.find(reads[j]);
            pass_reads[reads[j]] = (it != resources.end()) ? it->second : nullptr;
        }
        for (size_t j = 0; j < write_count; j++) {
            auto it = resources.find(writes[j]);
            pass_writes[writes[j]] = (it != resources.end()) ? it->second : nullptr;
        }

        ExecuteContext ctx;
        ctx.graphics = graphics;
        ctx.reads_fbos = std::move(pass_reads);
        ctx.writes_fbos = std::move(pass_writes);
        ctx.rect = Rect4i{0, 0, width, height};
        ctx.scene = TcSceneRef(scene);
        ctx.viewport_name = viewport_name;
        ctx.internal_entities = internal_entities;
        ctx.camera = const_cast<RenderCamera*>(&camera);
        ctx.lights = lights;
        ctx.layer_mask = layer_mask;

        tc_pass_execute(pass, &ctx);

        tc_profiler_begin_section("Sync Operations");
        tc_render_sync_mode sync_mode = tc_project_settings_get_render_sync_mode();
        if (sync_mode == TC_RENDER_SYNC_FLUSH) {
            graphics->flush();
        } else if (sync_mode == TC_RENDER_SYNC_FINISH) {
            graphics->finish();
        }
        tc_profiler_end_section();

        tc_profiler_end_section();
    }
    tc_profiler_end_section();
}

void RenderEngine::render_scene_pipeline_offscreen(
    RenderPipeline& pipeline,
    tc_scene_handle scene,
    const std::unordered_map<std::string, ViewportContext>& viewport_contexts,
    const std::vector<Light>& lights,
    const std::string& default_viewport
) {
    if (!pipeline.is_valid()) {
        tc::Log::error("RenderEngine::render_scene_pipeline_offscreen: pipeline is null");
        return;
    }
    if (!graphics) {
        tc::Log::error("RenderEngine::render_scene_pipeline_offscreen: graphics is null");
        return;
    }
    if (viewport_contexts.empty()) {
        tc::Log::error("RenderEngine::render_scene_pipeline_offscreen: no viewport contexts");
        return;
    }

    std::string default_vp = default_viewport;
    if (default_vp.empty()) {
        default_vp = viewport_contexts.begin()->first;
    }

    auto default_it = viewport_contexts.find(default_vp);
    if (default_it == viewport_contexts.end()) {
        default_it = viewport_contexts.begin();
    }
    const ViewportContext& default_ctx = default_it->second;

    int default_width = default_ctx.rect.width;
    int default_height = default_ctx.rect.height;

    tc_profiler_begin_section("Get Frame Graph");
    tc_frame_graph* fg = tc_pipeline_get_frame_graph(pipeline.handle());
    if (!fg) {
        tc_profiler_end_section();
        tc::Log::error("RenderEngine::render_scene_pipeline_offscreen: failed to get frame graph");
        return;
    }

    if (tc_frame_graph_get_error(fg) != TC_FG_OK) {
        tc::Log::error("RenderEngine::render_scene_pipeline_offscreen: frame graph error: %s",
                       tc_frame_graph_get_error_message(fg));
        tc_profiler_end_section();
        return;
    }
    tc_profiler_end_section();

    tc_profiler_begin_section("Collect Specs");
    auto specs = pipeline.collect_specs();

    std::unordered_map<std::string, ResourceSpec> spec_map;
    for (const auto& spec : specs) {
        auto it = spec_map.find(spec.resource);
        if (it == spec_map.end()) {
            spec_map[spec.resource] = spec;
        } else {
            ResourceSpec& existing = it->second;
            if (spec.samples > 1 && existing.samples == 1) {
                existing.samples = spec.samples;
            }
            if (spec.format && !existing.format) {
                existing.format = spec.format;
            }
            if (spec.clear_color && !existing.clear_color) {
                existing.clear_color = spec.clear_color;
            }
            if (spec.clear_depth && !existing.clear_depth) {
                existing.clear_depth = spec.clear_depth;
            }
        }
    }
    tc_profiler_end_section();

    tc_profiler_begin_section("Allocate Resources");
    FBOMap resources;
    resources["OUTPUT"] = default_ctx.output_fbo;
    resources["DISPLAY"] = default_ctx.output_fbo;

    const char* canonical_names[256];
    size_t canon_count = tc_frame_graph_get_canonical_resources(fg, canonical_names, 256);

    for (size_t i = 0; i < canon_count; i++) {
        const char* canon = canonical_names[i];

        if (strcmp(canon, "OUTPUT") == 0 || strcmp(canon, "DISPLAY") == 0) {
            const char* aliases[64];
            size_t alias_count = tc_frame_graph_get_alias_group(fg, canon, aliases, 64);
            for (size_t j = 0; j < alias_count; j++) {
                resources[aliases[j]] = default_ctx.output_fbo;
            }
            continue;
        }

        const ResourceSpec* spec = nullptr;
        auto it = spec_map.find(canon);
        if (it != spec_map.end()) {
            spec = &it->second;
        } else {
            const char* aliases[64];
            size_t alias_count = tc_frame_graph_get_alias_group(fg, canon, aliases, 64);
            for (size_t j = 0; j < alias_count && !spec; j++) {
                auto ait = spec_map.find(aliases[j]);
                if (ait != spec_map.end()) {
                    spec = &ait->second;
                }
            }
        }

        std::string resource_type = "fbo";
        if (spec && !spec->resource_type.empty()) {
            resource_type = spec->resource_type;
        }

        if (resource_type == "shadow_map_array") {
            auto& shadow_array = pipeline.shadow_arrays()[canon];
            if (!shadow_array) {
                int resolution = 1024;
                if (spec && spec->size) {
                    resolution = spec->size->first;
                }
                shadow_array = std::make_unique<ShadowMapArrayResource>(resolution);
            }

            const char* aliases[64];
            size_t alias_count = tc_frame_graph_get_alias_group(fg, canon, aliases, 64);
            for (size_t j = 0; j < alias_count; j++) {
                resources[aliases[j]] = shadow_array.get();
            }
            continue;
        }

        if (resource_type != "fbo") {
            const char* aliases[64];
            size_t alias_count = tc_frame_graph_get_alias_group(fg, canon, aliases, 64);
            for (size_t j = 0; j < alias_count; j++) {
                resources[aliases[j]] = nullptr;
            }
            continue;
        }

        int fbo_width = default_width;
        int fbo_height = default_height;
        int samples = 1;
        std::string format;
        TextureFilter filter = TextureFilter::LINEAR;

        if (spec) {
            if (spec->size) {
                fbo_width = spec->size->first;
                fbo_height = spec->size->second;
            }
            samples = spec->samples > 0 ? spec->samples : 1;
            if (spec->format) format = *spec->format;
            filter = spec->filter;
        }

        FBOPool& fbo_pool = pipeline.fbo_pool();
        FramebufferHandle* fbo = fbo_pool.ensure(graphics, canon, fbo_width, fbo_height, samples, format, filter);

        const char* aliases[64];
        size_t alias_count = tc_frame_graph_get_alias_group(fg, canon, aliases, 64);
        for (size_t j = 0; j < alias_count; j++) {
            resources[aliases[j]] = fbo;
            fbo_pool.add_alias(aliases[j], canon);
        }
    }
    tc_profiler_end_section();

    tc_profiler_begin_section("Clear Resources");
    for (const auto& spec : specs) {
        if (spec.resource_type != "fbo" && !spec.resource_type.empty()) {
            continue;
        }
        if (!spec.clear_color && !spec.clear_depth) {
            continue;
        }

        auto it = resources.find(spec.resource);
        if (it == resources.end() || it->second == nullptr) {
            continue;
        }

        FrameGraphResource* resource = it->second;
        FramebufferHandle* fbo = nullptr;
        try {
            fbo = dynamic_cast<FramebufferHandle*>(resource);
        } catch (const std::exception& e) {
            tc::Log::error("[render_scene_pipeline_offscreen] dynamic_cast failed: %s", e.what());
            continue;
        }

        if (!fbo) {
            tc::Log::warn("[render_scene_pipeline_offscreen] dynamic_cast returned nullptr for resource='%s'",
                          spec.resource.c_str());
            continue;
        }

        graphics->bind_framebuffer(fbo);

        int fb_w = spec.size ? spec.size->first : default_width;
        int fb_h = spec.size ? spec.size->second : default_height;
        graphics->set_viewport(0, 0, fb_w, fb_h);

        if (spec.clear_color && spec.clear_depth) {
            const auto& cc = *spec.clear_color;
            graphics->clear_color_depth(
                static_cast<float>(cc[0]), static_cast<float>(cc[1]),
                static_cast<float>(cc[2]), static_cast<float>(cc[3])
            );
        } else if (spec.clear_color) {
            const auto& cc = *spec.clear_color;
            graphics->clear_color(
                static_cast<float>(cc[0]), static_cast<float>(cc[1]),
                static_cast<float>(cc[2]), static_cast<float>(cc[3])
            );
        } else if (spec.clear_depth) {
            graphics->clear_depth(*spec.clear_depth);
        }
    }
    tc_profiler_end_section();

    size_t schedule_count = tc_frame_graph_schedule_count(fg);

    tc_profiler_begin_section("Execute Passes");
    for (size_t i = 0; i < schedule_count; i++) {
        tc_pass* pass = tc_frame_graph_schedule_at(fg, i);
        if (!pass || !pass->enabled || pass->passthrough) {
            continue;
        }

        const char* pass_name = pass->pass_name ? pass->pass_name : "UnnamedPass";
        tc_profiler_begin_section(pass_name);

        graphics->reset_state();

        std::string pass_viewport_name = default_vp;
        if (pass->viewport_name && pass->viewport_name[0] != '\0') {
            pass_viewport_name = pass->viewport_name;
        }

        auto vp_it = viewport_contexts.find(pass_viewport_name);
        if (vp_it == viewport_contexts.end()) {
            vp_it = default_it;
        }
        const ViewportContext& vp_ctx = vp_it->second;

        const char* reads[16];
        const char* writes[8];
        size_t read_count = tc_pass_get_reads(pass, reads, 16);
        size_t write_count = tc_pass_get_writes(pass, writes, 8);

        FBOMap pass_reads;
        FBOMap pass_writes;

        for (size_t j = 0; j < read_count; j++) {
            auto it = resources.find(reads[j]);
            FrameGraphResource* res = (it != resources.end()) ? it->second : nullptr;
            pass_reads[reads[j]] = res;
        }

        for (size_t j = 0; j < write_count; j++) {
            const char* write_name = writes[j];
            if (strcmp(write_name, "OUTPUT") == 0 || strcmp(write_name, "DISPLAY") == 0) {
                pass_writes[write_name] = vp_ctx.output_fbo;
            } else {
                auto it = resources.find(write_name);
                FrameGraphResource* res = (it != resources.end()) ? it->second : nullptr;
                pass_writes[write_name] = res;
            }
        }

        ExecuteContext ctx;
        ctx.graphics = graphics;
        ctx.reads_fbos = std::move(pass_reads);
        ctx.writes_fbos = std::move(pass_writes);
        ctx.rect = vp_ctx.rect;
        ctx.scene = TcSceneRef(scene);
        ctx.viewport_name = vp_ctx.name;
        ctx.internal_entities = vp_ctx.internal_entities;
        ctx.camera = const_cast<RenderCamera*>(&vp_ctx.camera);
        ctx.lights = lights;
        ctx.layer_mask = vp_ctx.layer_mask;

        tc_pass_execute(pass, &ctx);

        tc_render_sync_mode sync_mode = tc_project_settings_get_render_sync_mode();
        if (sync_mode == TC_RENDER_SYNC_FLUSH) {
            graphics->flush();
        } else if (sync_mode == TC_RENDER_SYNC_FINISH) {
            graphics->finish();
        }

        tc_profiler_end_section();
    }
    tc_profiler_end_section();
}

} // namespace termin
