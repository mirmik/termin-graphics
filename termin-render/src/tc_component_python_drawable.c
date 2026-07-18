#include "tc_component_python_drawable.h"
#include "core/tc_drawable_capability.h"
#include "core/tc_drawable_protocol.h"

static tc_python_drawable_callbacks g_py_drawable_callbacks = {0};

static tc_phase_mask py_drawable_phase_mask(tc_component* c) {
    if (g_py_drawable_callbacks.phase_mask && c->body) {
        return g_py_drawable_callbacks.phase_mask(c->body);
    }
    return TC_PHASE_NONE;
}

static bool py_drawable_collect_render_items(tc_component* c, const tc_render_item_collect_context* context, tc_render_item_sink* sink) {
    if (g_py_drawable_callbacks.collect_render_items && c->body) {
        return g_py_drawable_callbacks.collect_render_items(c->body, c, context, sink);
    }
    return false;
}

static const tc_drawable_vtable g_python_drawable_vtable = {
    .phase_mask = py_drawable_phase_mask,
    .collect_render_items = py_drawable_collect_render_items,
};

void tc_component_set_python_drawable_callbacks(const tc_python_drawable_callbacks* callbacks) {
    if (callbacks) {
        g_py_drawable_callbacks = *callbacks;
    }
}

void tc_component_install_python_drawable_vtable(tc_component* c) {
    if (c) {
        tc_drawable_capability_attach(c, &g_python_drawable_vtable, NULL);
    }
}
