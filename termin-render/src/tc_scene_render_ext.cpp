#include <termin/render/tc_scene_render_ext.hpp>

#include <tcbase/tc_value_trent.hpp>

extern "C" {
#include "core/tc_scene_render_mount.h"
#include "core/tc_scene_skybox.h"
}

namespace termin {

std::tuple<float, float, float, float> scene_get_background_color(const TcSceneRef& scene) {
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
    float a = 1.0f;

    if (tc_scene_render_state* state = tc_scene_render_state_get(scene.handle())) {
        r = state->background_color[0];
        g = state->background_color[1];
        b = state->background_color[2];
        a = state->background_color[3];
    }

    return {r, g, b, a};
}

void scene_set_background_color(const TcSceneRef& scene, float r, float g, float b, float a) {
    if (!tc_scene_render_state_ensure(scene.handle())) return;

    tc_scene_render_state* state = tc_scene_render_state_get(scene.handle());
    if (!state) return;

    state->background_color[0] = r;
    state->background_color[1] = g;
    state->background_color[2] = b;
    state->background_color[3] = a;
}

Vec4 scene_background_color(const TcSceneRef& scene) {
    auto [r, g, b, a] = scene_get_background_color(scene);
    return Vec4(r, g, b, a);
}

void scene_set_background_color(const TcSceneRef& scene, const Vec4& color) {
    scene_set_background_color(
        scene,
        static_cast<float>(color.x),
        static_cast<float>(color.y),
        static_cast<float>(color.z),
        static_cast<float>(color.w)
    );
}

std::tuple<float, float, float> scene_get_skybox_color_components(const TcSceneRef& scene) {
    float r = 0.5f;
    float g = 0.7f;
    float b = 0.9f;

    if (tc_scene_render_state* state = tc_scene_render_state_get(scene.handle())) {
        r = state->skybox.color[0];
        g = state->skybox.color[1];
        b = state->skybox.color[2];
    }

    return {r, g, b};
}

void scene_set_skybox_color_components(const TcSceneRef& scene, float r, float g, float b) {
    if (!tc_scene_render_state_ensure(scene.handle())) return;

    tc_scene_render_state* state = tc_scene_render_state_get(scene.handle());
    if (!state) return;

    state->skybox.color[0] = r;
    state->skybox.color[1] = g;
    state->skybox.color[2] = b;
}

Vec3 scene_skybox_color(const TcSceneRef& scene) {
    auto [r, g, b] = scene_get_skybox_color_components(scene);
    return Vec3(r, g, b);
}

void scene_set_skybox_color(const TcSceneRef& scene, const Vec3& color) {
    scene_set_skybox_color_components(
        scene,
        static_cast<float>(color.x),
        static_cast<float>(color.y),
        static_cast<float>(color.z)
    );
}

std::tuple<float, float, float> scene_get_skybox_top_color_components(const TcSceneRef& scene) {
    float r = 0.4f;
    float g = 0.6f;
    float b = 0.9f;

    if (tc_scene_render_state* state = tc_scene_render_state_get(scene.handle())) {
        r = state->skybox.top_color[0];
        g = state->skybox.top_color[1];
        b = state->skybox.top_color[2];
    }

    return {r, g, b};
}

void scene_set_skybox_top_color_components(const TcSceneRef& scene, float r, float g, float b) {
    if (!tc_scene_render_state_ensure(scene.handle())) return;

    tc_scene_render_state* state = tc_scene_render_state_get(scene.handle());
    if (!state) return;

    state->skybox.top_color[0] = r;
    state->skybox.top_color[1] = g;
    state->skybox.top_color[2] = b;
}

Vec3 scene_skybox_top_color(const TcSceneRef& scene) {
    auto [r, g, b] = scene_get_skybox_top_color_components(scene);
    return Vec3(r, g, b);
}

void scene_set_skybox_top_color(const TcSceneRef& scene, const Vec3& color) {
    scene_set_skybox_top_color_components(
        scene,
        static_cast<float>(color.x),
        static_cast<float>(color.y),
        static_cast<float>(color.z)
    );
}

std::tuple<float, float, float> scene_get_skybox_bottom_color_components(const TcSceneRef& scene) {
    float r = 0.6f;
    float g = 0.5f;
    float b = 0.4f;

    if (tc_scene_render_state* state = tc_scene_render_state_get(scene.handle())) {
        r = state->skybox.bottom_color[0];
        g = state->skybox.bottom_color[1];
        b = state->skybox.bottom_color[2];
    }

    return {r, g, b};
}

void scene_set_skybox_bottom_color_components(const TcSceneRef& scene, float r, float g, float b) {
    if (!tc_scene_render_state_ensure(scene.handle())) return;

    tc_scene_render_state* state = tc_scene_render_state_get(scene.handle());
    if (!state) return;

    state->skybox.bottom_color[0] = r;
    state->skybox.bottom_color[1] = g;
    state->skybox.bottom_color[2] = b;
}

Vec3 scene_skybox_bottom_color(const TcSceneRef& scene) {
    auto [r, g, b] = scene_get_skybox_bottom_color_components(scene);
    return Vec3(r, g, b);
}

void scene_set_skybox_bottom_color(const TcSceneRef& scene, const Vec3& color) {
    scene_set_skybox_bottom_color_components(
        scene,
        static_cast<float>(color.x),
        static_cast<float>(color.y),
        static_cast<float>(color.z)
    );
}

std::tuple<float, float, float> scene_get_ambient_color_components(const TcSceneRef& scene) {
    tc_scene_lighting* lighting = scene_lighting(scene);
    if (!lighting) return {1.0f, 1.0f, 1.0f};
    return {lighting->ambient_color[0], lighting->ambient_color[1], lighting->ambient_color[2]};
}

void scene_set_ambient_color_components(const TcSceneRef& scene, float r, float g, float b) {
    if (!tc_scene_render_state_ensure(scene.handle())) return;

    tc_scene_render_state* state = tc_scene_render_state_get(scene.handle());
    tc_scene_lighting* lighting = state ? &state->lighting : nullptr;
    if (!lighting) return;

    lighting->ambient_color[0] = r;
    lighting->ambient_color[1] = g;
    lighting->ambient_color[2] = b;
}

Vec3 scene_ambient_color(const TcSceneRef& scene) {
    auto [r, g, b] = scene_get_ambient_color_components(scene);
    return Vec3(r, g, b);
}

void scene_set_ambient_color(const TcSceneRef& scene, const Vec3& color) {
    scene_set_ambient_color_components(
        scene,
        static_cast<float>(color.x),
        static_cast<float>(color.y),
        static_cast<float>(color.z)
    );
}

float scene_ambient_intensity(const TcSceneRef& scene) {
    tc_scene_lighting* lighting = scene_lighting(scene);
    return lighting ? lighting->ambient_intensity : 0.1f;
}

void scene_set_ambient_intensity(const TcSceneRef& scene, float intensity) {
    if (!tc_scene_render_state_ensure(scene.handle())) return;

    tc_scene_render_state* state = tc_scene_render_state_get(scene.handle());
    tc_scene_lighting* lighting = state ? &state->lighting : nullptr;
    if (!lighting) return;

    lighting->ambient_intensity = intensity;
}

tc_scene_lighting* scene_lighting(const TcSceneRef& scene) {
    tc_scene_render_state* state = tc_scene_render_state_get(scene.handle());
    return state ? &state->lighting : nullptr;
}

void scene_add_viewport_config(const TcSceneRef& scene, const ViewportConfig& config) {
    tc_viewport_config c = config.to_c();
    tc_scene_add_viewport_config(scene.handle(), &c);
}

void scene_remove_viewport_config(const TcSceneRef& scene, size_t index) {
    tc_scene_remove_viewport_config(scene.handle(), index);
}

void scene_clear_viewport_configs(const TcSceneRef& scene) {
    tc_scene_clear_viewport_configs(scene.handle());
}

size_t scene_viewport_config_count(const TcSceneRef& scene) {
    tc_scene_render_mount* mount = tc_scene_render_mount_get(scene.handle());
    return mount ? mount->viewport_config_count : 0;
}

ViewportConfig scene_viewport_config_at(const TcSceneRef& scene, size_t index) {
    tc_scene_render_mount* mount = tc_scene_render_mount_get(scene.handle());
    if (!mount || index >= mount->viewport_config_count) {
        return ViewportConfig();
    }
    tc_viewport_config* c = &mount->viewport_configs[index];
    return ViewportConfig::from_c(c);
}

std::vector<ViewportConfig> scene_viewport_configs(const TcSceneRef& scene) {
    std::vector<ViewportConfig> result;
    tc_scene_render_mount* mount = tc_scene_render_mount_get(scene.handle());
    size_t count = mount ? mount->viewport_config_count : 0;
    result.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        tc_viewport_config* c = &mount->viewport_configs[i];
        result.push_back(ViewportConfig::from_c(c));
    }
    return result;
}

