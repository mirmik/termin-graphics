#ifndef TC_DRAWABLE_PROTOCOL_H
#define TC_DRAWABLE_PROTOCOL_H

#include "core/tc_component.h"
#include "core/tc_drawable_capability.h"
#include "core/tc_render_item.h"

#ifdef __cplusplus
extern "C" {
#endif

struct tc_drawable_vtable {
    bool (*has_phase)(tc_component* self, const char* phase_mark);
    bool (*collect_render_items)(tc_component* self, const tc_render_item_collect_context* context, tc_render_item_sink* sink);
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

static inline bool tc_component_collect_render_items(tc_component* c, const tc_render_item_collect_context* context, tc_render_item_sink* sink) {
    const tc_drawable_vtable* vt = tc_component_get_drawable_vtable(c);
    if (c && vt && vt->collect_render_items) {
        return vt->collect_render_items(c, context, sink);
    }
    return false;
}

#ifdef __cplusplus
}
#endif

#endif
