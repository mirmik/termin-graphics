#ifndef TC_COMPONENT_PYTHON_RENDER_LIFECYCLE_H
#define TC_COMPONENT_PYTHON_RENDER_LIFECYCLE_H

#include "core/tc_render_lifecycle.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*tc_py_render_attach_fn)(
    void* py_self,
    const tc_render_attachment_context* context
);
typedef void (*tc_py_render_prepare_fn)(
    void* py_self,
    const tc_render_prepare_context* context
);
typedef void (*tc_py_render_detach_fn)(
    void* py_self,
    const tc_render_attachment_context* context
);

typedef struct tc_python_render_lifecycle_callbacks {
    tc_py_render_attach_fn on_render_attach;
    tc_py_render_prepare_fn prepare_render;
    tc_py_render_detach_fn on_render_detach;
} tc_python_render_lifecycle_callbacks;

TC_API void tc_component_set_python_render_lifecycle_callbacks(
    const tc_python_render_lifecycle_callbacks* callbacks
);
TC_API void tc_component_install_python_render_lifecycle(tc_component* component);

#ifdef __cplusplus
}
#endif

#endif
