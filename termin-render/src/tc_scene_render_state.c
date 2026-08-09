// tc_scene_render_state.c - Render scene state extension implementation
#include "core/tc_scene_render_state.h"
#include "core/tc_scene.h"
#include "core/tc_scene_extension.h"
#include "tc_value.h"
#include <math.h>
#include <stdlib.h>
#include <tcbase/tc_log.h>
#include <tgfx/resources/tc_material.h>
#include <tgfx/resources/tc_material_registry.h>
#include <tgfx/resources/tc_mesh.h>
#include <tgfx/resources/tc_mesh_registry.h>

static void release_skybox_material(tc_scene_skybox* skybox) {
    if (!skybox || tc_material_handle_is_invalid(skybox->material))
        return;
    tc_material* material = tc_material_get(skybox->material);
    if (material) {
        tc_material_release(material);
    }
    skybox->material = tc_material_handle_invalid();
}

void tc_scene_lighting_init(tc_scene_lighting* lighting) {
    if (!lighting)
        return;
    lighting->ambient_color.r = 1.0f;
    lighting->ambient_color.g = 1.0f;
    lighting->ambient_color.b = 1.0f;
    lighting->ambient_color.a = 1.0f;
    lighting->ambient_intensity = 0.1f;
    lighting->shadow_method = TC_SHADOW_METHOD_PCF;
    lighting->shadow_softness = 1.0f;
    lighting->shadow_bias = 0.0f;
}

static bool value_to_float(const tc_value* v, float* out) {
    if (!v || !out)
        return false;
    switch (v->type) {
    case TC_VALUE_INT:
        *out = (float)v->data.i;
        return true;
    case TC_VALUE_FLOAT:
        *out = v->data.f;
        return true;
    case TC_VALUE_DOUBLE:
        *out = (float)v->data.d;
        return true;
    default:
        return false;
    }
}

static bool skybox_exponent_valid(float exponent) {
    return isfinite(exponent) && exponent > 0.0f;
}

static tc_value make_color3(float r, float g, float b) {
    tc_value list = tc_value_list_new();
    tc_value_list_push(&list, tc_value_double((double)r));
    tc_value_list_push(&list, tc_value_double((double)g));
    tc_value_list_push(&list, tc_value_double((double)b));
    return list;
}

static tc_value make_color4(float r, float g, float b, float a) {
    tc_value list = tc_value_list_new();
    tc_value_list_push(&list, tc_value_double((double)r));
    tc_value_list_push(&list, tc_value_double((double)g));
    tc_value_list_push(&list, tc_value_double((double)b));
    tc_value_list_push(&list, tc_value_double((double)a));
    return list;
}

static bool read_color3(const tc_value* v, float out_color[3]) {
    if (!v || v->type != TC_VALUE_LIST)
        return false;
    if (tc_value_list_size(v) < 3)
        return false;

    tc_value* c0 = tc_value_list_get((tc_value*)v, 0);
    tc_value* c1 = tc_value_list_get((tc_value*)v, 1);
    tc_value* c2 = tc_value_list_get((tc_value*)v, 2);
    if (!c0 || !c1 || !c2)
        return false;
    if (!value_to_float(c0, &out_color[0]))
        return false;
    if (!value_to_float(c1, &out_color[1]))
        return false;
    if (!value_to_float(c2, &out_color[2]))
        return false;
    return true;
}

static bool read_color4(const tc_value* v, float out_color[4]) {
    if (!v || v->type != TC_VALUE_LIST)
        return false;
    if (tc_value_list_size(v) < 4)
        return false;

    tc_value* c0 = tc_value_list_get((tc_value*)v, 0);
    tc_value* c1 = tc_value_list_get((tc_value*)v, 1);
    tc_value* c2 = tc_value_list_get((tc_value*)v, 2);
    tc_value* c3 = tc_value_list_get((tc_value*)v, 3);
    if (!c0 || !c1 || !c2 || !c3)
        return false;
    if (!value_to_float(c0, &out_color[0]))
        return false;
    if (!value_to_float(c1, &out_color[1]))
        return false;
    if (!value_to_float(c2, &out_color[2]))
        return false;
    if (!value_to_float(c3, &out_color[3]))
        return false;
    return true;
}

