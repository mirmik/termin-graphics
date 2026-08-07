#ifndef TC_RENDER_LIFECYCLE_H
#define TC_RENDER_LIFECYCLE_H

#include "core/tc_component_capability.h"
#include "core/tc_scene_pool.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

struct tc_component;
typedef struct tc_component tc_component;

typedef struct tc_render_attachment_context tc_render_attachment_context;
typedef struct tc_render_prepare_context tc_render_prepare_context;

typedef struct tc_render_lifecycle_vtable {
    void (*on_render_attach)(tc_component* component, const tc_render_attachment_context* context);
    void (*prepare_render)(tc_component* component, const tc_render_prepare_context* context);
    void (*on_render_detach)(tc_component* component, const tc_render_attachment_context* context);
} tc_render_lifecycle_vtable;

typedef struct tc_render_lifecycle_capability {
    const tc_render_lifecycle_vtable* vtable;
    void* userdata;
    bool attached;
} tc_render_lifecycle_capability;

TC_API tc_component_cap_id tc_render_lifecycle_capability_id(void);
TC_API bool tc_render_lifecycle_capability_attach(tc_component* component,
                                                  const tc_render_lifecycle_vtable* vtable,
                                                  void* userdata);
TC_API void tc_render_lifecycle_capability_detach(tc_component* component);
TC_API const tc_render_lifecycle_capability* tc_render_lifecycle_capability_get(const tc_component* component);
TC_API int tc_render_lifecycle_priority(const tc_component* component);
TC_API bool tc_render_lifecycle_set_priority(tc_component* component, int priority);

TC_API void tc_render_lifecycle_notify_component_registered(tc_component* component,
                                                            const tc_render_attachment_context* context);
TC_API void tc_render_lifecycle_notify_component_unregistering(tc_component* component,
                                                               const tc_render_attachment_context* context);
TC_API void tc_render_lifecycle_notify_scene_attach(tc_scene_handle scene, const tc_render_attachment_context* context);
TC_API void tc_render_lifecycle_prepare_scene(tc_scene_handle scene, const tc_render_prepare_context* context);
TC_API void tc_render_lifecycle_notify_scene_detach(tc_scene_handle scene, const tc_render_attachment_context* context);

#ifdef __cplusplus
}
#endif

#endif
