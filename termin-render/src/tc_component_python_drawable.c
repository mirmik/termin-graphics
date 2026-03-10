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

static void py_drawable_draw_geometry(tc_component* c, void* render_context, int geometry_id) {
    if (g_py_drawable_callbacks.draw_geometry && c->body) {
        g_py_drawable_callbacks.draw_geometry(c->body, render_context, geometry_id);
    }
}

static void* py_drawable_get_geometry_draws(tc_component* c, const char* phase_mark) {
    if (g_py_drawable_callbacks.get_geometry_draws && c->body) {
        return g_py_drawable_callbacks.get_geometry_draws(c->body, phase_mark);
    }
    return NULL;
}

static const tc_drawable_vtable g_python_drawable_vtable = {
    .has_phase = py_drawable_has_phase,
    .draw_geometry = py_drawable_draw_geometry,
    .get_geometry_draws = py_drawable_get_geometry_draws,
    .override_shader = NULL,
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
