#include <termin/render/tc_scene_render_accessors.hpp>

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

} // namespace termin
