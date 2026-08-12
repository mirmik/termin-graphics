#include <stdlib.h>

#include <termin_visual_scene/tc_visual_scene.h>
#include <termin_visual_scene/tc_visual_scene3d.h>

typedef struct test_item {
    tc_graphic_item item;
} test_item;

static void delete_item(tc_graphic_item* item) {
    free(item->body);
}

static const tc_graphic_item_vtable item_vtable = {
    .type_name = "termin.visual.test.ConsumerItem",
};

typedef struct test_item3d {
    tc_visual_item3d item;
} test_item3d;

static void delete_item3d(tc_visual_item3d* item) {
    free(item->body);
}

static const tc_visual_item3d_vtable item3d_vtable = {
    .type_name = "termin.visual.test.ConsumerItem3D",
};

int main(void) {
    tc_visual_scene_handle scene = tc_visual_scene_create();
    if (tc_visual_scene_handle_is_invalid(scene))
        return 1;

    test_item* item = (test_item*)calloc(1, sizeof(test_item));
    if (item == NULL) {
        tc_visual_scene_destroy(scene);
        return 2;
    }
    tc_graphic_item_init_unowned(&item->item, &item_vtable, TC_LANGUAGE_C, item);
    const tc_graphic_item_handle handle = tc_visual_scene_adopt_item(scene, &item->item, delete_item);
    const bool created = !tc_graphic_item_handle_is_invalid(handle);
    const bool destroyed = created && tc_visual_scene_destroy_item(scene, handle);
    tc_visual_scene_destroy(scene);
    if (!destroyed)
        return 3;

    tc_visual_scene3d_handle scene3d = tc_visual_scene3d_create();
    if (tc_visual_scene3d_handle_is_invalid(scene3d))
        return 4;
    test_item3d* item3d = (test_item3d*)calloc(1, sizeof(test_item3d));
    if (item3d == NULL) {
        tc_visual_scene3d_destroy(scene3d);
        return 5;
    }
    tc_visual_item3d_init_unowned(&item3d->item, &item3d_vtable, TC_LANGUAGE_C, item3d);
    const tc_visual_item3d_handle handle3d = tc_visual_scene3d_adopt_item(scene3d, &item3d->item, delete_item3d);
    const bool created3d = !tc_visual_item3d_handle_is_invalid(handle3d);
    const bool destroyed3d = created3d && tc_visual_scene3d_destroy_item(scene3d, handle3d);
    tc_visual_scene3d_destroy(scene3d);
    return destroyed3d ? 0 : 6;
}
