#ifndef TC_DRAWABLE_PROTOCOL_H
#define TC_DRAWABLE_PROTOCOL_H

#include "core/tc_component.h"
#include "core/tc_drawable_capability.h"

#ifdef __cplusplus
extern "C" {
#endif

struct tc_drawable_vtable {
    bool (*has_phase)(tc_component* self, const char* phase_mark);
    void (*draw_geometry)(tc_component* self, void* render_context, int geometry_id);
    void* (*get_geometry_draws)(tc_component* self, const char* phase_mark);
    tc_shader_handle (*override_shader)(tc_component* self, const char* phase_mark, int geometry_id, tc_shader_handle original_shader);
};

static inline bool tc_component_is_drawable(const tc_component* c) {
    return c && tc_drawable_capability_get(c) != NULL;
}

static inline const tc_drawable_vtable* tc_component_get_drawable_vtable(const tc_component* c) {
    if (!c) return NULL;
    const tc_drawable_capability* cap = tc_drawable_capability_get(c);
    return cap ? cap->vtable : NULL;
}

static inline void* tc_component_get_drawable_userdata(const tc_component* c) {
    if (!c) return NULL;
    const tc_drawable_capability* cap = tc_drawable_capability_get(c);
    return cap ? cap->userdata : NULL;
}

static inline bool tc_component_has_phase(tc_component* c, const char* phase_mark) {
    const tc_drawable_vtable* vt = tc_component_get_drawable_vtable(c);
    if (c && vt && vt->has_phase) {
        return vt->has_phase(c, phase_mark);
    }
    return false;
}

static inline void tc_component_draw_geometry(tc_component* c, void* render_context, int geometry_id) {
    const tc_drawable_vtable* vt = tc_component_get_drawable_vtable(c);
    if (c && vt && vt->draw_geometry) {
        vt->draw_geometry(c, render_context, geometry_id);
    }
}

static inline void* tc_component_get_geometry_draws(tc_component* c, const char* phase_mark) {
    const tc_drawable_vtable* vt = tc_component_get_drawable_vtable(c);
    if (c && vt && vt->get_geometry_draws) {
        return vt->get_geometry_draws(c, phase_mark);
    }
    return NULL;
}

static inline tc_shader_handle tc_component_override_shader(tc_component* c, const char* phase_mark, int geometry_id, tc_shader_handle original_shader) {
    const tc_drawable_vtable* vt = tc_component_get_drawable_vtable(c);
    if (c && vt && vt->override_shader) {
        return vt->override_shader(c, phase_mark, geometry_id, original_shader);
    }
    return original_shader;
}

#ifdef __cplusplus
}
#endif

#endif
