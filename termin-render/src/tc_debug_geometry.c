#include "core/tc_debug_geometry.h"

#include "core/tc_scene_render_mount.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <tcbase/tc_log.h>

typedef struct debug_type_entry {
    tc_debug_geometry_type_desc desc;
    size_t ref_count;
} debug_type_entry;

static debug_type_entry* g_types = NULL;
static size_t g_type_count = 0;
static size_t g_type_capacity = 0;

static char* duplicate_string(const char* value) {
    if (!value) return NULL;
    size_t size = strlen(value) + 1;
    char* result = (char*)malloc(size);
    if (result) memcpy(result, value, size);
    return result;
}

static tc_debug_geometry_type_id stable_id_hash(const char* value) {
    if (!value || !value[0]) return TC_DEBUG_GEOMETRY_TYPE_INVALID;
    uint64_t hash = UINT64_C(14695981039346656037);
    for (const unsigned char* it = (const unsigned char*)value; *it; ++it) {
        hash ^= (uint64_t)*it;
        hash *= UINT64_C(1099511628211);
    }
    return hash == TC_DEBUG_GEOMETRY_TYPE_INVALID ? UINT64_C(1) : hash;
}

static debug_type_entry* find_type(tc_debug_geometry_type_id type_id) {
    if (type_id == TC_DEBUG_GEOMETRY_TYPE_INVALID) return NULL;
    for (size_t i = 0; i < g_type_count; ++i) {
        if (g_types[i].desc.type_id == type_id) return &g_types[i];
    }
    return NULL;
}

static bool ensure_type_capacity(size_t needed) {
    if (g_type_capacity >= needed) return true;
    size_t capacity = g_type_capacity == 0 ? 8 : g_type_capacity * 2;
    while (capacity < needed) capacity *= 2;
    debug_type_entry* replacement = (debug_type_entry*)realloc(
        g_types, capacity * sizeof(debug_type_entry));
    if (!replacement) {
        tc_log_error("[debug_geometry] failed to grow type registry");
        return false;
    }
    g_types = replacement;
    g_type_capacity = capacity;
    return true;
}

tc_debug_geometry_type_id tc_debug_geometry_type_register(
    const char* stable_id,
    const char* display_name,
    const char* category,
    bool default_enabled
) {
    if (!stable_id || !stable_id[0] || !display_name || !display_name[0]) {
        tc_log_error("[debug_geometry] stable id and display name are required");
        return TC_DEBUG_GEOMETRY_TYPE_INVALID;
    }
    const char* normalized_category = category ? category : "";
    tc_debug_geometry_type_id type_id = stable_id_hash(stable_id);
    debug_type_entry* existing = find_type(type_id);
    if (existing) {
        if (strcmp(existing->desc.stable_id, stable_id) != 0) {
            tc_log_error("[debug_geometry] stable id hash collision for '%s'", stable_id);
            return TC_DEBUG_GEOMETRY_TYPE_INVALID;
        }
        if (strcmp(existing->desc.display_name, display_name) != 0 ||
            strcmp(existing->desc.category, normalized_category) != 0 ||
            existing->desc.default_enabled != default_enabled) {
            tc_log_error("[debug_geometry] conflicting registration for '%s'", stable_id);
            return TC_DEBUG_GEOMETRY_TYPE_INVALID;
        }
        existing->ref_count++;
        return type_id;
    }
    if (!ensure_type_capacity(g_type_count + 1)) {
        return TC_DEBUG_GEOMETRY_TYPE_INVALID;
    }
    char* stable_copy = duplicate_string(stable_id);
    char* display_copy = duplicate_string(display_name);
    char* category_copy = duplicate_string(normalized_category);
    if (!stable_copy || !display_copy || !category_copy) {
        free(stable_copy);
        free(display_copy);
        free(category_copy);
        tc_log_error("[debug_geometry] failed to copy type metadata");
        return TC_DEBUG_GEOMETRY_TYPE_INVALID;
    }
    g_types[g_type_count++] = (debug_type_entry){
        .desc = {
            .type_id = type_id,
            .stable_id = stable_copy,
            .display_name = display_copy,
            .category = category_copy,
            .default_enabled = default_enabled,
        },
        .ref_count = 1,
    };
    return type_id;
}

