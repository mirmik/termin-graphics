#include "tc_component_python_render_lifecycle.h"

#include "core/tc_component.h"
#include <string.h>

static tc_python_render_lifecycle_callbacks g_callbacks = {0};

static void python_attach(tc_component* component, const tc_render_attachment_context* context) {
    if (component && component->body && g_callbacks.on_render_attach) {
        g_callbacks.on_render_attach(component->body, context);
    }
}

static void python_prepare(tc_component* component, const tc_render_prepare_context* context) {
    if (component && component->body && g_callbacks.prepare_render) {
        g_callbacks.prepare_render(component->body, context);
    }
}

static void python_detach(tc_component* component, const tc_render_attachment_context* context) {
    if (component && component->body && g_callbacks.on_render_detach) {
        g_callbacks.on_render_detach(component->body, context);
    }
}

static const tc_render_lifecycle_vtable python_vtable = {
    python_attach,
    python_prepare,
    python_detach,
};

void tc_component_set_python_render_lifecycle_callbacks(const tc_python_render_lifecycle_callbacks* callbacks) {
    if (callbacks) {
        g_callbacks = *callbacks;
    } else {
        memset(&g_callbacks, 0, sizeof(g_callbacks));
    }
}

void tc_component_install_python_render_lifecycle(tc_component* component) {
    if (!component)
        return;
    tc_render_lifecycle_capability_attach(component, &python_vtable, component->body);
}
