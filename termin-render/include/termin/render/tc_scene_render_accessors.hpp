#pragma once

#include "core/tc_scene_render_state.h"
#include <termin/geom/color.hpp>
#include <termin/render/render_export.hpp>
#include <termin/tc_scene.hpp>

namespace termin {

    RENDER_API SrgbColor scene_background_srgb_color(const TcSceneRef& scene);
    RENDER_API void scene_set_background_srgb_color(const TcSceneRef& scene, SrgbColor color);

    RENDER_API SrgbColor scene_skybox_srgb_color(const TcSceneRef& scene);
    RENDER_API void scene_set_skybox_srgb_color(const TcSceneRef& scene, SrgbColor color);

    RENDER_API SrgbColor scene_skybox_top_srgb_color(const TcSceneRef& scene);
    RENDER_API void scene_set_skybox_top_srgb_color(const TcSceneRef& scene, SrgbColor color);

    RENDER_API SrgbColor scene_skybox_horizon_srgb_color(const TcSceneRef& scene);
    RENDER_API void scene_set_skybox_horizon_srgb_color(const TcSceneRef& scene, SrgbColor color);

    RENDER_API SrgbColor scene_skybox_bottom_srgb_color(const TcSceneRef& scene);
    RENDER_API void scene_set_skybox_bottom_srgb_color(const TcSceneRef& scene, SrgbColor color);

    RENDER_API float scene_skybox_top_exponent(const TcSceneRef& scene);
    RENDER_API void scene_set_skybox_top_exponent(const TcSceneRef& scene, float exponent);

    RENDER_API float scene_skybox_bottom_exponent(const TcSceneRef& scene);
    RENDER_API void scene_set_skybox_bottom_exponent(const TcSceneRef& scene, float exponent);

    RENDER_API SrgbColor scene_ambient_srgb_color(const TcSceneRef& scene);
    RENDER_API void scene_set_ambient_srgb_color(const TcSceneRef& scene, SrgbColor color);

    RENDER_API float scene_ambient_intensity(const TcSceneRef& scene);
    RENDER_API void scene_set_ambient_intensity(const TcSceneRef& scene, float intensity);

    RENDER_API tc_scene_lighting* scene_lighting(const TcSceneRef& scene);

} // namespace termin