static void* render_state_create(tc_scene_handle scene, void* type_userdata) {
    (void)scene;
    (void)type_userdata;

    tc_scene_render_state* state = (tc_scene_render_state*)calloc(1, sizeof(tc_scene_render_state));
    if (!state)
        return NULL;

    tc_scene_lighting_init(&state->lighting);
    tc_scene_skybox_init(&state->skybox);
    state->background_color.r = 0.0f;
    state->background_color.g = 0.0f;
    state->background_color.b = 0.0f;
    state->background_color.a = 0.0f;
    return state;
}

static void render_state_destroy(void* ext, void* type_userdata) {
    (void)type_userdata;
    if (!ext)
        return;

    tc_scene_render_state* state = (tc_scene_render_state*)ext;
    tc_scene_skybox_free(&state->skybox);
    free(state);
}

static bool render_state_serialize(void* ext, tc_value* out_data, void* type_userdata) {
    (void)type_userdata;
    if (!ext || !out_data)
        return false;
    if (out_data->type != TC_VALUE_DICT)
        return false;

    tc_scene_render_state* state = (tc_scene_render_state*)ext;

    tc_value_dict_set(out_data,
                      "background_color",
                      make_color4(state->background_color.r,
                                  state->background_color.g,
                                  state->background_color.b,
                                  state->background_color.a));

    tc_value lighting = tc_value_dict_new();
    tc_value_dict_set(&lighting,
                      "ambient_color",
                      make_color3(state->lighting.ambient_color.r,
                                  state->lighting.ambient_color.g,
                                  state->lighting.ambient_color.b));
    tc_value_dict_set(&lighting, "ambient_intensity", tc_value_double((double)state->lighting.ambient_intensity));
    tc_value shadow = tc_value_dict_new();
    tc_value_dict_set(&shadow, "method", tc_value_int((int64_t)state->lighting.shadow_method));
    tc_value_dict_set(&shadow, "softness", tc_value_double((double)state->lighting.shadow_softness));
    tc_value_dict_set(&shadow, "bias", tc_value_double((double)state->lighting.shadow_bias));
    tc_value_dict_set(&lighting, "shadow_settings", shadow);
    tc_value_dict_set(out_data, "lighting", lighting);

    tc_value skybox = tc_value_dict_new();
    tc_value_dict_set(&skybox, "type", tc_value_int((int64_t)state->skybox.type));
    tc_value_dict_set(
        &skybox, "color", make_color3(state->skybox.color.r, state->skybox.color.g, state->skybox.color.b));
    tc_value_dict_set(&skybox,
                      "top_color",
                      make_color3(state->skybox.top_color.r, state->skybox.top_color.g, state->skybox.top_color.b));
    tc_value_dict_set(&skybox,
                      "horizon_color",
                      make_color3(state->skybox.horizon_color.r,
                                  state->skybox.horizon_color.g,
                                  state->skybox.horizon_color.b));
    tc_value_dict_set(
        &skybox,
        "bottom_color",
        make_color3(state->skybox.bottom_color.r, state->skybox.bottom_color.g, state->skybox.bottom_color.b));
    tc_value_dict_set(&skybox, "top_exponent", tc_value_double((double)state->skybox.top_exponent));
    tc_value_dict_set(&skybox, "bottom_exponent", tc_value_double((double)state->skybox.bottom_exponent));
    tc_value_dict_set(out_data, "skybox", skybox);

    return true;
}

