#include "termin_visual_scene/tc_visual_scene.h"

#include <stdatomic.h>
#include <stdlib.h>

#include <tcbase/tc_log.h>
#include <tcbase/tc_pool.h>

#include "tc_visual_scene_internal.h"

typedef struct tc_graphic_item_slot {
    tc_graphic_item* item;
} tc_graphic_item_slot;

typedef struct tc_visual_scene_slot {
    tc_visual_scene* scene;
} tc_visual_scene_slot;

struct tc_visual_scene {
    uint64_t id;
    tc_pool items;
    uint64_t next_stable_order;
    uint64_t order_revision;
    bool clearing;
};

static atomic_uint_fast64_t g_next_scene_id = 1;
static tc_pool g_scene_pool;
static bool g_scene_pool_initialized = false;

static bool ensure_scene_pool(void) {
    if (g_scene_pool_initialized) return true;
    if (!tc_pool_init(
            &g_scene_pool,
            sizeof(tc_visual_scene_slot),
            16)) {
        tc_log_error(
            "tc_visual_scene: scene pool initialization failed");
        return false;
    }
    g_scene_pool_initialized = true;
    return true;
}

void tc_visual_scene_touch_order(tc_visual_scene* scene) {
    if (scene == NULL) return;
    ++scene->order_revision;
    if (scene->order_revision == 0) scene->order_revision = 1;
}

static tc_handle scene_base_handle(
    tc_visual_scene_handle handle)
{
    return (tc_handle){handle.index, handle.generation};
}

static tc_visual_scene* resolve_scene(
    tc_visual_scene_handle handle)
{
    if (!g_scene_pool_initialized ||
        tc_visual_scene_handle_is_invalid(handle)) {
        return NULL;
    }
    tc_visual_scene_slot* slot =
        (tc_visual_scene_slot*)tc_pool_get(
            &g_scene_pool, scene_base_handle(handle));
    return slot != NULL ? slot->scene : NULL;
}

static tc_handle local_handle(
    tc_graphic_item_handle handle)
{
    const tc_handle result = {
        handle.index,
        handle.generation,
    };
    return result;
}

static tc_graphic_item_handle public_handle(
    const tc_visual_scene* scene,
    tc_handle handle)
{
    if (tc_handle_is_invalid(handle)) {
        return tc_graphic_item_handle_invalid();
    }
    const tc_graphic_item_handle result = {
        scene->id,
        handle.index,
        handle.generation,
    };
    return result;
}

static tc_graphic_item_slot* slot_for(
    tc_visual_scene* scene,
    tc_handle handle)
{
    return (tc_graphic_item_slot*)tc_pool_get(
        &scene->items, handle);
}

static const tc_graphic_item_slot* slot_for_const(
    const tc_visual_scene* scene,
    tc_handle handle)
{
    return (const tc_graphic_item_slot*)tc_pool_get(
        (tc_pool*)&scene->items, handle);
}

static bool valid_handle(
    const tc_visual_scene* scene,
    tc_graphic_item_handle handle)
{
    if (scene == NULL ||
        tc_graphic_item_handle_is_invalid(handle) ||
        handle.scene_id != scene->id ||
        !tc_pool_is_valid(
            &scene->items, local_handle(handle))) {
        return false;
    }
    const tc_graphic_item_slot* slot =
        slot_for_const(scene, local_handle(handle));
    return slot != NULL &&
        slot->item != NULL &&
        slot->item->scene == scene &&
        slot->item->handle.scene_id == handle.scene_id &&
        slot->item->handle.index == handle.index &&
        slot->item->handle.generation == handle.generation;
}

static bool release_slot(
    tc_visual_scene* scene,
    tc_handle handle)
{
    if (!tc_pool_free_slot(&scene->items, handle)) {
        return false;
    }
    if (scene->items.generations[handle.index] == 0) {
        scene->items.generations[handle.index] = 1;
    }
    return true;
}

static bool link_runtime_type(tc_graphic_item* item) {
    const char* type_name = tc_graphic_item_type_name(item);
    return type_name == NULL ||
        !tc_runtime_type_registry_has_type(type_name) ||
        tc_runtime_type_registry_link_instance(
            type_name,
            &item->runtime_type_link,
            item);
}