void scene_add_render_target_config(const TcSceneRef& scene, const RenderTargetConfig& config) {
    tc_render_target_config c = config.to_c();
    tc_scene_add_render_target_config(scene.handle(), &c);
    tc_render_target_config_free(&c);
}

void scene_remove_render_target_config(const TcSceneRef& scene, size_t index) {
    tc_scene_remove_render_target_config(scene.handle(), index);
}

void scene_clear_render_target_configs(const TcSceneRef& scene) {
    tc_scene_clear_render_target_configs(scene.handle());
}

size_t scene_render_target_config_count(const TcSceneRef& scene) {
    tc_scene_render_mount* mount = tc_scene_render_mount_get(scene.handle());
    return mount ? mount->render_target_config_count : 0;
}

RenderTargetConfig scene_render_target_config_at(const TcSceneRef& scene, size_t index) {
    tc_scene_render_mount* mount = tc_scene_render_mount_get(scene.handle());
    if (!mount || index >= mount->render_target_config_count) {
        return RenderTargetConfig();
    }
    tc_render_target_config* c = &mount->render_target_configs[index];
    return RenderTargetConfig::from_c(c);
}

std::vector<RenderTargetConfig> scene_render_target_configs(const TcSceneRef& scene) {
    std::vector<RenderTargetConfig> result;
    tc_scene_render_mount* mount = tc_scene_render_mount_get(scene.handle());
    size_t count = mount ? mount->render_target_config_count : 0;
    result.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        tc_render_target_config* c = &mount->render_target_configs[i];
        result.push_back(RenderTargetConfig::from_c(c));
    }
    return result;
}

