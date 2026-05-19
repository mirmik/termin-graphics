#pragma once

#include <vector>

#include <termin/render/render_export.hpp>
#include <termin/tc_scene.hpp>
#include <tgfx/tgfx_shader_handle.hpp>

namespace termin {

RENDER_API std::vector<TcShader> collect_scene_shader_usages(tc_scene_handle scene);

} // namespace termin
