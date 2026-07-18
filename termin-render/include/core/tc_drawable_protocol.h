#ifndef TC_DRAWABLE_PROTOCOL_H
#define TC_DRAWABLE_PROTOCOL_H

#include "core/tc_component.h"
#include "core/tc_drawable_capability.h"
#include "core/tc_render_item.h"

#ifdef __cplusplus
extern "C" {
#endif

struct tc_drawable_vtable {
    tc_phase_mask (*phase_mask)(tc_component* self);
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

static inline tc_phase_mask tc_component_phase_mask(tc_component* c) {
    const tc_drawable_vtable* vt = tc_component_get_drawable_vtable(c);
    if (c && vt && vt->phase_mask) {
        return vt->phase_mask(c);
    }
    return TC_PHASE_NONE;
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
