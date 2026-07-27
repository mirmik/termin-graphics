#include <termin_visual_scene/tc_visual_scene.h>

int main(void) {
    tc_visual_scene* scene = tc_visual_scene_create();
    if (scene == NULL) return 1;

    tc_graphic_item_handle item = tc_graphic_item_handle_invalid();
    const bool created = tc_visual_scene_create_item(
        scene, tc_graphic_item_handle_invalid(), &item);
    const bool destroyed = created && tc_visual_scene_destroy_leaf(scene, item);
    tc_visual_scene_destroy(scene);
    return destroyed ? 0 : 2;
}
