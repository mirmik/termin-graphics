#include "termin_visual_scene/tc_visual_scene3d.h"

#include <math.h>
#include <stdatomic.h>
#include <stdlib.h>

#include <tcbase/tc_log.h>
#include <tcbase/tc_pool.h>

#include "tc_visual_scene3d_internal.h"

typedef struct tc_visual_item3d_slot {
    tc_visual_item3d* item;
} tc_visual_item3d_slot;

typedef struct tc_visual_scene3d_slot {
    tc_visual_scene3d* scene;
} tc_visual_scene3d_slot;

struct tc_visual_scene3d {
    uint64_t id;
    tc_pool items;
    uint64_t next_stable_order;
    uint64_t order_revision;
    bool clearing;
};

struct tc_visual_item_paint_context3d {
    tc_visual_item3d_handle item;
    tc_affine3d world_from_local;
    bool effective_visible;
    bool effective_enabled;
    const tc_visual_view3d* view;
    tc_visual_draw_sink3d* sink;
    bool submit_failed;
};

static atomic_uint_fast64_t g_next_scene_id = 1;
static tc_pool g_scene_pool;
static bool g_scene_pool_initialized = false;

static bool ensure_scene_pool(void) {
    if (g_scene_pool_initialized)
        return true;
    if (!tc_pool_init(&g_scene_pool, sizeof(tc_visual_scene3d_slot), 16)) {
        tc_log_error("tc_visual_scene3d: scene pool initialization failed");
        return false;
    }
    g_scene_pool_initialized = true;
    return true;
}

void tc_visual_scene3d_touch_order(tc_visual_scene3d* scene) {
    if (scene == NULL)
        return;
    ++scene->order_revision;
    if (scene->order_revision == 0)
        scene->order_revision = 1;
}

static tc_handle scene_base_handle(tc_visual_scene3d_handle handle) {
    return (tc_handle){handle.index, handle.generation};
}

tc_visual_scene3d* tc_visual_scene3d_resolve_internal(tc_visual_scene3d_handle handle) {
    if (!g_scene_pool_initialized || tc_visual_scene3d_handle_is_invalid(handle)) {
        return NULL;
    }
    tc_visual_scene3d_slot* slot = (tc_visual_scene3d_slot*)tc_pool_get(&g_scene_pool, scene_base_handle(handle));
    return slot != NULL ? slot->scene : NULL;
}
#define resolve_scene tc_visual_scene3d_resolve_internal

static tc_handle local_handle(tc_visual_item3d_handle handle) {
    const tc_handle result = {
        handle.index,
        handle.generation,
    };
    return result;
}

static tc_visual_item3d_handle public_handle(const tc_visual_scene3d* scene, tc_handle handle) {
    if (tc_handle_is_invalid(handle)) {
        return tc_visual_item3d_handle_invalid();
    }
    const tc_visual_item3d_handle result = {
        scene->id,
        handle.index,
        handle.generation,
    };
    return result;
}

static tc_visual_item3d_slot* slot_for(tc_visual_scene3d* scene, tc_handle handle) {
    return (tc_visual_item3d_slot*)tc_pool_get(&scene->items, handle);
}

static const tc_visual_item3d_slot* slot_for_const(const tc_visual_scene3d* scene, tc_handle handle) {
    return (const tc_visual_item3d_slot*)tc_pool_get((tc_pool*)&scene->items, handle);
}

static bool valid_handle(const tc_visual_scene3d* scene, tc_visual_item3d_handle handle) {
    if (scene == NULL || tc_visual_item3d_handle_is_invalid(handle) || handle.scene_id != scene->id ||
        !tc_pool_is_valid(&scene->items, local_handle(handle))) {
        return false;
    }
    const tc_visual_item3d_slot* slot = slot_for_const(scene, local_handle(handle));
    return slot != NULL && slot->item != NULL && slot->item->scene == scene &&
           slot->item->handle.scene_id == handle.scene_id && slot->item->handle.index == handle.index &&
           slot->item->handle.generation == handle.generation;
}