bool tc_debug_geometry_type_unregister(tc_debug_geometry_type_id type_id) {
    for (size_t i = 0; i < g_type_count; ++i) {
        debug_type_entry* entry = &g_types[i];
        if (entry->desc.type_id != type_id) continue;
        if (--entry->ref_count > 0) return true;
        free((void*)entry->desc.stable_id);
        free((void*)entry->desc.display_name);
        free((void*)entry->desc.category);
        if (i + 1 < g_type_count) g_types[i] = g_types[g_type_count - 1];
        g_type_count--;
        return true;
    }
    tc_log_error("[debug_geometry] unregister requested for unknown type");
    return false;
}

bool tc_debug_geometry_type_registered(tc_debug_geometry_type_id type_id) {
    return find_type(type_id) != NULL;
}

tc_debug_geometry_type_id tc_debug_geometry_type_find(const char* stable_id) {
    tc_debug_geometry_type_id type_id = stable_id_hash(stable_id);
    debug_type_entry* entry = find_type(type_id);
    return entry && strcmp(entry->desc.stable_id, stable_id) == 0
        ? type_id : TC_DEBUG_GEOMETRY_TYPE_INVALID;
}

size_t tc_debug_geometry_type_count(void) {
    return g_type_count;
}

bool tc_debug_geometry_type_at(size_t index, tc_debug_geometry_type_desc* out_desc) {
    if (!out_desc || index >= g_type_count) return false;
    *out_desc = g_types[index].desc;
    return true;
}

void tc_debug_geometry_registry_clear(void) {
    for (size_t i = 0; i < g_type_count; ++i) {
        free((void*)g_types[i].desc.stable_id);
        free((void*)g_types[i].desc.display_name);
        free((void*)g_types[i].desc.category);
    }
    free(g_types);
    g_types = NULL;
    g_type_count = 0;
    g_type_capacity = 0;
}

static tc_debug_geometry_setting* find_setting(
    tc_scene_render_mount* mount,
    tc_debug_geometry_type_id type_id
) {
    if (!mount) return NULL;
    for (size_t i = 0; i < mount->debug_geometry_setting_count; ++i) {
        if (mount->debug_geometry_settings[i].type_id == type_id) {
            return &mount->debug_geometry_settings[i];
        }
    }
    return NULL;
}

bool tc_scene_debug_geometry_enabled(
    tc_scene_handle scene,
    tc_debug_geometry_type_id type_id
) {
    debug_type_entry* type = find_type(type_id);
    tc_scene_render_mount* mount = tc_scene_render_mount_get(scene);
    if (!type || !mount) return false;
    tc_debug_geometry_setting* setting = find_setting(mount, type_id);
    return setting ? setting->enabled : type->desc.default_enabled;
}

bool tc_scene_debug_geometry_set_enabled(
    tc_scene_handle scene,
    tc_debug_geometry_type_id type_id,
    bool enabled
) {
    if (!find_type(type_id)) {
        tc_log_error("[debug_geometry] cannot configure an unregistered type");
        return false;
    }
    tc_scene_render_mount* mount = tc_scene_render_mount_get(scene);
    if (!mount) return false;
    tc_debug_geometry_setting* setting = find_setting(mount, type_id);
    if (setting) {
        setting->enabled = enabled;
        return true;
    }
    if (mount->debug_geometry_setting_count == mount->debug_geometry_setting_capacity) {
        size_t capacity = mount->debug_geometry_setting_capacity == 0
            ? 8 : mount->debug_geometry_setting_capacity * 2;
        tc_debug_geometry_setting* replacement = (tc_debug_geometry_setting*)realloc(
            mount->debug_geometry_settings,
            capacity * sizeof(tc_debug_geometry_setting));
        if (!replacement) {
            tc_log_error("[debug_geometry] failed to grow scene settings");
            return false;
        }
        mount->debug_geometry_settings = replacement;
        mount->debug_geometry_setting_capacity = capacity;
    }
    mount->debug_geometry_settings[mount->debug_geometry_setting_count++] =
        (tc_debug_geometry_setting){.type_id = type_id, .enabled = enabled};
    return true;
}

void tc_scene_debug_geometry_begin_collection(tc_scene_handle scene) {
    tc_scene_render_mount* mount = tc_scene_render_mount_get(scene);
    if (!mount || !mount->attachment_context) return;
    mount->debug_geometry_primitive_count = 0;
    mount->debug_geometry_collecting = true;
}

void tc_scene_debug_geometry_end_collection(tc_scene_handle scene) {
    tc_scene_render_mount* mount = tc_scene_render_mount_get(scene);
    if (mount) mount->debug_geometry_collecting = false;
}