static void destroy_object(
    tc_visual_scene* scene,
    tc_graphic_item* item)
{
    while (item->child_count != 0) {
        destroy_object(
            scene,
            item->children[item->child_count - 1]);
    }
    tc_graphic_item_detach(item);

    const tc_handle handle = local_handle(item->handle);
    tc_graphic_item_slot* slot = slot_for(scene, handle);
    const tc_graphic_item_deleter deleter = item->deleter;
    if (slot != NULL) slot->item = NULL;
    release_slot(scene, handle);

    item->scene = NULL;
    item->handle = tc_graphic_item_handle_invalid();
    item->deleter = NULL;
    free(item->children);
    item->children = NULL;
    item->child_count = 0;
    item->child_capacity = 0;
    tc_runtime_type_registry_unlink_instance(
        &item->runtime_type_link);

    if (item->vtable != NULL &&
        item->vtable->on_destroy != NULL) {
        item->vtable->on_destroy(item, scene);
    }
    deleter(item);
}

tc_visual_scene_handle tc_visual_scene_create(void) {
    if (!ensure_scene_pool()) {
        return tc_visual_scene_handle_invalid();
    }
    tc_visual_scene* scene =
        (tc_visual_scene*)calloc(
            1, sizeof(tc_visual_scene));
    if (scene == NULL) {
        tc_log_error(
            "tc_visual_scene_create: allocation failed");
        return tc_visual_scene_handle_invalid();
    }
    scene->id = atomic_fetch_add_explicit(
        &g_next_scene_id, 1, memory_order_relaxed);
    if (scene->id == 0) {
        scene->id = atomic_fetch_add_explicit(
            &g_next_scene_id, 1, memory_order_relaxed);
    }
    const tc_pool_config config = {
        .max_capacity = 0,
        .initial_generation = 1,
        .allocate_low_indices_first = true,
        .name = "GraphicItem",
    };
    if (!tc_pool_init_ex(
            &scene->items,
            sizeof(tc_graphic_item_slot),
            16,
            &config)) {
        tc_log_error(
            "tc_visual_scene_create: pool initialization failed");
        free(scene);
        return tc_visual_scene_handle_invalid();
    }
    scene->next_stable_order = 1;
    scene->order_revision = 1;
    const tc_handle local = tc_pool_alloc(&g_scene_pool);
    if (tc_handle_is_invalid(local)) {
        tc_pool_free(&scene->items);
        free(scene);
        return tc_visual_scene_handle_invalid();
    }
    tc_visual_scene_slot* slot =
        (tc_visual_scene_slot*)tc_pool_get(
            &g_scene_pool, local);
    slot->scene = scene;
    return (tc_visual_scene_handle){
        local.index, local.generation};
}

static void clear_scene(tc_visual_scene* scene);

void tc_visual_scene_destroy(tc_visual_scene_handle handle) {
    tc_visual_scene* scene = resolve_scene(handle);
    if (scene == NULL) return;
    clear_scene(scene);
    tc_pool_free(&scene->items);
    tc_visual_scene_slot* slot =
        (tc_visual_scene_slot*)tc_pool_get(
            &g_scene_pool, scene_base_handle(handle));
    if (slot != NULL) slot->scene = NULL;
    tc_pool_free_slot(
        &g_scene_pool, scene_base_handle(handle));
    free(scene);
}

bool tc_visual_scene_is_valid(tc_visual_scene_handle handle) {
    return resolve_scene(handle) != NULL;
}

uint64_t tc_visual_scene_id(
    tc_visual_scene_handle handle)
{
    const tc_visual_scene* scene = resolve_scene(handle);
    return scene != NULL ? scene->id : 0;
}

size_t tc_visual_scene_item_count(
    tc_visual_scene_handle handle)
{
    const tc_visual_scene* scene = resolve_scene(handle);
    return scene != NULL
        ? tc_pool_count(&scene->items)
        : 0;
}

uint64_t tc_visual_scene_order_revision(
    tc_visual_scene_handle handle)
{
    const tc_visual_scene* scene = resolve_scene(handle);
    return scene != NULL ? scene->order_revision : 0;
}

