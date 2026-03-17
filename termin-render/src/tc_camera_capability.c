// tc_camera_capability.c - Camera capability implementation
#include "core/tc_camera_capability.h"
#include <stdlib.h>

static void tc_camera_capability_destroy(void* cap_ptr) {
    free(cap_ptr);
}

tc_component_cap_id tc_camera_capability_id(void) {
    static tc_component_cap_id camera_cap = TC_COMPONENT_CAPABILITY_INVALID_ID;
    if (camera_cap == TC_COMPONENT_CAPABILITY_INVALID_ID) {
        camera_cap = tc_component_capability_register_with_destructor(
            "camera", tc_camera_capability_destroy);
    }
    return camera_cap;
}

bool tc_camera_capability_attach(
    tc_component* c,
    const tc_camera_vtable* vtable,
    void* userdata
) {
    if (!c || !vtable) return false;

    tc_camera_capability* cap = (tc_camera_capability*)tc_component_get_capability(
        c, tc_camera_capability_id());
    if (!cap) {
        cap = (tc_camera_capability*)calloc(1, sizeof(tc_camera_capability));
        if (!cap) return false;
        if (!tc_component_attach_capability(c, tc_camera_capability_id(), cap)) {
            free(cap);
            return false;
        }
    }

    cap->vtable = vtable;
    cap->userdata = userdata;
    return true;
}

const tc_camera_capability* tc_camera_capability_get(const tc_component* c) {
    if (!c) return NULL;
    return (const tc_camera_capability*)tc_component_get_capability(
        c, tc_camera_capability_id());
}