static bool render_state_deserialize(void* ext, const tc_value* in_data, void* type_userdata) {
    (void)type_userdata;
    if (!ext || !in_data)
        return false;
    if (in_data->type != TC_VALUE_DICT)
        return false;

    tc_scene_render_state* state = (tc_scene_render_state*)ext;

    tc_value* bg = tc_value_dict_get((tc_value*)in_data, "background_color");
    if (bg) {
        float values[4];
        if (read_color4(bg, values)) {
            state->background_color = (tc_srgb_color){values[0], values[1], values[2], values[3]};
        }
    }

    tc_value* lighting = tc_value_dict_get((tc_value*)in_data, "lighting");
    if (lighting && lighting->type == TC_VALUE_DICT) {
        tc_value* ambient_color = tc_value_dict_get(lighting, "ambient_color");
        if (ambient_color) {
            float values[3];
            if (read_color3(ambient_color, values)) {
                state->lighting.ambient_color.r = values[0];
                state->lighting.ambient_color.g = values[1];
                state->lighting.ambient_color.b = values[2];
                state->lighting.ambient_color.a = 1.0f;
            }
        }

        tc_value* ambient_intensity = tc_value_dict_get(lighting, "ambient_intensity");
        if (ambient_intensity) {
            value_to_float(ambient_intensity, &state->lighting.ambient_intensity);
        }

        tc_value* shadow = tc_value_dict_get(lighting, "shadow_settings");
        if (shadow && shadow->type == TC_VALUE_DICT) {
            tc_value* method = tc_value_dict_get(shadow, "method");
            tc_value* softness = tc_value_dict_get(shadow, "softness");
            tc_value* bias = tc_value_dict_get(shadow, "bias");
            if (method && method->type == TC_VALUE_INT) {
                state->lighting.shadow_method = (int)method->data.i;
            }
            if (softness)
                value_to_float(softness, &state->lighting.shadow_softness);
            if (bias)
                value_to_float(bias, &state->lighting.shadow_bias);
        }
    }

    tc_value* skybox = tc_value_dict_get((tc_value*)in_data, "skybox");
    if (skybox && skybox->type == TC_VALUE_DICT) {
        tc_value* type = tc_value_dict_get(skybox, "type");
        if (type && type->type == TC_VALUE_INT) {
            state->skybox.type = (int)type->data.i;
        }

        tc_value* color = tc_value_dict_get(skybox, "color");
        if (color) {
            float values[3];
            if (read_color3(color, values)) {
                state->skybox.color.r = values[0]; state->skybox.color.g = values[1];
                state->skybox.color.b = values[2]; state->skybox.color.a = 1.0f;
            }
        }
        tc_value* top_color = tc_value_dict_get(skybox, "top_color");
        if (top_color) {
            float values[3];
            if (read_color3(top_color, values)) {
                state->skybox.top_color.r = values[0]; state->skybox.top_color.g = values[1];
                state->skybox.top_color.b = values[2]; state->skybox.top_color.a = 1.0f;
            }
        }
        tc_value* bottom_color = tc_value_dict_get(skybox, "bottom_color");
        if (bottom_color) {
            float values[3];
            if (read_color3(bottom_color, values)) {
                state->skybox.bottom_color.r = values[0]; state->skybox.bottom_color.g = values[1];
                state->skybox.bottom_color.b = values[2]; state->skybox.bottom_color.a = 1.0f;
            }
        }
        tc_value* horizon_color = tc_value_dict_get(skybox, "horizon_color");
        if (horizon_color) {
            float values[3];
            if (read_color3(horizon_color, values)) {
                state->skybox.horizon_color.r = values[0]; state->skybox.horizon_color.g = values[1];
                state->skybox.horizon_color.b = values[2]; state->skybox.horizon_color.a = 1.0f;
            }
        } else {
            // Legacy two-color skyboxes had no authored horizon. Migrate them
            // to the perceptual midpoint of their authored sRGB endpoints.
            state->skybox.horizon_color.r = (state->skybox.top_color.r + state->skybox.bottom_color.r) * 0.5f;
            state->skybox.horizon_color.g = (state->skybox.top_color.g + state->skybox.bottom_color.g) * 0.5f;
            state->skybox.horizon_color.b = (state->skybox.top_color.b + state->skybox.bottom_color.b) * 0.5f;
            state->skybox.horizon_color.a = 1.0f;
        }
        tc_value* top_exponent = tc_value_dict_get(skybox, "top_exponent");
        float exponent = 1.0f;
        if (top_exponent && value_to_float(top_exponent, &exponent) && skybox_exponent_valid(exponent)) {
            state->skybox.top_exponent = exponent;
        } else if (top_exponent) {
            tc_log(TC_LOG_ERROR, "render_state_deserialize: skybox.top_exponent must be finite and positive");
        }
        tc_value* bottom_exponent = tc_value_dict_get(skybox, "bottom_exponent");
        exponent = 1.0f;
        if (bottom_exponent && value_to_float(bottom_exponent, &exponent) && skybox_exponent_valid(exponent)) {
            state->skybox.bottom_exponent = exponent;
        } else if (bottom_exponent) {
            tc_log(TC_LOG_ERROR, "render_state_deserialize: skybox.bottom_exponent must be finite and positive");
        }
    }

    return true;
}

