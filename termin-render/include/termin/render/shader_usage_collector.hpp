#pragma once

#include <vector>

#include <termin/render/render_export.hpp>
#include <termin/tc_scene.hpp>
#include <tgfx/tgfx_shader_handle.hpp>

namespace termin {

class RenderPipeline;

RENDER_API std::vector<TcShader> collect_scene_shader_usages(tc_scene_handle scene);
RENDER_API std::vector<TcShader> collect_shader_usages_for_pipeline(
    tc_scene_handle scene,
    const RenderPipeline& pipeline);

} // namespace termin