void tc_scene_debug_geometry_clear(tc_scene_handle scene) {
    tc_scene_render_mount* mount = tc_scene_render_mount_get(scene);
    if (!mount) return;
    mount->debug_geometry_primitive_count = 0;
    mount->debug_geometry_collecting = false;
}

bool tc_debug_geometry_drawer_valid(const tc_debug_geometry_drawer* drawer) {
    if (!drawer || !find_type(drawer->type_id)) return false;
    tc_scene_render_mount* mount = tc_scene_render_mount_get(drawer->scene);
    return mount && mount->attachment_context && mount->debug_geometry_collecting &&
        tc_scene_debug_geometry_enabled(drawer->scene, drawer->type_id);
}

static bool finite_vector(const float* values, size_t count) {
    if (!values) return false;
    for (size_t i = 0; i < count; ++i) {
        if (!isfinite(values[i])) return false;
    }
    return true;
}

static tc_debug_geometry_primitive* append_primitive(
    const tc_debug_geometry_drawer* drawer
) {
    if (!tc_debug_geometry_drawer_valid(drawer)) return NULL;
    tc_scene_render_mount* mount = tc_scene_render_mount_get(drawer->scene);
    if (mount->debug_geometry_primitive_count == mount->debug_geometry_primitive_capacity) {
        size_t capacity = mount->debug_geometry_primitive_capacity == 0
            ? 64 : mount->debug_geometry_primitive_capacity * 2;
        tc_debug_geometry_primitive* replacement =
            (tc_debug_geometry_primitive*)realloc(
                mount->debug_geometry_primitives,
                capacity * sizeof(tc_debug_geometry_primitive));
        if (!replacement) {
            tc_log_error("[debug_geometry] failed to grow primitive buffer");
            return NULL;
        }
        mount->debug_geometry_primitives = replacement;
        mount->debug_geometry_primitive_capacity = capacity;
    }
    tc_debug_geometry_primitive* result =
        &mount->debug_geometry_primitives[mount->debug_geometry_primitive_count++];
    *result = (tc_debug_geometry_primitive){0};
    result->type_id = drawer->type_id;
    return result;
}

bool tc_debug_geometry_drawer_line(
    const tc_debug_geometry_drawer* drawer,
    const float start[3],
    const float end[3],
    const float color[4],
    bool depth_test
) {
    if (!finite_vector(start, 3) || !finite_vector(end, 3) ||
        !finite_vector(color, 4)) return false;
    tc_debug_geometry_primitive* primitive = append_primitive(drawer);
    if (!primitive) return false;
    primitive->kind = TC_DEBUG_GEOMETRY_LINE;
    primitive->depth_test = depth_test;
    memcpy(primitive->color, color, sizeof(primitive->color));
    memcpy(primitive->data.line.start, start, sizeof(primitive->data.line.start));
    memcpy(primitive->data.line.end, end, sizeof(primitive->data.line.end));
    return true;
}

bool tc_debug_geometry_drawer_wire_sphere(
    const tc_debug_geometry_drawer* drawer,
    const float center[3],
    float radius,
    const float color[4],
    uint16_t segments,
    bool depth_test
) {
    if (!finite_vector(center, 3) || !finite_vector(color, 4) ||
        !isfinite(radius) || radius <= 0.0f) return false;
    tc_debug_geometry_primitive* primitive = append_primitive(drawer);
    if (!primitive) return false;
    primitive->kind = TC_DEBUG_GEOMETRY_WIRE_SPHERE;
    primitive->depth_test = depth_test;
    primitive->segments = segments < 3 ? 3 : segments;
    memcpy(primitive->color, color, sizeof(primitive->color));
    memcpy(primitive->data.sphere.center, center, sizeof(primitive->data.sphere.center));
    primitive->data.sphere.radius = radius;
    return true;
}

size_t tc_scene_debug_geometry_primitive_count(tc_scene_handle scene) {
    tc_scene_render_mount* mount = tc_scene_render_mount_get(scene);
    return mount ? mount->debug_geometry_primitive_count : 0;
}

const tc_debug_geometry_primitive* tc_scene_debug_geometry_primitive_at(
    tc_scene_handle scene,
    size_t index
) {
    tc_scene_render_mount* mount = tc_scene_render_mount_get(scene);
    if (!mount || index >= mount->debug_geometry_primitive_count) return NULL;
    const tc_debug_geometry_primitive* primitive =
        &mount->debug_geometry_primitives[index];
    return find_type(primitive->type_id) ? primitive : NULL;
}
