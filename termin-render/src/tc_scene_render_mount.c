// tc_scene_render_mount.c - Render mount extension implementation
#include "core/tc_scene_render_mount.h"
#include "core/tc_scene.h"
#include "core/tc_scene_extension.h"
#include "tc_value.h"
#include <stdlib.h>
#include <string.h>

static bool value_to_float(const tc_value* v, float* out) {
    if (!v || !out) return false;
    switch (v->type) {
        case TC_VALUE_INT: *out = (float)v->data.i; return true;
        case TC_VALUE_FLOAT: *out = v->data.f; return true;
        case TC_VALUE_DOUBLE: *out = (float)v->data.d; return true;
        default: return false;
    }
}

static bool value_to_int(const tc_value* v, int* out) {
    if (!v || !out) return false;
    switch (v->type) {
        case TC_VALUE_INT: *out = (int)v->data.i; return true;
        case TC_VALUE_FLOAT: *out = (int)v->data.f; return true;
        case TC_VALUE_DOUBLE: *out = (int)v->data.d; return true;
        default: return false;
    }
}

static bool value_to_uint64(const tc_value* v, uint64_t* out) {
    if (!v || !out) return false;
    switch (v->type) {
        case TC_VALUE_INT: *out = (uint64_t)v->data.i; return true;
        case TC_VALUE_FLOAT: *out = (uint64_t)v->data.f; return true;
        case TC_VALUE_DOUBLE: *out = (uint64_t)v->data.d; return true;
        default: return false;
    }
}

static void render_mount_ensure_pipeline_capacity(tc_scene_render_mount* mount, size_t needed) {
    if (!mount) return;
    if (mount->pipeline_template_capacity >= needed) return;
    size_t new_cap = (mount->pipeline_template_capacity == 0) ? 4 : mount->pipeline_template_capacity * 2;
    while (new_cap < needed) new_cap *= 2;
    tc_spt_handle* p = (tc_spt_handle*)realloc(mount->pipeline_templates, new_cap * sizeof(tc_spt_handle));
    if (!p) return;
    mount->pipeline_templates = p;
    mount->pipeline_template_capacity = new_cap;
}

static void render_mount_ensure_viewport_capacity(tc_scene_render_mount* mount, size_t needed) {
    if (!mount) return;
    if (mount->viewport_config_capacity >= needed) return;
    size_t new_cap = (mount->viewport_config_capacity == 0) ? 4 : mount->viewport_config_capacity * 2;
    while (new_cap < needed) new_cap *= 2;
    tc_viewport_config* p = (tc_viewport_config*)realloc(mount->viewport_configs, new_cap * sizeof(tc_viewport_config));
    if (!p) return;
    mount->viewport_configs = p;
    mount->viewport_config_capacity = new_cap;
}

static void* render_mount_create(tc_scene_handle scene, void* type_userdata) {
    (void)scene;
    (void)type_userdata;
    return calloc(1, sizeof(tc_scene_render_mount));
}

static void render_mount_destroy(void* ext, void* type_userdata) {
    (void)type_userdata;
    if (!ext) return;
    tc_scene_render_mount* mount = (tc_scene_render_mount*)ext;
    free(mount->pipeline_templates);
    free(mount->viewport_configs);
    free(mount);
}