static bool release_slot(tc_visual_scene3d* scene, tc_handle handle) {
    if (!tc_pool_free_slot(&scene->items, handle)) {
        return false;
    }
    if (scene->items.generations[handle.index] == 0) {
        scene->items.generations[handle.index] = 1;
    }
    return true;
}

static bool link_runtime_type(tc_visual_item3d* item) {
    const char* type_name = tc_visual_item3d_type_name(item);
    return type_name == NULL || !tc_runtime_type_registry_has_type(type_name) ||
           tc_runtime_type_registry_link_instance(type_name, &item->runtime_type_link, item);
}

static void destroy_object(tc_visual_scene3d* scene, tc_visual_item3d* item) {
    while (item->child_count != 0) {
        destroy_object(scene, item->children[item->child_count - 1]);
    }
    tc_visual_item3d_detach(item);

    const tc_handle handle = local_handle(item->handle);
    tc_visual_item3d_slot* slot = slot_for(scene, handle);
    const tc_visual_item3d_deleter deleter = item->deleter;
    if (slot != NULL)
        slot->item = NULL;
    release_slot(scene, handle);

    item->scene = NULL;
    item->handle = tc_visual_item3d_handle_invalid();
    item->deleter = NULL;
    free(item->children);
    item->children = NULL;
    item->child_count = 0;
    item->child_capacity = 0;
    tc_runtime_type_registry_unlink_instance(&item->runtime_type_link);

    if (item->vtable != NULL && item->vtable->on_destroy != NULL) {
        item->vtable->on_destroy(item, scene);
    }
    deleter(item);
}

tc_visual_scene3d_handle tc_visual_scene3d_create(void) {
    if (!ensure_scene_pool()) {
        return tc_visual_scene3d_handle_invalid();
    }
    tc_visual_scene3d* scene = (tc_visual_scene3d*)calloc(1, sizeof(tc_visual_scene3d));
    if (scene == NULL) {
        tc_log_error("tc_visual_scene3d_create: allocation failed");
        return tc_visual_scene3d_handle_invalid();
    }
    scene->id = atomic_fetch_add_explicit(&g_next_scene_id, 1, memory_order_relaxed);
    if (scene->id == 0) {
        scene->id = atomic_fetch_add_explicit(&g_next_scene_id, 1, memory_order_relaxed);
    }
    const tc_pool_config config = {
        .max_capacity = 0,
        .initial_generation = 1,
        .allocate_low_indices_first = true,
        .name = "VisualItem3D",
    };
    if (!tc_pool_init_ex(&scene->items, sizeof(tc_visual_item3d_slot), 16, &config)) {
        tc_log_error("tc_visual_scene3d_create: pool initialization failed");
        free(scene);
        return tc_visual_scene3d_handle_invalid();
    }
    scene->next_stable_order = 1;
    scene->order_revision = 1;
    const tc_handle local = tc_pool_alloc(&g_scene_pool);
    if (tc_handle_is_invalid(local)) {
        tc_pool_free(&scene->items);
        free(scene);
        return tc_visual_scene3d_handle_invalid();
    }
    tc_visual_scene3d_slot* slot = (tc_visual_scene3d_slot*)tc_pool_get(&g_scene_pool, local);
    slot->scene = scene;
    return (tc_visual_scene3d_handle){local.index, local.generation};
}

static void clear_scene(tc_visual_scene3d* scene);

void tc_visual_scene3d_destroy(tc_visual_scene3d_handle handle) {
    tc_visual_scene3d* scene = resolve_scene(handle);
    if (scene == NULL)
        return;
    clear_scene(scene);
    tc_pool_free(&scene->items);
    tc_visual_scene3d_slot* slot = (tc_visual_scene3d_slot*)tc_pool_get(&g_scene_pool, scene_base_handle(handle));
    if (slot != NULL)
        slot->scene = NULL;
    tc_pool_free_slot(&g_scene_pool, scene_base_handle(handle));
    free(scene);
}

bool tc_visual_scene3d_is_valid(tc_visual_scene3d_handle handle) {
    return resolve_scene(handle) != NULL;
}