bool scene_add_render_pipeline(const TcSceneRef& scene, const TcRenderPipeline& pipeline) {
    return tc_scene_add_render_pipeline(scene.handle(), pipeline.handle);
}

void scene_clear_render_pipelines(const TcSceneRef& scene) {
    tc_scene_clear_render_pipelines(scene.handle());
}

size_t scene_render_pipeline_count(const TcSceneRef& scene) {
    tc_scene_render_mount* mount = tc_scene_render_mount_get(scene.handle());
    return mount ? mount->pipeline_count : 0;
}

TcRenderPipeline scene_render_pipeline_at(const TcSceneRef& scene, size_t index) {
    tc_scene_render_mount* mount = tc_scene_render_mount_get(scene.handle());
    if (!mount || index >= mount->pipeline_count) {
        return TcRenderPipeline();
    }
    return TcRenderPipeline(mount->pipelines[index]);
}

void scene_merge_legacy_render_extensions(const nos::trent& data, nos::trent& merged_extensions) {
    if (!merged_extensions.contains("render_mount")) {
        bool has_legacy_render_mount =
            (data.contains("viewport_configs") && data["viewport_configs"].is_list()) ||
            (data.contains("scene_pipelines") && data["scene_pipelines"].is_list());

        if (has_legacy_render_mount) {
            nos::trent render_mount;
            if (data.contains("viewport_configs") && data["viewport_configs"].is_list()) {
                render_mount["viewport_configs"] = data["viewport_configs"];
            }
            if (data.contains("scene_pipelines") && data["scene_pipelines"].is_list()) {
                render_mount["scene_pipelines"] = data["scene_pipelines"];
            }
            merged_extensions["render_mount"] = std::move(render_mount);
        }
    }

    if (!merged_extensions.contains("render_state")) {
        bool has_legacy_render_state =
            data.contains("background_color") ||
            data.contains("ambient_color") ||
            data.contains("ambient_intensity") ||
            data.contains("shadow_settings") ||
            data.contains("skybox_type") ||
            data.contains("skybox_color") ||
            data.contains("skybox_top_color") ||
            data.contains("skybox_bottom_color");

        if (has_legacy_render_state) {
            nos::trent render_state;

            if (data.contains("background_color")) {
                render_state["background_color"] = data["background_color"];
            }

            nos::trent lighting;
            bool has_lighting = false;
            if (data.contains("ambient_color")) {
                lighting["ambient_color"] = data["ambient_color"];
                has_lighting = true;
            }
            if (data.contains("ambient_intensity")) {
                lighting["ambient_intensity"] = data["ambient_intensity"];
                has_lighting = true;
            }
            if (data.contains("shadow_settings")) {
                lighting["shadow_settings"] = data["shadow_settings"];
                has_lighting = true;
            }
            if (has_lighting) {
                render_state["lighting"] = std::move(lighting);
            }

            nos::trent skybox;
            bool has_skybox = false;
            if (data.contains("skybox_type")) {
                std::string type_str = data["skybox_type"].as_string_default("gradient");
                int type_int = TC_SKYBOX_GRADIENT;
                if (type_str == "none") type_int = TC_SKYBOX_NONE;
                else if (type_str == "solid") type_int = TC_SKYBOX_SOLID;
                skybox["type"] = static_cast<int64_t>(type_int);
                has_skybox = true;
            }
            if (data.contains("skybox_color")) {
                skybox["color"] = data["skybox_color"];
                has_skybox = true;
            }
            if (data.contains("skybox_top_color")) {
                skybox["top_color"] = data["skybox_top_color"];
                has_skybox = true;
            }
            if (data.contains("skybox_bottom_color")) {
                skybox["bottom_color"] = data["skybox_bottom_color"];
                has_skybox = true;
            }
            if (has_skybox) {
                render_state["skybox"] = std::move(skybox);
            }

            merged_extensions["render_state"] = std::move(render_state);
        }
    }
}

} // namespace termin