static tc_value serialize_viewport_config(const tc_viewport_config* vc) {
    tc_value v = tc_value_dict_new();
    if (!vc) return v;

    if (vc->name && vc->name[0]) tc_value_dict_set(&v, "name", tc_value_string(vc->name));
    if (vc->display_name && vc->display_name[0]) tc_value_dict_set(&v, "display_name", tc_value_string(vc->display_name));
    if (vc->camera_uuid && vc->camera_uuid[0]) tc_value_dict_set(&v, "camera_uuid", tc_value_string(vc->camera_uuid));

    tc_value region = tc_value_list_new();
    tc_value_list_push(&region, tc_value_double((double)vc->region[0]));
    tc_value_list_push(&region, tc_value_double((double)vc->region[1]));
    tc_value_list_push(&region, tc_value_double((double)vc->region[2]));
    tc_value_list_push(&region, tc_value_double((double)vc->region[3]));
    tc_value_dict_set(&v, "region", region);

    tc_value_dict_set(&v, "depth", tc_value_int((int64_t)vc->depth));
    if (vc->input_mode && vc->input_mode[0]) tc_value_dict_set(&v, "input_mode", tc_value_string(vc->input_mode));
    tc_value_dict_set(&v, "block_input_in_editor", tc_value_bool(vc->block_input_in_editor));
    if (vc->pipeline_uuid && vc->pipeline_uuid[0]) tc_value_dict_set(&v, "pipeline_uuid", tc_value_string(vc->pipeline_uuid));
    if (vc->pipeline_name && vc->pipeline_name[0]) tc_value_dict_set(&v, "pipeline_name", tc_value_string(vc->pipeline_name));
    tc_value_dict_set(&v, "layer_mask", tc_value_int((int64_t)vc->layer_mask));
    tc_value_dict_set(&v, "enabled", tc_value_bool(vc->enabled));
    return v;
}

static bool deserialize_viewport_config(const tc_value* data, tc_viewport_config* out) {
    if (!data || !out) return false;
    if (data->type != TC_VALUE_DICT) return false;

    tc_viewport_config_init(out);

    tc_value* name = tc_value_dict_get((tc_value*)data, "name");
    if (name && name->type == TC_VALUE_STRING) out->name = name->data.s;

    tc_value* display_name = tc_value_dict_get((tc_value*)data, "display_name");
    if (display_name && display_name->type == TC_VALUE_STRING) out->display_name = display_name->data.s;

    tc_value* camera_uuid = tc_value_dict_get((tc_value*)data, "camera_uuid");
    if (camera_uuid && camera_uuid->type == TC_VALUE_STRING) out->camera_uuid = camera_uuid->data.s;

    tc_value* region = tc_value_dict_get((tc_value*)data, "region");
    if (region && region->type == TC_VALUE_LIST && tc_value_list_size(region) >= 4) {
        tc_value* x = tc_value_list_get(region, 0);
        tc_value* y = tc_value_list_get(region, 1);
        tc_value* w = tc_value_list_get(region, 2);
        tc_value* h = tc_value_list_get(region, 3);
        value_to_float(x, &out->region[0]);
        value_to_float(y, &out->region[1]);
        value_to_float(w, &out->region[2]);
        value_to_float(h, &out->region[3]);
    }

    tc_value* depth = tc_value_dict_get((tc_value*)data, "depth");
    if (depth) value_to_int(depth, &out->depth);

    tc_value* input_mode = tc_value_dict_get((tc_value*)data, "input_mode");
    if (input_mode && input_mode->type == TC_VALUE_STRING) out->input_mode = input_mode->data.s;

    tc_value* block_input = tc_value_dict_get((tc_value*)data, "block_input_in_editor");
    if (block_input && block_input->type == TC_VALUE_BOOL) out->block_input_in_editor = block_input->data.b;

    tc_value* pipeline_uuid = tc_value_dict_get((tc_value*)data, "pipeline_uuid");
    if (pipeline_uuid && pipeline_uuid->type == TC_VALUE_STRING) out->pipeline_uuid = pipeline_uuid->data.s;

    tc_value* pipeline_name = tc_value_dict_get((tc_value*)data, "pipeline_name");
    if (pipeline_name && pipeline_name->type == TC_VALUE_STRING) out->pipeline_name = pipeline_name->data.s;

    tc_value* layer_mask = tc_value_dict_get((tc_value*)data, "layer_mask");
    if (layer_mask) value_to_uint64(layer_mask, &out->layer_mask);

    tc_value* enabled = tc_value_dict_get((tc_value*)data, "enabled");
    if (enabled && enabled->type == TC_VALUE_BOOL) out->enabled = enabled->data.b;

    return true;
}