uint64_t tc_visual_scene3d_id(tc_visual_scene3d_handle handle) {
    const tc_visual_scene3d* scene = resolve_scene(handle);
    return scene != NULL ? scene->id : 0;
}

size_t tc_visual_scene3d_item_count(tc_visual_scene3d_handle handle) {
    const tc_visual_scene3d* scene = resolve_scene(handle);
    return scene != NULL ? tc_pool_count(&scene->items) : 0;
}

uint64_t tc_visual_scene3d_order_revision(tc_visual_scene3d_handle handle) {
    const tc_visual_scene3d* scene = resolve_scene(handle);
    return scene != NULL ? scene->order_revision : 0;
}

tc_visual_item3d_handle tc_visual_scene3d_adopt_item(tc_visual_scene3d_handle scene_handle,
                                                     tc_visual_item3d* item,
                                                     tc_visual_item3d_deleter deleter) {
    tc_visual_scene3d* scene = resolve_scene(scene_handle);
    if (scene == NULL || item == NULL || deleter == NULL) {
        tc_log_error("tc_visual_scene3d_adopt_item: scene, item and deleter are required");
        return tc_visual_item3d_handle_invalid();
    }
    if (scene->clearing) {
        tc_log_error("tc_visual_scene3d_adopt_item: scene is clearing");
        deleter(item);
        return tc_visual_item3d_handle_invalid();
    }
    if (tc_visual_item3d_is_attached(item) || item->deleter != NULL) {
        tc_log_error("tc_visual_scene3d_adopt_item: item is already attached");
        return tc_visual_item3d_handle_invalid();
    }
    if (item->vtable == NULL || item->body == NULL || item->parent != NULL || item->child_count != 0 ||
        item->children != NULL) {
        tc_log_error("tc_visual_scene3d_adopt_item: item is not an initialized tree root");
        deleter(item);
        return tc_visual_item3d_handle_invalid();
    }

    const tc_handle local = tc_pool_alloc(&scene->items);
    if (tc_handle_is_invalid(local)) {
        tc_log_error("tc_visual_scene3d_adopt_item: pool allocation failed");
        deleter(item);
        return tc_visual_item3d_handle_invalid();
    }
    tc_visual_item3d_slot* slot = slot_for(scene, local);
    slot->item = item;
    item->deleter = deleter;
    item->scene = scene;
    item->handle = public_handle(scene, local);
    item->stable_order = scene->next_stable_order++;

    if (!link_runtime_type(item)) {
        tc_log_error("tc_visual_scene3d_adopt_item: runtime type link failed");
        slot->item = NULL;
        item->deleter = NULL;
        item->scene = NULL;
        item->handle = tc_visual_item3d_handle_invalid();
        release_slot(scene, local);
        deleter(item);
        return tc_visual_item3d_handle_invalid();
    }
    tc_visual_scene3d_touch_order(scene);
    return item->handle;
}

