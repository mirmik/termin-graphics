#include <stdlib.h>

#include <termin_visual_scene/tc_visual_scene.h>

typedef struct test_item {
    tc_graphic_item item;
} test_item;

static void delete_item(tc_graphic_item* item) {
    free(item->body);
}

static const tc_graphic_item_vtable item_vtable = {
    .type_name = "termin.visual.test.ConsumerItem",
};

int main(void) {
    tc_visual_scene_handle scene = tc_visual_scene_create();
    if (tc_visual_scene_handle_is_invalid(scene)) return 1;

    test_item* item = (test_item*)calloc(1, sizeof(test_item));
    if (item == NULL) {
        tc_visual_scene_destroy(scene);
        return 2;
    }
    tc_graphic_item_init_unowned(
        &item->item, &item_vtable, TC_LANGUAGE_C, item);
    const tc_graphic_item_handle handle =
        tc_visual_scene_adopt_item(
            scene, &item->item, delete_item);
    const bool created =
        !tc_graphic_item_handle_is_invalid(handle);
    const bool destroyed =
        created && tc_visual_scene_destroy_item(scene, handle);
    tc_visual_scene_destroy(scene);
    return destroyed ? 0 : 3;
}