tc_graphic_item_handle tc_visual_scene_adopt_item(
    tc_visual_scene_handle scene_handle,
    tc_graphic_item* item,
    tc_graphic_item_deleter deleter)
{
    tc_visual_scene* scene = resolve_scene(scene_handle);
    if (scene == NULL || item == NULL || deleter == NULL) {
        tc_log_error(
            "tc_visual_scene_adopt_item: scene, item and deleter are required");
        return tc_graphic_item_handle_invalid();
    }
    if (scene->clearing) {
        tc_log_error(
            "tc_visual_scene_adopt_item: scene is clearing");
        deleter(item);
        return tc_graphic_item_handle_invalid();
    }
    if (tc_graphic_item_is_attached(item) ||
        item->deleter != NULL) {
        tc_log_error(
            "tc_visual_scene_adopt_item: item is already attached");
        return tc_graphic_item_handle_invalid();
    }
    if (item->vtable == NULL || item->body == NULL ||
        item->parent != NULL || item->child_count != 0 ||
        item->children != NULL) {
        tc_log_error(
            "tc_visual_scene_adopt_item: item is not an initialized tree root");
        deleter(item);
        return tc_graphic_item_handle_invalid();
    }

    const tc_handle local = tc_pool_alloc(&scene->items);
    if (tc_handle_is_invalid(local)) {
        tc_log_error(
            "tc_visual_scene_adopt_item: pool allocation failed");
        deleter(item);
        return tc_graphic_item_handle_invalid();
    }
    tc_graphic_item_slot* slot = slot_for(scene, local);
    slot->item = item;
    item->deleter = deleter;
    item->scene = scene;
    item->handle = public_handle(scene, local);
    item->stable_order = scene->next_stable_order++;

    if (!link_runtime_type(item)) {
        tc_log_error(
            "tc_visual_scene_adopt_item: runtime type link failed");
        slot->item = NULL;
        item->deleter = NULL;
        item->scene = NULL;
        item->handle = tc_graphic_item_handle_invalid();
        release_slot(scene, local);
        deleter(item);
        return tc_graphic_item_handle_invalid();
    }
    tc_visual_scene_touch_order(scene);
    return item->handle;
}

bool tc_visual_scene_replace_item(
    tc_visual_scene_handle scene_handle,
    tc_graphic_item_handle handle,
    tc_graphic_item* replacement,
    tc_graphic_item_deleter deleter)
{
    tc_visual_scene* scene = resolve_scene(scene_handle);
    if (!valid_handle(scene, handle) ||
        replacement == NULL || deleter == NULL) {
        if (replacement != NULL && deleter != NULL &&
            !tc_graphic_item_is_attached(replacement)) {
            deleter(replacement);
        }
        return false;
    }
    if (tc_graphic_item_is_attached(replacement) ||
        replacement->deleter != NULL) {
        tc_log_error(
            "tc_visual_scene_replace_item: replacement is already attached");
        return false;
    }
    if (replacement->parent != NULL ||
        replacement->child_count != 0 ||
        replacement->children != NULL ||
        replacement->vtable == NULL ||
        replacement->body == NULL) {
        tc_log_error(
            "tc_visual_scene_replace_item: replacement is not an unattached root");
        deleter(replacement);
        return false;
    }

    tc_graphic_item_slot* slot =
        slot_for(scene, local_handle(handle));
    tc_graphic_item* previous = slot->item;
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
    replacement->opacity = previous->opacity;
    replacement->z_order = previous->z_order;
    replacement->stable_order = previous->stable_order;

    if (replacement->parent != NULL) {
        for (size_t index = 0;
             index < replacement->parent->child_count;
             ++index) {
            if (replacement->parent->children[index] == previous) {
                replacement->parent->children[index] = replacement;
                break;
            }
        }
    }
    for (size_t index = 0;
         index < replacement->child_count;
         ++index) {
        replacement->children[index]->parent = replacement;
    }
    slot->item = replacement;

    const tc_graphic_item_deleter previous_deleter =
        previous->deleter;
    previous->scene = NULL;
    previous->handle = tc_graphic_item_handle_invalid();
    previous->deleter = NULL;
    previous->parent = NULL;
    previous->children = NULL;
    previous->child_count = 0;
    previous->child_capacity = 0;
    tc_runtime_type_registry_unlink_instance(
        &previous->runtime_type_link);
    if (previous->vtable != NULL &&
        previous->vtable->on_destroy != NULL) {
        previous->vtable->on_destroy(previous, scene);
    }
    previous_deleter(previous);
    tc_visual_scene_touch_order(scene);
    return true;
}

tc_graphic_item* tc_visual_scene_resolve_item(
    tc_visual_scene_handle scene_handle,
    tc_graphic_item_handle handle)
{
    tc_visual_scene* scene = resolve_scene(scene_handle);
    if (!valid_handle(scene, handle)) return NULL;
    return slot_for(scene, local_handle(handle))->item;
}