bool tc_visual_scene3d_replace_item(tc_visual_scene3d_handle scene_handle,
                                    tc_visual_item3d_handle handle,
                                    tc_visual_item3d* replacement,
                                    tc_visual_item3d_deleter deleter) {
    tc_visual_scene3d* scene = resolve_scene(scene_handle);
    if (!valid_handle(scene, handle) || replacement == NULL || deleter == NULL) {
        if (replacement != NULL && deleter != NULL && !tc_visual_item3d_is_attached(replacement)) {
            deleter(replacement);
        }
        return false;
    }
    if (tc_visual_item3d_is_attached(replacement) || replacement->deleter != NULL) {
        tc_log_error("tc_visual_scene3d_replace_item: replacement is already attached");
        return false;
    }
    if (replacement->parent != NULL || replacement->child_count != 0 || replacement->children != NULL ||
        replacement->vtable == NULL || replacement->body == NULL) {
        tc_log_error("tc_visual_scene3d_replace_item: replacement is not an unattached root");
        deleter(replacement);
        return false;
    }

    tc_visual_item3d_slot* slot = slot_for(scene, local_handle(handle));
    tc_visual_item3d* previous = slot->item;
    if (!link_runtime_type(replacement)) {
        deleter(replacement);
        return false;
    }

    replacement->deleter = deleter;
    replacement->scene = scene;
    replacement->handle = previous->handle;
    replacement->parent = previous->parent;
    replacement->children = previous->children;
    replacement->child_count = previous->child_count;
    replacement->child_capacity = previous->child_capacity;
    replacement->local_transform = previous->local_transform;
    replacement->visible = previous->visible;
    replacement->enabled = previous->enabled;
    replacement->stable_order = previous->stable_order;

    if (replacement->parent != NULL) {
        for (size_t index = 0; index < replacement->parent->child_count; ++index) {
            if (replacement->parent->children[index] == previous) {
                replacement->parent->children[index] = replacement;
                break;
            }
        }
    }
    for (size_t index = 0; index < replacement->child_count; ++index) {
        replacement->children[index]->parent = replacement;
    }
    slot->item = replacement;

    const tc_visual_item3d_deleter previous_deleter = previous->deleter;
    previous->scene = NULL;
    previous->handle = tc_visual_item3d_handle_invalid();
    previous->deleter = NULL;
    previous->parent = NULL;
    previous->children = NULL;
    previous->child_count = 0;
    previous->child_capacity = 0;
    tc_runtime_type_registry_unlink_instance(&previous->runtime_type_link);
    if (previous->vtable != NULL && previous->vtable->on_destroy != NULL) {
        previous->vtable->on_destroy(previous, scene);
    }
    previous_deleter(previous);
    tc_visual_scene3d_touch_order(scene);
    return true;
}

tc_visual_item3d* tc_visual_scene3d_resolve_item(tc_visual_scene3d_handle scene_handle,
                                                 tc_visual_item3d_handle handle) {
    tc_visual_scene3d* scene = resolve_scene(scene_handle);
    if (!valid_handle(scene, handle))
        return NULL;
    return slot_for(scene, local_handle(handle))->item;
}

const tc_visual_item3d* tc_visual_scene3d_resolve_item_const(tc_visual_scene3d_handle scene_handle,
                                                             tc_visual_item3d_handle handle) {
    const tc_visual_scene3d* scene = resolve_scene(scene_handle);
    if (!valid_handle(scene, handle))
        return NULL;
    return slot_for_const(scene, local_handle(handle))->item;
}

size_t
tc_visual_scene3d_copy_items(tc_visual_scene3d_handle scene_handle, tc_visual_item3d** out_items, size_t capacity) {
    const tc_visual_scene3d* scene = resolve_scene(scene_handle);
    if (scene == NULL || (out_items == NULL && capacity != 0)) {
        return 0;
    }
    const size_t count = tc_pool_count(&scene->items);
    size_t written = 0;
    uint64_t previous_order = 0;
    while (written < capacity && written < count) {
        const tc_visual_item3d* next = NULL;
        for (uint32_t index = 0; index < scene->items.capacity; ++index) {
            if (scene->items.states[index] != TC_SLOT_OCCUPIED)
                continue;
            const tc_handle local = {
                index,
                scene->items.generations[index],
            };
            const tc_visual_item3d_slot* slot = slot_for_const(scene, local);
            const tc_visual_item3d* candidate = slot != NULL ? slot->item : NULL;
            if (candidate != NULL && candidate->stable_order > previous_order &&
                (next == NULL || candidate->stable_order < next->stable_order)) {
                next = candidate;
            }
        }
        if (next == NULL)
            break;
        out_items[written++] = (tc_visual_item3d*)next;
        previous_order = next->stable_order;
    }
    return count;
}

size_t tc_visual_scene3d_copy_item_handles(tc_visual_scene3d_handle scene_handle,
                                           tc_visual_item3d_handle* out_handles,
                                           size_t capacity) {
    const tc_visual_scene3d* scene = resolve_scene(scene_handle);
    if (scene == NULL || (out_handles == NULL && capacity != 0)) {
        return 0;
    }
    const size_t count = tc_pool_count(&scene->items);
    size_t written = 0;
    uint64_t previous_order = 0;
    while (written < capacity && written < count) {
        const tc_visual_item3d* next = NULL;
        for (uint32_t index = 0; index < scene->items.capacity; ++index) {
            if (scene->items.states[index] != TC_SLOT_OCCUPIED)
                continue;
            const tc_handle local = {
                index,
                scene->items.generations[index],
            };
            const tc_visual_item3d_slot* slot = slot_for_const(scene, local);
            const tc_visual_item3d* candidate = slot != NULL ? slot->item : NULL;
            if (candidate != NULL && candidate->stable_order > previous_order &&
                (next == NULL || candidate->stable_order < next->stable_order)) {
                next = candidate;
            }
        }
        if (next == NULL)
            break;
        out_handles[written++] = next->handle;
        previous_order = next->stable_order;
    }
    return count;
}

