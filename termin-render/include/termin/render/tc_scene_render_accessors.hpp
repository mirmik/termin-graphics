#pragma once

#include <tuple>

#include <termin/geom/vec3.hpp>
#include <termin/geom/vec4.hpp>
#include <termin/render/render_export.hpp>
#include <termin/tc_scene.hpp>
#include "core/tc_scene_render_state.h"

namespace termin {

RENDER_API std::tuple<float, float, float, float> scene_get_background_color(const TcSceneRef& scene);
RENDER_API void scene_set_background_color(const TcSceneRef& scene, float r, float g, float b, float a);
RENDER_API Vec4 scene_background_color(const TcSceneRef& scene);
RENDER_API void scene_set_background_color(const TcSceneRef& scene, const Vec4& color);

RENDER_API std::tuple<float, float, float> scene_get_skybox_color_components(const TcSceneRef& scene);
RENDER_API void scene_set_skybox_color_components(const TcSceneRef& scene, float r, float g, float b);
RENDER_API Vec3 scene_skybox_color(const TcSceneRef& scene);
RENDER_API void scene_set_skybox_color(const TcSceneRef& scene, const Vec3& color);

RENDER_API std::tuple<float, float, float> scene_get_skybox_top_color_components(const TcSceneRef& scene);
RENDER_API void scene_set_skybox_top_color_components(const TcSceneRef& scene, float r, float g, float b);
RENDER_API Vec3 scene_skybox_top_color(const TcSceneRef& scene);
RENDER_API void scene_set_skybox_top_color(const TcSceneRef& scene, const Vec3& color);

RENDER_API std::tuple<float, float, float> scene_get_skybox_bottom_color_components(const TcSceneRef& scene);
RENDER_API void scene_set_skybox_bottom_color_components(const TcSceneRef& scene, float r, float g, float b);
RENDER_API Vec3 scene_skybox_bottom_color(const TcSceneRef& scene);
RENDER_API void scene_set_skybox_bottom_color(const TcSceneRef& scene, const Vec3& color);

RENDER_API std::tuple<float, float, float> scene_get_ambient_color_components(const TcSceneRef& scene);
RENDER_API void scene_set_ambient_color_components(const TcSceneRef& scene, float r, float g, float b);
RENDER_API Vec3 scene_ambient_color(const TcSceneRef& scene);
RENDER_API void scene_set_ambient_color(const TcSceneRef& scene, const Vec3& color);

RENDER_API float scene_ambient_intensity(const TcSceneRef& scene);
RENDER_API void scene_set_ambient_intensity(const TcSceneRef& scene, float intensity);

RENDER_API tc_scene_lighting* scene_lighting(const TcSceneRef& scene);

} // namespace termin