void tc_scene_render_state_extension_init(void) {
    if (tc_scene_ext_is_registered(TC_SCENE_EXT_TYPE_RENDER_STATE))
        return;

    tc_scene_ext_vtable vtable = {
        .create = render_state_create,
        .destroy = render_state_destroy,
        .serialize = render_state_serialize,
        .deserialize = render_state_deserialize,
    };

    if (!tc_scene_ext_register(TC_SCENE_EXT_TYPE_RENDER_STATE, "render_state", "render_state", &vtable, NULL)) {
        return;
    }
}

tc_scene_render_state* tc_scene_render_state_get(tc_scene_handle scene) {
    return (tc_scene_render_state*)tc_scene_ext_get(scene, TC_SCENE_EXT_TYPE_RENDER_STATE);
}

bool tc_scene_render_state_ensure(tc_scene_handle scene) {
    if (tc_scene_ext_has(scene, TC_SCENE_EXT_TYPE_RENDER_STATE))
        return true;
    return tc_scene_ext_attach(scene, TC_SCENE_EXT_TYPE_RENDER_STATE);
}

void tc_scene_set_background_srgb_color(tc_scene_handle h, tc_srgb_color color) {
    if (!tc_scene_alive(h))
        return;
    if (!tc_scene_render_state_ensure(h))
        return;
    tc_scene_render_state* state = tc_scene_render_state_get(h);
    if (!state)
        return;
    state->background_color = color;
}

void tc_scene_get_background_srgb_color(tc_scene_handle h, tc_srgb_color* out_color) {
    if (!tc_scene_alive(h))
        return;
    tc_scene_render_state* state = tc_scene_render_state_get(h);
    if (!state)
        return;
    if (out_color)
        *out_color = state->background_color;
}

tc_scene_skybox* tc_scene_get_skybox(tc_scene_handle h) {
    if (!tc_scene_alive(h))
        return NULL;
    tc_scene_render_state* state = tc_scene_render_state_get(h);
    return state ? &state->skybox : NULL;
}

void tc_scene_set_skybox_type(tc_scene_handle h, int type) {
    if (!tc_scene_alive(h))
        return;
    if (!tc_scene_render_state_ensure(h))
        return;
    tc_scene_render_state* state = tc_scene_render_state_get(h);
    if (!state)
        return;
    state->skybox.type = type;
}

int tc_scene_get_skybox_type(tc_scene_handle h) {
    if (!tc_scene_alive(h))
        return TC_SKYBOX_GRADIENT;
    tc_scene_render_state* state = tc_scene_render_state_get(h);
    return state ? state->skybox.type : TC_SKYBOX_GRADIENT;
}

void tc_scene_set_skybox_srgb_color(tc_scene_handle h, tc_srgb_color color) {
    if (!tc_scene_alive(h))
        return;
    if (!tc_scene_render_state_ensure(h))
        return;
    tc_scene_render_state* state = tc_scene_render_state_get(h);
    if (!state)
        return;
    state->skybox.color = color;
}