static bool render_mount_serialize(void* ext, tc_value* out_data, void* type_userdata) {
    (void)type_userdata;
    if (!ext || !out_data) return false;
    if (out_data->type != TC_VALUE_DICT) return false;

    tc_scene_render_mount* mount = (tc_scene_render_mount*)ext;

    tc_value pipelines = tc_value_list_new();
    for (size_t i = 0; i < mount->pipeline_template_count; i++) {
        tc_spt_handle h = mount->pipeline_templates[i];
        if (!tc_spt_is_valid(h)) continue;
        const char* uuid = tc_spt_get_uuid(h);
        if (!uuid || !uuid[0]) continue;
        tc_value p = tc_value_dict_new();
        tc_value_dict_set(&p, "uuid", tc_value_string(uuid));
        tc_value_list_push(&pipelines, p);
    }
    tc_value_dict_set(out_data, "scene_pipelines", pipelines);

    tc_value vps = tc_value_list_new();
    for (size_t i = 0; i < mount->viewport_config_count; i++) {
        tc_value_list_push(&vps, serialize_viewport_config(&mount->viewport_configs[i]));
    }
    tc_value_dict_set(out_data, "viewport_configs", vps);
    return true;
}

static bool render_mount_deserialize(void* ext, const tc_value* in_data, void* type_userdata) {
    (void)type_userdata;
    if (!ext || !in_data) return false;
    if (in_data->type != TC_VALUE_DICT) return false;

    tc_scene_render_mount* mount = (tc_scene_render_mount*)ext;
    mount->pipeline_template_count = 0;
    mount->viewport_config_count = 0;

    tc_value* pipelines = tc_value_dict_get((tc_value*)in_data, "scene_pipelines");
    if (pipelines && pipelines->type == TC_VALUE_LIST) {
        size_t n = tc_value_list_size(pipelines);
        render_mount_ensure_pipeline_capacity(mount, n);
        for (size_t i = 0; i < n; i++) {
            tc_value* item = tc_value_list_get(pipelines, i);
            if (!item || item->type != TC_VALUE_DICT) continue;
            tc_value* uuid = tc_value_dict_get(item, "uuid");
            if (!uuid || uuid->type != TC_VALUE_STRING || !uuid->data.s || !uuid->data.s[0]) continue;
            tc_spt_handle h = tc_spt_find_by_uuid(uuid->data.s);
            if (!tc_spt_is_valid(h)) continue;
            mount->pipeline_templates[mount->pipeline_template_count++] = h;
        }
    }

    tc_value* viewports = tc_value_dict_get((tc_value*)in_data, "viewport_configs");
    if (viewports && viewports->type == TC_VALUE_LIST) {
        size_t n = tc_value_list_size(viewports);
        render_mount_ensure_viewport_capacity(mount, n);
        for (size_t i = 0; i < n; i++) {
            tc_value* item = tc_value_list_get(viewports, i);
            if (!item) continue;
            tc_viewport_config cfg;
            if (!deserialize_viewport_config(item, &cfg)) continue;
            tc_viewport_config_copy(&mount->viewport_configs[mount->viewport_config_count], &cfg);
            mount->viewport_config_count++;
        }
    }

    return true;
}

void tc_scene_render_mount_extension_init(void) {
    if (tc_scene_ext_is_registered(TC_SCENE_EXT_TYPE_RENDER_MOUNT)) return;

    tc_scene_ext_vtable vtable = {
        .create = render_mount_create,
        .destroy = render_mount_destroy,
        .serialize = render_mount_serialize,
        .deserialize = render_mount_deserialize,
    };

    if (!tc_scene_ext_register(
            TC_SCENE_EXT_TYPE_RENDER_MOUNT,
            "render_mount",
            "render_mount",
            &vtable,
            NULL
        )) {
        return;
    }
}

tc_scene_render_mount* tc_scene_render_mount_get(tc_scene_handle scene) {
    return (tc_scene_render_mount*)tc_scene_ext_get(scene, TC_SCENE_EXT_TYPE_RENDER_MOUNT);
}

bool tc_scene_render_mount_ensure(tc_scene_handle scene) {
    if (tc_scene_ext_has(scene, TC_SCENE_EXT_TYPE_RENDER_MOUNT)) return true;
    return tc_scene_ext_attach(scene, TC_SCENE_EXT_TYPE_RENDER_MOUNT);
}