const tc_graphic_item*
tc_visual_scene_resolve_item_const(
    tc_visual_scene_handle scene_handle,
    tc_graphic_item_handle handle)
{
    const tc_visual_scene* scene = resolve_scene(scene_handle);
    if (!valid_handle(scene, handle)) return NULL;
    return slot_for_const(
        scene, local_handle(handle))->item;
}

size_t tc_visual_scene_copy_items(
    tc_visual_scene_handle scene_handle,
    tc_graphic_item** out_items,
    size_t capacity)
{
    const tc_visual_scene* scene = resolve_scene(scene_handle);
    if (scene == NULL ||
        (out_items == NULL && capacity != 0)) {
        return 0;
    }
    const size_t count = tc_pool_count(&scene->items);
    size_t written = 0;
    uint64_t previous_order = 0;
    while (written < capacity && written < count) {
        const tc_graphic_item* next = NULL;
        for (uint32_t index = 0;
             index < scene->items.capacity;
             ++index) {
            if (scene->items.states[index] != TC_SLOT_OCCUPIED) continue;
            const tc_handle local = {
                index,
                scene->items.generations[index],
            };
            const tc_graphic_item_slot* slot =
                slot_for_const(scene, local);
            const tc_graphic_item* candidate =
                slot != NULL ? slot->item : NULL;
            if (candidate != NULL &&
                candidate->stable_order > previous_order &&
                (next == NULL ||
                 candidate->stable_order < next->stable_order)) {
                next = candidate;
            }
        }
        if (next == NULL) break;
        out_items[written++] = (tc_graphic_item*)next;
        previous_order = next->stable_order;
    }
    return count;
}

size_t tc_visual_scene_copy_item_handles(
    tc_visual_scene_handle scene_handle,
    tc_graphic_item_handle* out_handles,
    size_t capacity)
{
    const tc_visual_scene* scene = resolve_scene(scene_handle);
    if (scene == NULL ||
        (out_handles == NULL && capacity != 0)) {
        return 0;
    }
    const size_t count = tc_pool_count(&scene->items);
    size_t written = 0;
    uint64_t previous_order = 0;
    while (written < capacity && written < count) {
        const tc_graphic_item* next = NULL;
        for (uint32_t index = 0;
             index < scene->items.capacity;
             ++index) {
            if (scene->items.states[index] != TC_SLOT_OCCUPIED) continue;
            const tc_handle local = {
                index,
                scene->items.generations[index],
            };
            const tc_graphic_item_slot* slot =
                slot_for_const(scene, local);
            const tc_graphic_item* candidate =
                slot != NULL ? slot->item : NULL;
            if (candidate != NULL &&
                candidate->stable_order > previous_order &&
                (next == NULL ||
                 candidate->stable_order < next->stable_order)) {
                next = candidate;
            }
        }
        if (next == NULL) break;
        out_handles[written++] = next->handle;
        previous_order = next->stable_order;
    }
    return count;
}

bool tc_visual_scene_destroy_item(
    tc_visual_scene_handle scene_handle,
    tc_graphic_item_handle handle)
{
    tc_visual_scene* scene = resolve_scene(scene_handle);
    tc_graphic_item* item =
        valid_handle(scene, handle)
            ? slot_for(scene, local_handle(handle))->item
            : NULL;
    if (item == NULL) return false;
    tc_visual_scene_touch_order(scene);
    destroy_object(scene, item);
    return true;
}

static void clear_scene(tc_visual_scene* scene) {
    if (scene == NULL || scene->clearing) return;
    scene->clearing = true;
    while (tc_pool_count(&scene->items) != 0) {
        tc_graphic_item* item = NULL;
        for (size_t index = 0;
             index < scene->items.capacity;
             ++index) {
            tc_handle local = {
                (uint32_t)index,
                scene->items.generations[index]};
            const tc_graphic_item_slot* slot =
                slot_for_const(scene, local);
            if (slot != NULL && slot->item != NULL) {
                item = slot->item;
                break;
            }
        }
        if (item == NULL) break;
        destroy_object(scene, item);
    }
    scene->clearing = false;
}

void tc_visual_scene_clear(tc_visual_scene_handle handle) {
    tc_visual_scene* scene = resolve_scene(handle);
    if (scene != NULL) clear_scene(scene);
}
