#pragma once

#include <cstddef>
#include <vector>

#include <termin/render/render_export.hpp>
#include <termin/render/tc_scene_render_accessors.hpp>
#include <termin/render/tc_pipeline_template.hpp>
#include <termin/tc_scene.hpp>
#include <termin/viewport_config.hpp>
#include <termin/render_target_config.hpp>

namespace nos {
class trent;
}

namespace termin {

// --- Viewport configs ---

RENDER_API void scene_add_viewport_config(const TcSceneRef& scene, const ViewportConfig& config);
RENDER_API void scene_remove_viewport_config(const TcSceneRef& scene, size_t index);
RENDER_API void scene_clear_viewport_configs(const TcSceneRef& scene);
RENDER_API size_t scene_viewport_config_count(const TcSceneRef& scene);
RENDER_API ViewportConfig scene_viewport_config_at(const TcSceneRef& scene, size_t index);
RENDER_API std::vector<ViewportConfig> scene_viewport_configs(const TcSceneRef& scene);

// --- Render target configs ---

RENDER_API void scene_add_render_target_config(const TcSceneRef& scene, const RenderTargetConfig& config);
RENDER_API void scene_remove_render_target_config(const TcSceneRef& scene, size_t index);
RENDER_API void scene_clear_render_target_configs(const TcSceneRef& scene);
RENDER_API size_t scene_render_target_config_count(const TcSceneRef& scene);
RENDER_API RenderTargetConfig scene_render_target_config_at(const TcSceneRef& scene, size_t index);
RENDER_API std::vector<RenderTargetConfig> scene_render_target_configs(const TcSceneRef& scene);

// --- Canonical pipeline templates ---

RENDER_API bool scene_add_pipeline_template(
    const TcSceneRef& scene,
    const TcPipelineTemplate& pipeline_template);
RENDER_API void scene_clear_pipeline_templates(const TcSceneRef& scene);
RENDER_API size_t scene_pipeline_template_count(const TcSceneRef& scene);
RENDER_API TcPipelineTemplate scene_pipeline_template_at(const TcSceneRef& scene, size_t index);

// --- Legacy adapter for load_from_data render mount/state ---

// Merge legacy render_mount/render_state fields into extensions dict.
RENDER_API void scene_merge_legacy_render_extensions(const nos::trent& data, nos::trent& merged_extensions);

} // namespace termin
