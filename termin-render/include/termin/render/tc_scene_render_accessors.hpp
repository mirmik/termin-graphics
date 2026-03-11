#pragma once

#include <tuple>

#include <termin/geom/vec3.hpp>
#include <termin/geom/vec4.hpp>
#include <termin/tc_scene.hpp>
#include "core/tc_scene_render_state.h"

namespace termin {

std::tuple<float, float, float, float> scene_get_background_color(const TcSceneRef& scene);
void scene_set_background_color(const TcSceneRef& scene, float r, float g, float b, float a);
Vec4 scene_background_color(const TcSceneRef& scene);
void scene_set_background_color(const TcSceneRef& scene, const Vec4& color);

std::tuple<float, float, float> scene_get_skybox_color_components(const TcSceneRef& scene);
void scene_set_skybox_color_components(const TcSceneRef& scene, float r, float g, float b);
Vec3 scene_skybox_color(const TcSceneRef& scene);
void scene_set_skybox_color(const TcSceneRef& scene, const Vec3& color);

std::tuple<float, float, float> scene_get_skybox_top_color_components(const TcSceneRef& scene);
void scene_set_skybox_top_color_components(const TcSceneRef& scene, float r, float g, float b);
Vec3 scene_skybox_top_color(const TcSceneRef& scene);
void scene_set_skybox_top_color(const TcSceneRef& scene, const Vec3& color);

std::tuple<float, float, float> scene_get_skybox_bottom_color_components(const TcSceneRef& scene);
void scene_set_skybox_bottom_color_components(const TcSceneRef& scene, float r, float g, float b);
Vec3 scene_skybox_bottom_color(const TcSceneRef& scene);
void scene_set_skybox_bottom_color(const TcSceneRef& scene, const Vec3& color);

std::tuple<float, float, float> scene_get_ambient_color_components(const TcSceneRef& scene);
void scene_set_ambient_color_components(const TcSceneRef& scene, float r, float g, float b);
Vec3 scene_ambient_color(const TcSceneRef& scene);
void scene_set_ambient_color(const TcSceneRef& scene, const Vec3& color);

float scene_ambient_intensity(const TcSceneRef& scene);
void scene_set_ambient_intensity(const TcSceneRef& scene, float intensity);

tc_scene_lighting* scene_lighting(const TcSceneRef& scene);

} // namespace termin
