// tc_light_capability.c - Light capability implementation
#include "core/tc_light_capability.h"
#include <stdlib.h>

static void tc_light_capability_destroy(void* cap_ptr) {
    free(cap_ptr);
}

tc_component_cap_id tc_light_capability_id(void) {
    static tc_component_cap_id light_cap = TC_COMPONENT_CAPABILITY_INVALID_ID;
    if (light_cap == TC_COMPONENT_CAPABILITY_INVALID_ID) {
        light_cap = tc_component_capability_register_with_destructor(
            "light", tc_light_capability_destroy);
    }
    return light_cap;
}

bool tc_light_capability_attach(
    tc_component* c,
    const tc_light_vtable* vtable,
    void* userdata
) {
    if (!c || !vtable) return false;

    tc_light_capability* cap = (tc_light_capability*)tc_component_get_capability(
        c, tc_light_capability_id());
    if (!cap) {
        cap = (tc_light_capability*)calloc(1, sizeof(tc_light_capability));
        if (!cap) return false;
        if (!tc_component_attach_capability(c, tc_light_capability_id(), cap)) {
            free(cap);
            return false;
        }
    }

    cap->vtable = vtable;
    cap->userdata = userdata;
    return true;
}

const tc_light_capability* tc_light_capability_get(const tc_component* c) {
    if (!c) return NULL;
    return (const tc_light_capability*)tc_component_get_capability(
        c, tc_light_capability_id());
}
