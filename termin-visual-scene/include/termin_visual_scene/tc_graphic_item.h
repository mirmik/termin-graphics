#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <geom/tc_affine2.h>
#include <inspect/tc_runtime_type_registry.h>
#include <tcbase/tc_binding_types.h>

#include "termin_visual_scene/export.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct tc_visual_scene tc_visual_scene;
typedef struct tc_graphic_item tc_graphic_item;
typedef struct tc_graphic_item_draw_sink tc_graphic_item_draw_sink;

typedef struct tc_graphic_item_handle {
    uint64_t scene_id;
    uint32_t index;
    uint32_t generation;
} tc_graphic_item_handle;

static inline tc_graphic_item_handle tc_graphic_item_handle_invalid(void) {
    const tc_graphic_item_handle result = {0, UINT32_MAX, 0};
    return result;
}

static inline bool tc_graphic_item_handle_is_invalid(tc_graphic_item_handle handle) {
    return handle.scene_id == 0 || handle.index == UINT32_MAX || handle.generation == 0;
}

typedef void (*tc_graphic_item_deleter)(tc_graphic_item* item);

typedef struct tc_graphic_item_vtable {
    const char* type_name;
    bool (*local_bounds)(const tc_graphic_item* item, tc_bounds2f* out_bounds);
    bool (*hit_test)(const tc_graphic_item* item, tc_vec2f local_point, float tolerance);
    bool (*paint)(const tc_graphic_item* item, tc_graphic_item_draw_sink* sink);
    bool (*push_clip)(const tc_graphic_item* item, tc_graphic_item_draw_sink* sink, bool* out_pushed);
    bool (*clip_contains)(const tc_graphic_item* item, tc_vec2f local_point);
    void (*on_destroy)(tc_graphic_item* item, tc_visual_scene* scene);
} tc_graphic_item_vtable;

// Language implementations embed this base in their concrete object, exactly
// as widget implementations embed tc_widget. The scene owns an adopted item
// through one creator-supplied deleter. Internal topology is pointer-based;
// generation handles exist only for external references.
struct tc_graphic_item {
    const tc_graphic_item_vtable* vtable;
    tc_graphic_item_deleter deleter;
    tc_visual_scene* scene;
    tc_graphic_item_handle handle;
    tc_language native_language;
    void* body;
    const char* declared_type_name;
    tc_runtime_type_instance_link runtime_type_link;

    tc_graphic_item* parent;
    tc_graphic_item** children;
    size_t child_count;
    size_t child_capacity;

    tc_affine2f local_transform;
    bool visible;
    bool enabled;
    float opacity;
    int64_t z_order;
    uint64_t stable_order;
};

TERMIN_VISUAL_SCENE_API void tc_graphic_item_init_unowned(tc_graphic_item* item,
                                                          const tc_graphic_item_vtable* vtable,
                                                          tc_language native_language,
                                                          void* body);

TERMIN_VISUAL_SCENE_API bool tc_graphic_item_is_attached(const tc_graphic_item* item);
TERMIN_VISUAL_SCENE_API const char* tc_graphic_item_type_name(const tc_graphic_item* item);
TERMIN_VISUAL_SCENE_API void tc_graphic_item_set_declared_type_name(tc_graphic_item* item, const char* type_name);

TERMIN_VISUAL_SCENE_API bool tc_graphic_item_append_child(tc_graphic_item* parent, tc_graphic_item* child);
TERMIN_VISUAL_SCENE_API bool
tc_graphic_item_insert_child(tc_graphic_item* parent, size_t index, tc_graphic_item* child);
TERMIN_VISUAL_SCENE_API bool tc_graphic_item_remove_child(tc_graphic_item* parent, tc_graphic_item* child);
TERMIN_VISUAL_SCENE_API bool tc_graphic_item_detach(tc_graphic_item* item);
TERMIN_VISUAL_SCENE_API tc_graphic_item* tc_graphic_item_parent(tc_graphic_item* item);
TERMIN_VISUAL_SCENE_API const tc_graphic_item* tc_graphic_item_parent_const(const tc_graphic_item* item);
TERMIN_VISUAL_SCENE_API size_t tc_graphic_item_child_count(const tc_graphic_item* item);
TERMIN_VISUAL_SCENE_API tc_graphic_item* tc_graphic_item_child_at(tc_graphic_item* item, size_t index);
TERMIN_VISUAL_SCENE_API const tc_graphic_item* tc_graphic_item_child_at_const(const tc_graphic_item* item,
                                                                              size_t index);

#ifdef __cplusplus
}
#endif