bool tc_visual_scene3d_destroy_item(tc_visual_scene3d_handle scene_handle, tc_visual_item3d_handle handle) {
    tc_visual_scene3d* scene = resolve_scene(scene_handle);
    tc_visual_item3d* item = valid_handle(scene, handle) ? slot_for(scene, local_handle(handle))->item : NULL;
    if (item == NULL)
        return false;
    tc_visual_scene3d_touch_order(scene);
    destroy_object(scene, item);
    return true;
}

static void clear_scene(tc_visual_scene3d* scene) {
    if (scene == NULL || scene->clearing)
        return;
    scene->clearing = true;
    while (tc_pool_count(&scene->items) != 0) {
        tc_visual_item3d* item = NULL;
        for (size_t index = 0; index < scene->items.capacity; ++index) {
            tc_handle local = {(uint32_t)index, scene->items.generations[index]};
            const tc_visual_item3d_slot* slot = slot_for_const(scene, local);
            if (slot != NULL && slot->item != NULL) {
                item = slot->item;
                break;
            }
        }
        if (item == NULL)
            break;
        destroy_object(scene, item);
    }
    scene->clearing = false;
}

void tc_visual_scene3d_clear(tc_visual_scene3d_handle handle) {
    tc_visual_scene3d* scene = resolve_scene(handle);
    if (scene != NULL)
        clear_scene(scene);
}

static bool item_effectively_visible(const tc_visual_item3d* item) {
    for (const tc_visual_item3d* cursor = item; cursor != NULL; cursor = cursor->parent) {
        if (!cursor->visible)
            return false;
    }
    return true;
}

static bool item_effectively_enabled(const tc_visual_item3d* item) {
    for (const tc_visual_item3d* cursor = item; cursor != NULL; cursor = cursor->parent) {
        if (!cursor->enabled)
            return false;
    }
    return true;
}

static bool item_effectively_hittable(const tc_visual_item3d* item) {
    return item_effectively_visible(item) && item_effectively_enabled(item);
}

static tc_affine3d item_world_transform(const tc_visual_item3d* item) {
    if (item->parent == NULL)
        return item->local_transform;
    return tc_affine3d_mul(item_world_transform(item->parent), item->local_transform);
}

static tc_vec3 ray_point(tc_ray3 ray, double distance) {
    return tc_vec3_add(ray.origin, tc_vec3_scale(ray.direction, distance));
}

static tc_visual_item3d* next_item_after(tc_visual_scene3d* scene, uint64_t previous_order) {
    tc_visual_item3d* next = NULL;
    for (uint32_t index = 0; index < scene->items.capacity; ++index) {
        if (scene->items.states[index] != TC_SLOT_OCCUPIED)
            continue;
        const tc_handle local = {
            index,
            scene->items.generations[index],
        };
        tc_visual_item3d_slot* slot = slot_for(scene, local);
        tc_visual_item3d* candidate = slot != NULL ? slot->item : NULL;
        if (candidate != NULL && candidate->stable_order > previous_order &&
            (next == NULL || candidate->stable_order < next->stable_order)) {
            next = candidate;
        }
    }
    return next;
}

