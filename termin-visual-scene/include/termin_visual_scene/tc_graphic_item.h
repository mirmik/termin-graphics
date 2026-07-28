#pragma once

#include <stdbool.h>
#include <stdint.h>

#include <geom/tc_affine2.h>
#include <inspect/tc_runtime_type_registry.h>
#include <tcbase/tc_binding_types.h>
#include <tcbase/tc_value.h>

#include "termin_visual_scene/export.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct tc_visual_scene tc_visual_scene;
typedef struct tc_graphic_item tc_graphic_item;
typedef struct tc_graphic_item_snapshot_sink tc_graphic_item_snapshot_sink;

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
    return handle.scene_id == 0 ||
           handle.index == UINT32_MAX ||
           handle.generation == 0;
}

typedef void (*tc_graphic_item_deleter)(tc_graphic_item* item);

typedef struct tc_graphic_item_vtable {
    const char* type_name;
    bool (*local_bounds)(
        const tc_graphic_item* item,
        tc_bounds2f* out_bounds);
    bool (*prepare_snapshot)(
        const tc_graphic_item* item,
        tc_graphic_item_snapshot_sink* sink);
    bool (*hit_test)(
        const tc_graphic_item* item,
        tc_vec2f local_point,
        float tolerance);
    bool (*serialize)(
        const tc_graphic_item* item,
        tc_value* out_state);
    bool (*deserialize)(
        tc_graphic_item* item,
        const tc_value* state);
    void (*on_destroy)(tc_graphic_item* item);
} tc_graphic_item_vtable;

typedef enum tc_graphic_item_dirty_flags {
    TC_GRAPHIC_ITEM_DIRTY_NONE = 0,
    TC_GRAPHIC_ITEM_DIRTY_TOPOLOGY = 1u << 0,
    TC_GRAPHIC_ITEM_DIRTY_TRANSFORM = 1u << 1,
    TC_GRAPHIC_ITEM_DIRTY_VISUAL = 1u << 2,
    TC_GRAPHIC_ITEM_DIRTY_INTERACTION = 1u << 3,
    TC_GRAPHIC_ITEM_DIRTY_ALL =
        TC_GRAPHIC_ITEM_DIRTY_TOPOLOGY |
        TC_GRAPHIC_ITEM_DIRTY_TRANSFORM |
        TC_GRAPHIC_ITEM_DIRTY_VISUAL |
        TC_GRAPHIC_ITEM_DIRTY_INTERACTION
} tc_graphic_item_dirty_flags;

typedef struct tc_graphic_item_state {
    tc_affine2f local_transform;
    bool visible;
    bool enabled;
    float opacity;
    int64_t z_order;
} tc_graphic_item_state;

// Language implementations embed this structure in their concrete item body.
// tc_visual_scene owns an adopted item through exactly one creator-supplied
// deleter. body points at the language object which embeds or otherwise owns
// this base. clip_body is optional type-owned state interpreted by the item
// snapshot/hit hooks; the scene storage never casts or releases it directly.
struct tc_graphic_item {
    const tc_graphic_item_vtable* vtable;
    tc_graphic_item_deleter deleter;
    tc_visual_scene* scene;
    tc_graphic_item_handle handle;
    tc_language native_language;
    void* body;
    tc_runtime_type_instance_link runtime_type_link;

    tc_graphic_item_handle parent;
    tc_graphic_item_handle first_child;
    tc_graphic_item_handle next_sibling;
    tc_graphic_item_handle previous_sibling;

    tc_affine2f local_transform;
    bool visible;
    bool enabled;
    float opacity;
    int64_t z_order;
    void* clip_body;

    uint64_t stable_order;
    uint64_t revision;
    uint64_t topology_revision;
    uint32_t dirty_flags;
};

TERMIN_VISUAL_SCENE_API void tc_graphic_item_init_unowned(
    tc_graphic_item* item,
    const tc_graphic_item_vtable* vtable,
    tc_language native_language,
    void* body);

TERMIN_VISUAL_SCENE_API bool tc_graphic_item_is_attached(
    const tc_graphic_item* item);

TERMIN_VISUAL_SCENE_API const char* tc_graphic_item_type_name(
    const tc_graphic_item* item);

#ifdef __cplusplus
}
#endif
