#include <assert.h>
#include <math.h>
#include <stdlib.h>

#include "termin_visual_scene/tc_visual_scene3d.h"

typedef struct test_item3d {
    tc_visual_item3d item;
    int* deletes;
    double hit_distance;
    uint64_t hit_part;
} test_item3d;

static void delete_item(tc_visual_item3d* item) {
    test_item3d* value = (test_item3d*)item->body;
    *value->deletes += 1;
    free(value);
}

static bool
hit_item(const tc_visual_item3d* item, const tc_visual_hit_test_context3d* context, tc_visual_hit_candidate3d* out) {
    const test_item3d* value = (const test_item3d*)item->body;
    assert(context->world_ray.direction.x == 1.0);
    if (value->hit_distance <= 0.0)
        return false;
    out->distance = value->hit_distance;
    out->part = value->hit_part;
    return true;
}

static const tc_visual_item3d_vtable item_vtable = {
    .type_name = "termin.visual.test.CItem3D",
    .hit_test = hit_item,
};

static test_item3d* make_item(int* deletes) {
    test_item3d* result = (test_item3d*)calloc(1, sizeof(test_item3d));
    assert(result != NULL);
    result->deletes = deletes;
    tc_visual_item3d_init_unowned(&result->item, &item_vtable, TC_LANGUAGE_C, result);
    return result;
}

int main(void) {
    int deletes = 0;
    tc_visual_scene3d_handle scene = tc_visual_scene3d_create();
    assert(tc_visual_scene3d_is_valid(scene));

    test_item3d* root = make_item(&deletes);
    test_item3d* first = make_item(&deletes);
    test_item3d* second = make_item(&deletes);
    tc_visual_item3d_handle root_handle = tc_visual_scene3d_adopt_item(scene, &root->item, delete_item);
    tc_visual_item3d_handle first_handle = tc_visual_scene3d_adopt_item(scene, &first->item, delete_item);
    tc_visual_item3d_handle second_handle = tc_visual_scene3d_adopt_item(scene, &second->item, delete_item);
    assert(!tc_visual_item3d_handle_is_invalid(root_handle));
    assert(tc_visual_item3d_set_parent_in_scene(scene, first_handle, root_handle, 0));
    assert(tc_visual_item3d_set_parent_in_scene(scene, second_handle, root_handle, 1));
    assert(tc_visual_item3d_set_parent_in_scene(scene, first_handle, root_handle, 2));
    assert(tc_visual_item3d_child_count_in_scene(scene, root_handle) == 2);
    assert(tc_visual_item3d_handle_eq(tc_visual_item3d_child_at_in_scene(scene, root_handle, 1), first_handle));

    root->hit_distance = 5.0;
    first->hit_distance = 2.0;
    first->hit_part = 42;
    second->hit_distance = 3.0;
    tc_visual_hit_result3d hit;
    assert(tc_visual_scene3d_hit_test(scene, (tc_ray3){.origin = {0.0, 0.0, 0.0}, .direction = {5.0, 0.0, 0.0}}, &hit));
    assert(tc_visual_item3d_handle_eq(hit.item, first_handle));
    assert(hit.distance == 2.0 && hit.part == 42 && hit.world_point.x == 2.0);

    tc_affine3d transform = tc_affine3d_translation(2.0, 3.0, 4.0);
    assert(tc_visual_item3d_set_local_transform(scene, first_handle, transform));
    tc_affine3d returned = tc_affine3d_identity();
    assert(tc_visual_item3d_get_local_transform(scene, first_handle, &returned));
    assert(returned.translation.x == 2.0);
    transform.translation.x = NAN;
    assert(!tc_visual_item3d_set_local_transform(scene, first_handle, transform));

    tc_visual_scene3d_handle foreign = tc_visual_scene3d_create();
    assert(!tc_visual_item3d_is_valid(foreign, first_handle));
    tc_visual_scene3d_destroy(foreign);

    assert(tc_visual_scene3d_destroy_item(scene, root_handle));
    assert(deletes == 3);
    assert(!tc_visual_item3d_is_valid(scene, first_handle));
    tc_visual_scene3d_destroy(scene);
    return 0;
}
