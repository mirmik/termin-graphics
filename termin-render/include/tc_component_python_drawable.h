#ifndef TC_COMPONENT_PYTHON_DRAWABLE_H
#define TC_COMPONENT_PYTHON_DRAWABLE_H

#include "core/tc_component.h"
#include "core/tc_render_item.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef bool (*tc_py_drawable_has_phase_fn)(void* py_self, const char* phase_mark);
typedef bool (*tc_py_drawable_collect_render_items_fn)(void* py_self, const tc_render_item_collect_context* context, tc_render_item_sink* sink);

typedef struct {
    tc_py_drawable_has_phase_fn has_phase;
    tc_py_drawable_collect_render_items_fn collect_render_items;
} tc_python_drawable_callbacks;

TC_API void tc_component_set_python_drawable_callbacks(const tc_python_drawable_callbacks* callbacks);
TC_API void tc_component_install_python_drawable_vtable(tc_component* c);

#ifdef __cplusplus
}
#endif

#endif