void tc_scene_add_viewport_config(tc_scene_handle h, const tc_viewport_config* config) {
    if (!tc_scene_alive(h) || !config) return;
    if (!tc_scene_render_mount_ensure(h)) return;
    tc_scene_render_mount* mount = tc_scene_render_mount_get(h);
    if (!mount) return;

    render_mount_ensure_viewport_capacity(mount, mount->viewport_config_count + 1);
    tc_viewport_config_copy(&mount->viewport_configs[mount->viewport_config_count], config);
    mount->viewport_config_count++;
}

void tc_scene_remove_viewport_config(tc_scene_handle h, size_t index) {
    if (!tc_scene_alive(h)) return;
    tc_scene_render_mount* mount = tc_scene_render_mount_get(h);
    if (!mount) return;
    if (index >= mount->viewport_config_count) return;

    if (index < mount->viewport_config_count - 1) {
        mount->viewport_configs[index] = mount->viewport_configs[mount->viewport_config_count - 1];
    }
    mount->viewport_config_count--;
}

void tc_scene_clear_viewport_configs(tc_scene_handle h) {
    if (!tc_scene_alive(h)) return;
    tc_scene_render_mount* mount = tc_scene_render_mount_get(h);
    if (!mount) return;
    mount->viewport_config_count = 0;
}

size_t tc_scene_viewport_config_count(tc_scene_handle h) {
    if (!tc_scene_alive(h)) return 0;
    tc_scene_render_mount* mount = tc_scene_render_mount_get(h);
    return mount ? mount->viewport_config_count : 0;
}

tc_viewport_config* tc_scene_viewport_config_at(tc_scene_handle h, size_t index) {
    if (!tc_scene_alive(h)) return NULL;
    tc_scene_render_mount* mount = tc_scene_render_mount_get(h);
    if (!mount) return NULL;
    if (index >= mount->viewport_config_count) return NULL;
    return &mount->viewport_configs[index];
}

void tc_scene_add_pipeline_template(tc_scene_handle h, tc_spt_handle spt) {
    if (!tc_scene_alive(h)) return;
    if (!tc_spt_is_valid(spt)) return;
    if (!tc_scene_render_mount_ensure(h)) return;
    tc_scene_render_mount* mount = tc_scene_render_mount_get(h);
    if (!mount) return;

    render_mount_ensure_pipeline_capacity(mount, mount->pipeline_template_count + 1);
    mount->pipeline_templates[mount->pipeline_template_count++] = spt;
}

void tc_scene_remove_pipeline_template(tc_scene_handle h, tc_spt_handle spt) {
    if (!tc_scene_alive(h)) return;
    tc_scene_render_mount* mount = tc_scene_render_mount_get(h);
    if (!mount) return;

    for (size_t i = 0; i < mount->pipeline_template_count; i++) {
        if (tc_spt_handle_eq(mount->pipeline_templates[i], spt)) {
            mount->pipeline_templates[i] = mount->pipeline_templates[--mount->pipeline_template_count];
            return;
        }
    }
}

void tc_scene_clear_pipeline_templates(tc_scene_handle h) {
    if (!tc_scene_alive(h)) return;
    tc_scene_render_mount* mount = tc_scene_render_mount_get(h);
    if (!mount) return;
    mount->pipeline_template_count = 0;
}

size_t tc_scene_pipeline_template_count(tc_scene_handle h) {
    if (!tc_scene_alive(h)) return 0;
    tc_scene_render_mount* mount = tc_scene_render_mount_get(h);
    return mount ? mount->pipeline_template_count : 0;
}

tc_spt_handle tc_scene_pipeline_template_at(tc_scene_handle h, size_t index) {
    if (!tc_scene_alive(h)) return TC_SPT_HANDLE_INVALID;
    tc_scene_render_mount* mount = tc_scene_render_mount_get(h);
    if (!mount) return TC_SPT_HANDLE_INVALID;
    if (index >= mount->pipeline_template_count) return TC_SPT_HANDLE_INVALID;
    return mount->pipeline_templates[index];
}
