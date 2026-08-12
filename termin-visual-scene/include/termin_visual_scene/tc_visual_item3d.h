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
typedef struct tc_visual_item_paint_context3d tc_visual_item_paint_context3d;
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

typedef struct tc_visual_bounds3d {
    tc_vec3 min;
    tc_vec3 max;
} tc_visual_bounds3d;

// The host supplies a world ray. The scene normalizes it, maps it through the
// exact inverse world affine, and deliberately leaves local_ray.direction
// unnormalized. Consequently the same positive parameter is a world-space
// distance on both rays, including under non-uniform scale.
typedef struct tc_visual_hit_test_context3d {
    tc_ray3 world_ray;
    tc_ray3 local_ray;
    tc_affine3d world_from_local;
    tc_affine3d local_from_world;
} tc_visual_hit_test_context3d;

typedef struct tc_visual_hit_candidate3d {
    double distance;
    uint64_t part;
} tc_visual_hit_candidate3d;

typedef struct tc_visual_hit_result3d {
    tc_visual_item3d_handle item;
    double distance;
    uint64_t part;
    tc_vec3 world_point;
    tc_vec3 local_point;
} tc_visual_hit_result3d;

// Caller-owned view state borrowed for one synchronous scene paint. Matrices
// are column-major. extension is an optional host-defined borrowed view whose
// contract is established by the submitted packet protocol.
typedef struct tc_visual_view3d {
    tc_mat44 view_matrix;
    tc_mat44 projection_matrix;
    tc_vec3 camera_world_position;
    uint32_t viewport_width;
    uint32_t viewport_height;
    const void* extension;
} tc_visual_view3d;

// Item-defined borrowed draw payload. protocol identifies its representation;
// a sink must consume or retain everything it needs before submit returns.
typedef struct tc_visual_draw_packet3d {
    const char* protocol;
    const void* payload;
    size_t payload_size;
} tc_visual_draw_packet3d;

typedef struct tc_visual_draw_submission3d {
    tc_visual_item3d_handle item;
    tc_affine3d world_from_local;
    bool effective_visible;
    bool effective_enabled;
    const tc_visual_view3d* view;
    tc_visual_draw_packet3d packet;
} tc_visual_draw_submission3d;

typedef bool (*tc_visual_draw_sink3d_begin_fn)(const tc_visual_view3d* view, void* user_data);
typedef bool (*tc_visual_draw_sink3d_submit_fn)(const tc_visual_draw_submission3d* submission, void* user_data);
typedef bool (*tc_visual_draw_sink3d_end_fn)(void* user_data);
typedef void (*tc_visual_draw_sink3d_abort_fn)(void* user_data);

// Transactional scene sink. A successful begin is completed by end or abort;
// when end reports failure it is followed by abort for cleanup. The sink must
// not publish a partially collected frame before end succeeds.
typedef struct tc_visual_draw_sink3d {
    tc_visual_draw_sink3d_begin_fn begin;
    tc_visual_draw_sink3d_submit_fn submit;
    tc_visual_draw_sink3d_end_fn end;
    tc_visual_draw_sink3d_abort_fn abort;
    void* user_data;
} tc_visual_draw_sink3d;

typedef struct tc_visual_item3d_vtable {
    const char* type_name;
    void (*on_destroy)(tc_visual_item3d* item, tc_visual_scene3d* scene);
    bool (*hit_test)(const tc_visual_item3d* item,
                     const tc_visual_hit_test_context3d* context,
                     tc_visual_hit_candidate3d* out_candidate);
    bool (*local_bounds)(const tc_visual_item3d* item, tc_visual_bounds3d* out_bounds);
    bool (*paint)(const tc_visual_item3d* item, tc_visual_item_paint_context3d* context);
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
TERMIN_VISUAL_SCENE_API bool
tc_visual_item3d_local_bounds_in_scene(tc_visual_scene3d_handle, tc_visual_item3d_handle, tc_visual_bounds3d*);
TERMIN_VISUAL_SCENE_API tc_visual_item3d_handle
tc_visual_item_paint_context3d_item(const tc_visual_item_paint_context3d*);
TERMIN_VISUAL_SCENE_API tc_affine3d
tc_visual_item_paint_context3d_world_from_local(const tc_visual_item_paint_context3d*);
TERMIN_VISUAL_SCENE_API bool tc_visual_item_paint_context3d_effective_visible(const tc_visual_item_paint_context3d*);
TERMIN_VISUAL_SCENE_API bool tc_visual_item_paint_context3d_effective_enabled(const tc_visual_item_paint_context3d*);
TERMIN_VISUAL_SCENE_API const tc_visual_view3d*
tc_visual_item_paint_context3d_view(const tc_visual_item_paint_context3d*);
TERMIN_VISUAL_SCENE_API bool tc_visual_item_paint_context3d_submit(tc_visual_item_paint_context3d*,
                                                                   const char* protocol,
                                                                   const void* payload,
                                                                   size_t payload_size);
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