void tc_scene_get_skybox_srgb_color(tc_scene_handle h, tc_srgb_color* out_color) {
    if (!tc_scene_alive(h))
        return;
    tc_scene_render_state* state = tc_scene_render_state_get(h);
    if (!state)
        return;
    if (out_color)
        *out_color = state->skybox.color;
}

void tc_scene_set_skybox_top_srgb_color(tc_scene_handle h, tc_srgb_color color) {
    if (!tc_scene_alive(h))
        return;
    if (!tc_scene_render_state_ensure(h))
        return;
    tc_scene_render_state* state = tc_scene_render_state_get(h);
    if (!state)
        return;
    state->skybox.top_color = color;
}

void tc_scene_get_skybox_top_srgb_color(tc_scene_handle h, tc_srgb_color* out_color) {
    if (!tc_scene_alive(h))
        return;
    tc_scene_render_state* state = tc_scene_render_state_get(h);
    if (!state)
        return;
    if (out_color)
        *out_color = state->skybox.top_color;
}

void tc_scene_set_skybox_horizon_srgb_color(tc_scene_handle h, tc_srgb_color color) {
    if (!tc_scene_alive(h) || !tc_scene_render_state_ensure(h))
        return;
    tc_scene_render_state* state = tc_scene_render_state_get(h);
    if (state)
        state->skybox.horizon_color = color;
}

void tc_scene_get_skybox_horizon_srgb_color(tc_scene_handle h, tc_srgb_color* out_color) {
    if (!tc_scene_alive(h) || !out_color)
        return;
    tc_scene_render_state* state = tc_scene_render_state_get(h);
    if (state)
        *out_color = state->skybox.horizon_color;
}

void tc_scene_set_skybox_bottom_srgb_color(tc_scene_handle h, tc_srgb_color color) {
    if (!tc_scene_alive(h))
        return;
    if (!tc_scene_render_state_ensure(h))
        return;
    tc_scene_render_state* state = tc_scene_render_state_get(h);
    if (!state)
        return;
    state->skybox.bottom_color = color;
}

void tc_scene_get_skybox_bottom_srgb_color(tc_scene_handle h, tc_srgb_color* out_color) {
    if (!tc_scene_alive(h))
        return;
    tc_scene_render_state* state = tc_scene_render_state_get(h);
    if (!state)
        return;
    if (out_color)
        *out_color = state->skybox.bottom_color;
}

void tc_scene_set_skybox_top_exponent(tc_scene_handle h, float exponent) {
    if (!skybox_exponent_valid(exponent)) {
        tc_log(TC_LOG_ERROR, "tc_scene_set_skybox_top_exponent: exponent must be finite and positive");
        return;
    }
    if (!tc_scene_alive(h) || !tc_scene_render_state_ensure(h))
        return;
    tc_scene_render_state* state = tc_scene_render_state_get(h);
    if (state)
        state->skybox.top_exponent = exponent;
}

float tc_scene_get_skybox_top_exponent(tc_scene_handle h) {
    tc_scene_render_state* state = tc_scene_alive(h) ? tc_scene_render_state_get(h) : NULL;
    return state ? state->skybox.top_exponent : 1.0f;
}

void tc_scene_set_skybox_bottom_exponent(tc_scene_handle h, float exponent) {
    if (!skybox_exponent_valid(exponent)) {
        tc_log(TC_LOG_ERROR, "tc_scene_set_skybox_bottom_exponent: exponent must be finite and positive");
        return;
    }
    if (!tc_scene_alive(h) || !tc_scene_render_state_ensure(h))
        return;
    tc_scene_render_state* state = tc_scene_render_state_get(h);
    if (state)
        state->skybox.bottom_exponent = exponent;
}

float tc_scene_get_skybox_bottom_exponent(tc_scene_handle h) {
    tc_scene_render_state* state = tc_scene_alive(h) ? tc_scene_render_state_get(h) : NULL;
    return state ? state->skybox.bottom_exponent : 1.0f;
}

