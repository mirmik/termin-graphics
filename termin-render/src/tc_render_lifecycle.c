#include "core/tc_render_lifecycle.h"

#include "core/tc_component.h"
#include "core/tc_scene.h"
#include "core/tc_scene_render_mount.h"
#include <stdlib.h>

static void render_lifecycle_capability_destroy(void* cap_ptr) {
    free(cap_ptr);
}

tc_component_cap_id tc_render_lifecycle_capability_id(void) {
    static tc_component_cap_id capability = TC_COMPONENT_CAPABILITY_INVALID_ID;
    if (capability == TC_COMPONENT_CAPABILITY_INVALID_ID) {
        capability = tc_component_capability_register_with_destructor(
            "render_lifecycle", render_lifecycle_capability_destroy);
    }
    return capability;
}

static tc_render_lifecycle_capability* mutable_capability(tc_component* component) {
    if (!component) return NULL;
    return (tc_render_lifecycle_capability*)tc_component_get_capability(
        component, tc_render_lifecycle_capability_id());
}

static void dispatch_attach(
    tc_component* component,
    tc_render_lifecycle_capability* capability,
    const tc_render_attachment_context* context
) {
    if (!component || !capability || capability->attached || !context) return;
    capability->attached = true;
    if (capability->vtable && capability->vtable->on_render_attach) {
        capability->vtable->on_render_attach(component, context);
    }
}

static void dispatch_detach(
    tc_component* component,
    tc_render_lifecycle_capability* capability,
    const tc_render_attachment_context* context
) {
    if (!component || !capability || !capability->attached || !context) return;
    if (capability->vtable && capability->vtable->on_render_detach) {
        capability->vtable->on_render_detach(component, context);
    }
    capability->attached = false;
}

bool tc_render_lifecycle_capability_attach(
    tc_component* component,
    const tc_render_lifecycle_vtable* vtable,
    void* userdata
) {
    if (!component || !vtable) return false;

    tc_render_lifecycle_capability* capability = mutable_capability(component);
    if (!capability) {
        capability = (tc_render_lifecycle_capability*)calloc(
            1, sizeof(tc_render_lifecycle_capability));
        if (!capability) return false;
        if (!tc_component_attach_capability(
                component, tc_render_lifecycle_capability_id(), capability)) {
            free(capability);
            return false;
        }
    }

    capability->vtable = vtable;
    capability->userdata = userdata;

    if (tc_scene_handle_valid(component->lifecycle_scene)) {
        const tc_render_attachment_context* context =
            tc_scene_render_mount_attachment_context(component->lifecycle_scene);
        dispatch_attach(component, capability, context);
    }
    return true;
}

void tc_render_lifecycle_capability_detach(tc_component* component) {
    tc_render_lifecycle_capability* capability = mutable_capability(component);
    if (!component || !capability) return;

    if (tc_scene_handle_valid(component->lifecycle_scene)) {
        const tc_render_attachment_context* context =
            tc_scene_render_mount_attachment_context(component->lifecycle_scene);
        dispatch_detach(component, capability, context);
    }
    tc_component_detach_capability(component, tc_render_lifecycle_capability_id());
}

const tc_render_lifecycle_capability* tc_render_lifecycle_capability_get(
    const tc_component* component
) {
    if (!component) return NULL;
    return (const tc_render_lifecycle_capability*)tc_component_get_capability(
        component, tc_render_lifecycle_capability_id());
}

int tc_render_lifecycle_priority(const tc_component* component) {
    return tc_component_get_capability_priority(
        component, tc_render_lifecycle_capability_id());
}

bool tc_render_lifecycle_set_priority(tc_component* component, int priority) {
    return tc_component_set_capability_priority(
        component, tc_render_lifecycle_capability_id(), priority);
}

void tc_render_lifecycle_notify_component_registered(
    tc_component* component,
    const tc_render_attachment_context* context
) {
    dispatch_attach(component, mutable_capability(component), context);
}

void tc_render_lifecycle_notify_component_unregistering(
    tc_component* component,
    const tc_render_attachment_context* context
) {
    dispatch_detach(component, mutable_capability(component), context);
}

typedef struct lifecycle_dispatch_context {
    const tc_render_attachment_context* attachment;
    const tc_render_prepare_context* prepare;
} lifecycle_dispatch_context;

static bool attach_component(tc_component* component, void* user_data) {
    lifecycle_dispatch_context* context = (lifecycle_dispatch_context*)user_data;
    dispatch_attach(component, mutable_capability(component), context->attachment);
    return true;
}

static bool prepare_component(tc_component* component, void* user_data) {
    lifecycle_dispatch_context* context = (lifecycle_dispatch_context*)user_data;
    tc_render_lifecycle_capability* capability = mutable_capability(component);
    dispatch_attach(component, capability, context->attachment);
    if (capability && capability->attached && capability->vtable &&
        capability->vtable->prepare_render) {
        capability->vtable->prepare_render(component, context->prepare);
    }
    return true;
}

static bool detach_component(tc_component* component, void* user_data) {
    lifecycle_dispatch_context* context = (lifecycle_dispatch_context*)user_data;
    dispatch_detach(component, mutable_capability(component), context->attachment);
    return true;
}

static int lifecycle_filters(void) {
    return TC_SCENE_FILTER_ENABLED | TC_SCENE_FILTER_ENTITY_ENABLED;
}

void tc_render_lifecycle_notify_scene_attach(
    tc_scene_handle scene,
    const tc_render_attachment_context* context
) {
    lifecycle_dispatch_context dispatch = {.attachment = context, .prepare = NULL};
    tc_scene_foreach_with_capability(
        scene, tc_render_lifecycle_capability_id(), attach_component,
        &dispatch, lifecycle_filters());
}

void tc_render_lifecycle_prepare_scene(
    tc_scene_handle scene,
    const tc_render_prepare_context* context
) {
    const tc_render_attachment_context* attachment =
        tc_scene_render_mount_attachment_context(scene);
    lifecycle_dispatch_context dispatch = {
        .attachment = attachment,
        .prepare = context,
    };
    tc_scene_foreach_with_capability(
        scene, tc_render_lifecycle_capability_id(), prepare_component,
        &dispatch, lifecycle_filters());
}

void tc_render_lifecycle_notify_scene_detach(
    tc_scene_handle scene,
    const tc_render_attachment_context* context
) {
    lifecycle_dispatch_context dispatch = {.attachment = context, .prepare = NULL};
    tc_scene_foreach_with_capability(
        scene, tc_render_lifecycle_capability_id(), detach_component,
        &dispatch, TC_SCENE_FILTER_NONE);
}
