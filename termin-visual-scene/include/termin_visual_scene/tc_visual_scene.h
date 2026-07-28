#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <tcbase/tc_binding_types.h>

#include "termin_visual_scene/export.h"
#include "termin_visual_scene/tc_graphic_item.h"

#ifdef __cplusplus
extern "C" {
#endif

TC_DEFINE_HANDLE(tc_visual_scene_handle)

// Thread-confined pooled owner of graphic items. The pointer never crosses
// the public boundary; callers carry only the generation-checked pool handle.
TERMIN_VISUAL_SCENE_API tc_visual_scene_handle tc_visual_scene_create(void);
TERMIN_VISUAL_SCENE_API void tc_visual_scene_destroy(
    tc_visual_scene_handle scene);
TERMIN_VISUAL_SCENE_API bool tc_visual_scene_is_valid(
    tc_visual_scene_handle scene);
TERMIN_VISUAL_SCENE_API uint64_t tc_visual_scene_id(
    tc_visual_scene_handle scene);
TERMIN_VISUAL_SCENE_API size_t tc_visual_scene_item_count(
    tc_visual_scene_handle scene);

// Adoption mirrors tc_ui_document_adopt_widget: the item must be unattached
// and have no pre-existing tree. On success the scene owns it through deleter.
TERMIN_VISUAL_SCENE_API tc_graphic_item_handle tc_visual_scene_adopt_item(
    tc_visual_scene_handle scene,
    tc_graphic_item* item,
    tc_graphic_item_deleter deleter);
TERMIN_VISUAL_SCENE_API bool tc_visual_scene_replace_item(
    tc_visual_scene_handle scene,
    tc_graphic_item_handle handle,
    tc_graphic_item* replacement,
    tc_graphic_item_deleter deleter);
TERMIN_VISUAL_SCENE_API tc_graphic_item* tc_visual_scene_resolve_item(
    tc_visual_scene_handle scene,
    tc_graphic_item_handle handle);
TERMIN_VISUAL_SCENE_API const tc_graphic_item*
tc_visual_scene_resolve_item_const(
    tc_visual_scene_handle scene,
    tc_graphic_item_handle handle);

// Copies borrowed pointers in stable adoption order. Passing NULL/0 is a size
// query. Pointers remain scene-owned and must not survive mutation.
TERMIN_VISUAL_SCENE_API size_t tc_visual_scene_copy_items(
    tc_visual_scene_handle scene,
    tc_graphic_item** out_items,
    size_t capacity);

// Destroying an item destroys its complete child subtree.
TERMIN_VISUAL_SCENE_API bool tc_visual_scene_destroy_item(
    tc_visual_scene_handle scene,
    tc_graphic_item_handle handle);
TERMIN_VISUAL_SCENE_API void tc_visual_scene_clear(
    tc_visual_scene_handle scene);

#ifdef __cplusplus
}
#endif
