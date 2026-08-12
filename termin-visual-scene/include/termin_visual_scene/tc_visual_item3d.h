#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "termin_visual_scene/export.h"
#include <geom/tc_affine3.h>
#include <inspect/tc_runtime_type_registry.h>
#include <tcbase/tc_binding_types.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct tc_visual_scene3d tc_visual_scene3d;
typedef struct tc_visual_item3d tc_visual_item3d;
TC_DEFINE_HANDLE(tc_visual_scene3d_handle)
typedef struct tc_visual_item3d_handle {
    uint64_t scene_id;
    uint32_t index;
    uint32_t generation;
} tc_visual_item3d_handle;
static inline tc_visual_item3d_handle tc_visual_item3d_handle_invalid(void) {
    return (tc_visual_item3d_handle){0, UINT32_MAX, 0};
}
static inline bool tc_visual_item3d_handle_is_invalid(tc_visual_item3d_handle h) {
    return h.scene_id == 0 || h.index == UINT32_MAX || h.generation == 0;
}
static inline bool tc_visual_item3d_handle_eq(tc_visual_item3d_handle left, tc_visual_item3d_handle right) {
    return left.scene_id == right.scene_id && left.index == right.index && left.generation == right.generation;
}
typedef void (*tc_visual_item3d_deleter)(tc_visual_item3d* item);

typedef struct tc_visual_item3d_vtable {
    const char* type_name;
    void (*on_destroy)(tc_visual_item3d* item, tc_visual_scene3d* scene);
} tc_visual_item3d_vtable;

struct tc_visual_item3d {
    const tc_visual_item3d_vtable* vtable;
    tc_visual_item3d_deleter deleter;
    tc_visual_scene3d* scene;
    tc_visual_item3d_handle handle;
    tc_language native_language;
    void* body;
    const char* declared_type_name;
    tc_runtime_type_instance_link runtime_type_link;
    tc_visual_item3d* parent;
    tc_visual_item3d** children;
    size_t child_count;
    size_t child_capacity;
    tc_affine3d local_transform;
    bool visible;
    bool enabled;
    uint64_t stable_order;
};

TERMIN_VISUAL_SCENE_API void
tc_visual_item3d_init_unowned(tc_visual_item3d*, const tc_visual_item3d_vtable*, tc_language, void*);
TERMIN_VISUAL_SCENE_API bool tc_visual_item3d_is_attached(const tc_visual_item3d*);
TERMIN_VISUAL_SCENE_API const char* tc_visual_item3d_type_name(const tc_visual_item3d*);
TERMIN_VISUAL_SCENE_API void tc_visual_item3d_set_declared_type_name(tc_visual_item3d*, const char*);
TERMIN_VISUAL_SCENE_API bool tc_visual_item3d_append_child(tc_visual_item3d*, tc_visual_item3d*);
TERMIN_VISUAL_SCENE_API bool tc_visual_item3d_insert_child(tc_visual_item3d*, size_t, tc_visual_item3d*);
TERMIN_VISUAL_SCENE_API bool tc_visual_item3d_remove_child(tc_visual_item3d*, tc_visual_item3d*);
TERMIN_VISUAL_SCENE_API bool tc_visual_item3d_detach(tc_visual_item3d*);
TERMIN_VISUAL_SCENE_API tc_visual_item3d* tc_visual_item3d_parent(tc_visual_item3d*);
TERMIN_VISUAL_SCENE_API const tc_visual_item3d* tc_visual_item3d_parent_const(const tc_visual_item3d*);
TERMIN_VISUAL_SCENE_API size_t tc_visual_item3d_child_count(const tc_visual_item3d*);
TERMIN_VISUAL_SCENE_API tc_visual_item3d* tc_visual_item3d_child_at(tc_visual_item3d*, size_t);
TERMIN_VISUAL_SCENE_API const tc_visual_item3d* tc_visual_item3d_child_at_const(const tc_visual_item3d*, size_t);

TERMIN_VISUAL_SCENE_API bool
tc_visual_item3d_get_local_transform(tc_visual_scene3d_handle, tc_visual_item3d_handle, tc_affine3d*);
TERMIN_VISUAL_SCENE_API bool
    tc_visual_item3d_set_local_transform(tc_visual_scene3d_handle, tc_visual_item3d_handle, tc_affine3d);
TERMIN_VISUAL_SCENE_API bool tc_visual_item3d_get_visible(tc_visual_scene3d_handle, tc_visual_item3d_handle, bool*);
TERMIN_VISUAL_SCENE_API bool tc_visual_item3d_set_visible(tc_visual_scene3d_handle, tc_visual_item3d_handle, bool);
TERMIN_VISUAL_SCENE_API bool tc_visual_item3d_get_enabled(tc_visual_scene3d_handle, tc_visual_item3d_handle, bool*);
TERMIN_VISUAL_SCENE_API bool tc_visual_item3d_set_enabled(tc_visual_scene3d_handle, tc_visual_item3d_handle, bool);
TERMIN_VISUAL_SCENE_API bool tc_visual_item3d_is_valid(tc_visual_scene3d_handle, tc_visual_item3d_handle);
TERMIN_VISUAL_SCENE_API const char* tc_visual_item3d_type_name_in_scene(tc_visual_scene3d_handle,
                                                                        tc_visual_item3d_handle);
TERMIN_VISUAL_SCENE_API tc_visual_item3d_handle tc_visual_item3d_parent_in_scene(tc_visual_scene3d_handle,
                                                                                 tc_visual_item3d_handle);
TERMIN_VISUAL_SCENE_API size_t tc_visual_item3d_child_count_in_scene(tc_visual_scene3d_handle, tc_visual_item3d_handle);
TERMIN_VISUAL_SCENE_API tc_visual_item3d_handle tc_visual_item3d_child_at_in_scene(tc_visual_scene3d_handle,
                                                                                   tc_visual_item3d_handle,
                                                                                   size_t);
TERMIN_VISUAL_SCENE_API bool tc_visual_item3d_set_parent_in_scene(tc_visual_scene3d_handle,
                                                                  tc_visual_item3d_handle,
                                                                  tc_visual_item3d_handle,
                                                                  size_t);

#ifdef __cplusplus
}
#endif
