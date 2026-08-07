#include <assert.h>

#include "termin_visual_scene/tc_builtin_items2d.h"

int main(void) {
    tc_visual_scene_handle scene = tc_visual_scene_create();
    assert(tc_visual_scene_is_valid(scene));
    tc_graphic_item_handle group = tc_visual_group_item2d_create(scene, tc_graphic_item_handle_invalid());
    assert(tc_visual_scene_item_is_valid(scene, group));
    assert(tc_visual_scene_destroy_item(scene, group));
    tc_visual_scene_destroy(scene);
    return 0;
}
