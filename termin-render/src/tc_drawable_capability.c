#include "core/tc_drawable_capability.h"
#include "core/tc_component.h"
#include <stdlib.h>

static void tc_drawable_capability_destroy(void* cap_ptr) {
    free(cap_ptr);
}

tc_component_cap_id tc_drawable_capability_id(void) {
    static tc_component_cap_id drawable_cap = TC_COMPONENT_CAPABILITY_INVALID_ID;
    if (drawable_cap == TC_COMPONENT_CAPABILITY_INVALID_ID) {
        drawable_cap = tc_component_capability_register_with_destructor("drawable", tc_drawable_capability_destroy);
    }
    return drawable_cap;
}

bool tc_drawable_capability_attach(tc_component* c, const tc_drawable_vtable* vtable, void* userdata) {
    if (!c || !vtable) return false;

    tc_drawable_capability* cap = (tc_drawable_capability*)tc_component_get_capability(c, tc_drawable_capability_id());
    if (!cap) {
        cap = (tc_drawable_capability*)calloc(1, sizeof(tc_drawable_capability));
        if (!cap) return false;
        if (!tc_component_attach_capability(c, tc_drawable_capability_id(), cap)) {
            free(cap);
            return false;
        }
    }

    cap->vtable = vtable;
    cap->userdata = userdata;
    return true;
}

const tc_drawable_capability* tc_drawable_capability_get(const tc_component* c) {
    if (!c) return NULL;
    return (const tc_drawable_capability*)tc_component_get_capability(c, tc_drawable_capability_id());
}