void tc_scene_set_skybox_mesh(tc_scene_handle h, tc_mesh* mesh) {
    if (!tc_scene_alive(h))
        return;
    if (!tc_scene_render_state_ensure(h))
        return;
    tc_scene_render_state* state = tc_scene_render_state_get(h);
    if (!state)
        return;
    tc_scene_skybox* skybox = &state->skybox;
    tc_mesh_handle mesh_handle = tc_mesh_handle_invalid();
    if (mesh) {
        mesh_handle = tc_mesh_find(mesh->header.uuid);
        if (tc_mesh_handle_is_invalid(mesh_handle)) {
            mesh_handle = tc_mesh_find_by_name(mesh->header.name);
        }
        if (tc_mesh_handle_is_invalid(mesh_handle)) {
            return;
        }
        if (tc_mesh_handle_eq(skybox->mesh, mesh_handle)) {
            return;
        }
    }
    if (!tc_mesh_handle_is_invalid(skybox->mesh)) {
        tc_mesh* old_mesh = tc_mesh_get(skybox->mesh);
        if (old_mesh) {
            tc_mesh_release(old_mesh);
        }
    }
    skybox->mesh = tc_mesh_handle_invalid();
    if (mesh) {
        skybox->mesh = mesh_handle;
        tc_mesh_add_ref(mesh);
    }
}

tc_mesh* tc_scene_get_skybox_mesh(tc_scene_handle h) {
    if (!tc_scene_alive(h))
        return NULL;
    tc_scene_render_state* state = tc_scene_render_state_get(h);
    if (!state)
        return NULL;
    return tc_scene_skybox_ensure_mesh(&state->skybox);
}

void tc_scene_set_skybox_material(tc_scene_handle h, tc_material* material) {
    if (!tc_scene_alive(h))
        return;
    if (!tc_scene_render_state_ensure(h))
        return;
    tc_scene_render_state* state = tc_scene_render_state_get(h);
    if (!state)
        return;
    tc_scene_skybox* skybox = &state->skybox;
    tc_material_handle material_handle = tc_material_handle_invalid();
    if (material) {
        material_handle = tc_material_find(material->header.uuid);
        if (tc_material_handle_is_invalid(material_handle)) {
            material_handle = tc_material_find_by_name(material->header.name);
        }
        if (tc_material_handle_is_invalid(material_handle)) {
            return;
        }
        if (tc_material_handle_eq(skybox->material, material_handle)) {
            return;
        }
    }
    release_skybox_material(skybox);
    skybox->material = tc_material_handle_invalid();
    if (material) {
        skybox->material = material_handle;
        tc_material_add_ref(material);
    }
}

tc_material* tc_scene_get_skybox_material(tc_scene_handle h) {
    if (!tc_scene_alive(h))
        return NULL;
    tc_scene_render_state* state = tc_scene_render_state_get(h);
    return state ? tc_material_get(state->skybox.material) : NULL;
}

tc_scene_lighting* tc_scene_get_lighting(tc_scene_handle h) {
    if (!tc_scene_alive(h))
        return NULL;
    tc_scene_render_state* state = tc_scene_render_state_get(h);
    return state ? &state->lighting : NULL;
}

void tc_scene_set_ambient_srgb_color(tc_scene_handle h, tc_srgb_color color, float intensity) {
    if (!tc_scene_alive(h))
        return;
    if (!tc_scene_render_state_ensure(h))
        return;
    tc_scene_render_state* state = tc_scene_render_state_get(h);
    if (!state)
        return;
    state->lighting.ambient_color = color;
    state->lighting.ambient_intensity = intensity;
}

void tc_scene_set_shadow_settings(tc_scene_handle h, int method, float softness, float bias) {
    if (!tc_scene_alive(h))
        return;
    if (!tc_scene_render_state_ensure(h))
        return;
    tc_scene_render_state* state = tc_scene_render_state_get(h);
    if (!state)
        return;
    state->lighting.shadow_method = method;
    state->lighting.shadow_softness = softness;
    state->lighting.shadow_bias = bias;
}
