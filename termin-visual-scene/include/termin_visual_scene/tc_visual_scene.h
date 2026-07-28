#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "termin_visual_scene/export.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct tc_visual_scene tc_visual_scene;

typedef struct tc_graphic_item_handle {
    uint64_t scene_id;
    uint32_t index;
    uint32_t generation;
} tc_graphic_item_handle;

typedef void (*tc_graphic_item_deleter)(void* payload, void* user_data);

typedef struct tc_graphic_item_view {
    void* payload;
    tc_graphic_item_handle parent;
    tc_graphic_item_handle first_child;
    tc_graphic_item_handle next_sibling;
    tc_graphic_item_handle previous_sibling;
} tc_graphic_item_view;

static inline tc_graphic_item_handle tc_graphic_item_handle_invalid(void) {
    tc_graphic_item_handle result = {0, UINT32_MAX, 0};
    return result;
}

static inline bool tc_graphic_item_handle_is_invalid(tc_graphic_item_handle h) {
    return h.scene_id == 0 || h.index == UINT32_MAX || h.generation == 0;
}

// Scene owns adopted payloads until removal. If adoption fails, the supplied
// deleter is invoked exactly once before the call returns.
TERMIN_VISUAL_SCENE_API tc_visual_scene* tc_visual_scene_create(void);
TERMIN_VISUAL_SCENE_API void tc_visual_scene_destroy(tc_visual_scene* scene);
TERMIN_VISUAL_SCENE_API uint64_t tc_visual_scene_id(const tc_visual_scene* scene);
TERMIN_VISUAL_SCENE_API size_t tc_visual_scene_item_count(tc_visual_scene* scene);

TERMIN_VISUAL_SCENE_API bool tc_visual_scene_adopt(
    tc_visual_scene* scene,
    void* payload,
    tc_graphic_item_deleter deleter,
    void* deleter_user_data,
    tc_graphic_item_handle parent,
    tc_graphic_item_handle* out_handle);
TERMIN_VISUAL_SCENE_API bool tc_visual_scene_create_item(
    tc_visual_scene* scene,
    tc_graphic_item_handle parent,
    tc_graphic_item_handle* out_handle);

// Returns a by-value topology snapshot. Payload lifetime remains scene-owned;
// callers needing lifetime across mutations must synchronize at their layer.
TERMIN_VISUAL_SCENE_API bool tc_visual_scene_resolve(
    tc_visual_scene* scene,
    tc_graphic_item_handle handle,
    tc_graphic_item_view* out_view);

TERMIN_VISUAL_SCENE_API bool tc_visual_scene_reparent(
    tc_visual_scene* scene,
    tc_graphic_item_handle item,
    tc_graphic_item_handle new_parent);
TERMIN_VISUAL_SCENE_API bool tc_visual_scene_detach(
    tc_visual_scene* scene,
    tc_graphic_item_handle item);
TERMIN_VISUAL_SCENE_API bool tc_visual_scene_destroy_leaf(
    tc_visual_scene* scene,
    tc_graphic_item_handle item);
TERMIN_VISUAL_SCENE_API bool tc_visual_scene_destroy_subtree(
    tc_visual_scene* scene,
    tc_graphic_item_handle root);
TERMIN_VISUAL_SCENE_API void tc_visual_scene_clear(tc_visual_scene* scene);

#ifdef __cplusplus
}
#endif
