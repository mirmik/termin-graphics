#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "termin_visual_scene/export.h"
#include "termin_visual_scene/tc_graphic_item.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct tc_graphic_item_view {
    tc_graphic_item* item;
    tc_graphic_item_handle parent;
    tc_graphic_item_handle first_child;
    tc_graphic_item_handle next_sibling;
    tc_graphic_item_handle previous_sibling;
} tc_graphic_item_view;

// Scene owns successfully adopted items until removal. Adoption requires a
// non-null creator-supplied deleter. Once an unattached item is offered to this
// function, ownership transfers to the call and a later failure invokes that
// deleter exactly once. Passing an already attached item is a precondition
// violation and leaves its existing scene ownership unchanged.
TERMIN_VISUAL_SCENE_API tc_visual_scene* tc_visual_scene_create(void);
TERMIN_VISUAL_SCENE_API void tc_visual_scene_destroy(tc_visual_scene* scene);
TERMIN_VISUAL_SCENE_API uint64_t tc_visual_scene_id(const tc_visual_scene* scene);
TERMIN_VISUAL_SCENE_API size_t tc_visual_scene_item_count(tc_visual_scene* scene);

TERMIN_VISUAL_SCENE_API bool tc_visual_scene_adopt(
    tc_visual_scene* scene,
    tc_graphic_item* item,
    tc_graphic_item_deleter deleter,
    tc_graphic_item_handle parent,
    tc_graphic_item_handle* out_handle);
TERMIN_VISUAL_SCENE_API bool tc_visual_scene_create_item(
    tc_visual_scene* scene,
    tc_graphic_item_handle parent,
    tc_graphic_item_handle* out_handle);

// Returns a by-value topology snapshot. Item lifetime remains scene-owned;
// callers needing lifetime across mutations must synchronize at their layer.
TERMIN_VISUAL_SCENE_API bool tc_visual_scene_resolve(
    tc_visual_scene* scene,
    tc_graphic_item_handle handle,
    tc_graphic_item_view* out_view);

TERMIN_VISUAL_SCENE_API bool tc_visual_scene_get_item_state(
    tc_visual_scene* scene,
    tc_graphic_item_handle item,
    tc_graphic_item_state* out_state);
TERMIN_VISUAL_SCENE_API bool tc_visual_scene_set_item_state(
    tc_visual_scene* scene,
    tc_graphic_item_handle item,
    const tc_graphic_item_state* state);
TERMIN_VISUAL_SCENE_API bool tc_visual_scene_mark_item_dirty(
    tc_visual_scene* scene,
    tc_graphic_item_handle item,
    uint32_t dirty_flags);

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