bool tc_visual_scene3d_hit_test(tc_visual_scene3d_handle scene_handle,
                                tc_ray3 world_ray,
                                tc_visual_hit_result3d* out_result) {
    if (out_result == NULL) {
        tc_log_error("tc_visual_scene3d_hit_test: out_result is required");
        return false;
    }
    *out_result = (tc_visual_hit_result3d){
        .item = tc_visual_item3d_handle_invalid(),
    };
    tc_visual_scene3d* scene = resolve_scene(scene_handle);
    const double direction_length = tc_vec3_length(world_ray.direction);
    if (scene == NULL || !isfinite(world_ray.origin.x) || !isfinite(world_ray.origin.y) ||
        !isfinite(world_ray.origin.z) || !isfinite(direction_length) || direction_length <= 1.0e-12) {
        tc_log_error("tc_visual_scene3d_hit_test: finite origin and non-zero finite direction are required");
        return false;
    }
    world_ray.direction = tc_vec3_scale(world_ray.direction, 1.0 / direction_length);

    bool found = false;
    const uint64_t traversal_revision = scene->order_revision;
    const size_t count = tc_pool_count(&scene->items);
    uint64_t previous_order = 0;
    for (size_t visited = 0; visited < count; ++visited) {
        tc_visual_item3d* item = next_item_after(scene, previous_order);
        if (item == NULL) {
            tc_log_error("tc_visual_scene3d_hit_test: inconsistent stable traversal state");
            return false;
        }
        previous_order = item->stable_order;
        if (item->vtable == NULL || item->vtable->hit_test == NULL || !item_effectively_hittable(item)) {
            continue;
        }
        const tc_affine3d world_from_local = item_world_transform(item);
        tc_affine3d local_from_world;
        if (!tc_affine3d_try_inverse(world_from_local, 1.0e-12, &local_from_world)) {
            tc_log_error("visual item3d '%s' has a singular world transform and is not hittable",
                         tc_visual_item3d_type_name(item));
            continue;
        }
        const tc_ray3 local_ray = {
            .origin = tc_affine3d_transform_point(local_from_world, world_ray.origin),
            .direction = tc_affine3d_transform_vector(local_from_world, world_ray.direction),
        };
        const tc_visual_hit_test_context3d context = {
            .world_ray = world_ray,
            .local_ray = local_ray,
            .world_from_local = world_from_local,
            .local_from_world = local_from_world,
        };
        tc_visual_hit_candidate3d candidate = {
            .distance = NAN,
            .part = 0,
        };
        const bool item_hit = item->vtable->hit_test(item, &context, &candidate);
        if (scene->order_revision != traversal_revision) {
            tc_log_error("tc_visual_scene3d_hit_test: scene topology mutated during item callback");
            *out_result = (tc_visual_hit_result3d){
                .item = tc_visual_item3d_handle_invalid(),
            };
            return false;
        }
        if (!item_hit)
            continue;
        if (!isfinite(candidate.distance) || candidate.distance <= 0.0) {
            tc_log_error("visual item3d '%s' returned an invalid hit distance", tc_visual_item3d_type_name(item));
            continue;
        }
        // Stable adoption order makes retaining the first exact-distance
        // candidate the deterministic tie break.
        if (found && candidate.distance >= out_result->distance)
            continue;
        found = true;
        out_result->item = item->handle;
        out_result->distance = candidate.distance;
        out_result->part = candidate.part;
        out_result->world_point = ray_point(world_ray, candidate.distance);
        out_result->local_point = ray_point(local_ray, candidate.distance);
    }
    return found;
}

tc_visual_item3d_handle tc_visual_item_paint_context3d_item(const tc_visual_item_paint_context3d* context) {
    return context != NULL ? context->item : tc_visual_item3d_handle_invalid();
}

tc_affine3d tc_visual_item_paint_context3d_world_from_local(const tc_visual_item_paint_context3d* context) {
    return context != NULL ? context->world_from_local : tc_affine3d_identity();
}

bool tc_visual_item_paint_context3d_effective_visible(const tc_visual_item_paint_context3d* context) {
    return context != NULL && context->effective_visible;
}

bool tc_visual_item_paint_context3d_effective_enabled(const tc_visual_item_paint_context3d* context) {
    return context != NULL && context->effective_enabled;
}

const tc_visual_view3d* tc_visual_item_paint_context3d_view(const tc_visual_item_paint_context3d* context) {
    return context != NULL ? context->view : NULL;
}

