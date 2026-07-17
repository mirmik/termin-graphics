#include "tc_component_python_drawable.h"
#include "core/tc_drawable_capability.h"
#include "core/tc_drawable_protocol.h"

static tc_python_drawable_callbacks g_py_drawable_callbacks = {0};

static bool py_drawable_has_phase(tc_component* c, const char* phase_mark) {
    if (g_py_drawable_callbacks.has_phase && c->body) {
        return g_py_drawable_callbacks.has_phase(c->body, phase_mark);
    }
    return false;
}

static bool py_drawable_collect_render_items(tc_component* c, const tc_render_item_collect_context* context, tc_render_item_sink* sink) {
    if (g_py_drawable_callbacks.collect_render_items && c->body) {
        return g_py_drawable_callbacks.collect_render_items(c->body, c, context, sink);
    }
    return false;
}

static const tc_drawable_vtable g_python_drawable_vtable = {
    .has_phase = py_drawable_has_phase,
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
