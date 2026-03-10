#ifndef TC_COMPONENT_PYTHON_DRAWABLE_H
#define TC_COMPONENT_PYTHON_DRAWABLE_H

#include "core/tc_component.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef bool (*tc_py_drawable_has_phase_fn)(void* py_self, const char* phase_mark);
typedef void (*tc_py_drawable_draw_geometry_fn)(void* py_self, void* render_context, int geometry_id);
typedef void* (*tc_py_drawable_get_geometry_draws_fn)(void* py_self, const char* phase_mark);

typedef struct {
    tc_py_drawable_has_phase_fn has_phase;
    tc_py_drawable_draw_geometry_fn draw_geometry;
    tc_py_drawable_get_geometry_draws_fn get_geometry_draws;
} tc_python_drawable_callbacks;

TC_API void tc_component_set_python_drawable_callbacks(const tc_python_drawable_callbacks* callbacks);
TC_API void tc_component_install_python_drawable_vtable(tc_component* c);

#ifdef __cplusplus
}
#endif

#endif