bool tc_visual_item_paint_context3d_submit(tc_visual_item_paint_context3d* context,
                                           const char* protocol,
                                           const void* payload,
                                           size_t payload_size) {
    if (context == NULL || context->sink == NULL || protocol == NULL || protocol[0] == '\0' ||
        (payload == NULL && payload_size != 0)) {
        tc_log_error("tc_visual_item_paint_context3d_submit: context, protocol and payload must be valid");
        if (context != NULL)
            context->submit_failed = true;
        return false;
    }
    const tc_visual_draw_submission3d submission = {
        .item = context->item,
        .world_from_local = context->world_from_local,
        .effective_visible = context->effective_visible,
        .effective_enabled = context->effective_enabled,
        .view = context->view,
        .packet =
            {
                .protocol = protocol,
                .payload = payload,
                .payload_size = payload_size,
            },
    };
    if (!context->sink->submit(&submission, context->sink->user_data)) {
        tc_log_error("visual item3d draw sink rejected protocol '%s'", protocol);
        context->submit_failed = true;
        return false;
    }
    return true;
}

static bool view_is_valid(const tc_visual_view3d* view) {
    if (view == NULL || view->viewport_width == 0 || view->viewport_height == 0 ||
        !isfinite(view->camera_world_position.x) || !isfinite(view->camera_world_position.y) ||
        !isfinite(view->camera_world_position.z)) {
        return false;
    }
    for (size_t index = 0; index < 16; ++index) {
        if (!isfinite(view->view_matrix.m[index]) || !isfinite(view->projection_matrix.m[index]))
            return false;
    }
    return true;
}

bool tc_visual_scene3d_paint(tc_visual_scene3d_handle scene_handle,
                             const tc_visual_view3d* view,
                             tc_visual_draw_sink3d* sink) {
    tc_visual_scene3d* scene = resolve_scene(scene_handle);
    if (scene == NULL || !view_is_valid(view) || sink == NULL || sink->begin == NULL || sink->submit == NULL ||
        sink->end == NULL || sink->abort == NULL) {
        tc_log_error("tc_visual_scene3d_paint: valid scene, view and transactional sink are required");
        return false;
    }

    const uint64_t traversal_revision = scene->order_revision;
    if (!sink->begin(view, sink->user_data)) {
        tc_log_error("tc_visual_scene3d_paint: draw sink failed to begin batch");
        return false;
    }
    if (scene->order_revision != traversal_revision) {
        tc_log_error("tc_visual_scene3d_paint: scene topology mutated during draw sink begin");
        sink->abort(sink->user_data);
        return false;
    }

    const size_t count = tc_pool_count(&scene->items);
    uint64_t previous_order = 0;
    for (size_t visited = 0; visited < count; ++visited) {
        tc_visual_item3d* item = next_item_after(scene, previous_order);
        if (item == NULL) {
            tc_log_error("tc_visual_scene3d_paint: inconsistent stable traversal state");
            sink->abort(sink->user_data);
            return false;
        }
        previous_order = item->stable_order;
        if (!item_effectively_visible(item) || item->vtable == NULL || item->vtable->paint == NULL)
            continue;

        tc_visual_item_paint_context3d context = {
            .item = item->handle,
            .world_from_local = item_world_transform(item),
            .effective_visible = true,
            .effective_enabled = item_effectively_enabled(item),
            .view = view,
            .sink = sink,
            .submit_failed = false,
        };
        const char* type_name = tc_visual_item3d_type_name(item);
        const bool painted = item->vtable->paint(item, &context);
        if (scene->order_revision != traversal_revision) {
            tc_log_error("tc_visual_scene3d_paint: scene topology mutated during item callback");
            sink->abort(sink->user_data);
            return false;
        }
        if (!painted || context.submit_failed) {
            tc_log_error("visual item3d '%s' failed to paint", type_name);
            sink->abort(sink->user_data);
            return false;
        }
    }

    if (!sink->end(sink->user_data)) {
        tc_log_error("tc_visual_scene3d_paint: draw sink failed to complete batch");
        sink->abort(sink->user_data);
        return false;
    }
    return true;
}
