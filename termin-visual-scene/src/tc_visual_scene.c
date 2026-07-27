#include "termin_visual_scene/tc_visual_scene.h"

#include <stdatomic.h>
#include <stdlib.h>
#include <threads.h>

#include <tcbase/tc_log.h>
#include <tcbase/tc_pool.h>

typedef struct tc_graphic_item_slot {
    void* payload;
    tc_graphic_item_deleter deleter;
    void* deleter_user_data;
    tc_handle parent;
    tc_handle first_child;
    tc_handle next_sibling;
    tc_handle previous_sibling;
} tc_graphic_item_slot;

struct tc_visual_scene {
    uint64_t id;
    tc_pool items;
    mtx_t mutex;
    bool clearing;
};

typedef struct tc_delete_record {
    void* payload;
    tc_graphic_item_deleter deleter;
    void* user_data;
} tc_delete_record;

typedef struct tc_walk_entry {
    tc_handle handle;
    bool expanded;
} tc_walk_entry;

static atomic_uint_fast64_t g_next_scene_id = 1;

static tc_handle local_handle(tc_graphic_item_handle handle) {
    tc_handle result = {handle.index, handle.generation};
    return result;
}

static tc_graphic_item_handle public_handle(const tc_visual_scene* scene, tc_handle handle) {
    if (tc_handle_is_invalid(handle)) {
        return tc_graphic_item_handle_invalid();
    }
    tc_graphic_item_handle result = {scene->id, handle.index, handle.generation};
    return result;
}

static bool lock_scene(tc_visual_scene* scene) {
    if (scene == NULL) {
        tc_log_error("tc_visual_scene: scene is NULL");
        return false;
    }
    if (mtx_lock(&scene->mutex) != thrd_success) {
        tc_log_error("tc_visual_scene: failed to lock scene mutex");
        return false;
    }
    return true;
}

static bool valid_locked(
    tc_visual_scene* scene,
    tc_graphic_item_handle handle,
    const char* operation)
{
    if (tc_graphic_item_handle_is_invalid(handle)) {
        tc_log_error("%s: graphic item handle is invalid", operation);
        return false;
    }
    if (handle.scene_id != scene->id) {
        tc_log_error("%s: graphic item belongs to another scene", operation);
        return false;
    }
    if (!tc_pool_is_valid(&scene->items, local_handle(handle))) {
        tc_log_error("%s: graphic item handle is stale", operation);
        return false;
    }
    return true;
}

static tc_graphic_item_slot* slot_locked(tc_visual_scene* scene, tc_handle handle) {
    return (tc_graphic_item_slot*)tc_pool_get(&scene->items, handle);
}

static void unlink_locked(tc_visual_scene* scene, tc_handle item_handle) {
    tc_graphic_item_slot* item = slot_locked(scene, item_handle);
    if (item == NULL || tc_handle_is_invalid(item->parent)) {
        if (item != NULL) {
            item->previous_sibling = TC_HANDLE_INVALID;
            item->next_sibling = TC_HANDLE_INVALID;
            item->parent = TC_HANDLE_INVALID;
        }
        return;
    }

    tc_graphic_item_slot* parent = slot_locked(scene, item->parent);
    if (parent != NULL && tc_handle_eq(parent->first_child, item_handle)) {
        parent->first_child = item->next_sibling;
    }
    if (!tc_handle_is_invalid(item->previous_sibling)) {
        tc_graphic_item_slot* previous = slot_locked(scene, item->previous_sibling);
        if (previous != NULL) previous->next_sibling = item->next_sibling;
    }
    if (!tc_handle_is_invalid(item->next_sibling)) {
        tc_graphic_item_slot* next = slot_locked(scene, item->next_sibling);
        if (next != NULL) next->previous_sibling = item->previous_sibling;
    }
    item->parent = TC_HANDLE_INVALID;
    item->previous_sibling = TC_HANDLE_INVALID;
    item->next_sibling = TC_HANDLE_INVALID;
}

static void append_child_locked(
    tc_visual_scene* scene,
    tc_handle parent_handle,
    tc_handle child_handle)
{
    tc_graphic_item_slot* parent = slot_locked(scene, parent_handle);
    tc_graphic_item_slot* child = slot_locked(scene, child_handle);
    child->parent = parent_handle;
    if (tc_handle_is_invalid(parent->first_child)) {
        parent->first_child = child_handle;
        return;
    }
    tc_handle cursor = parent->first_child;
    tc_graphic_item_slot* last = slot_locked(scene, cursor);
    while (!tc_handle_is_invalid(last->next_sibling)) {
        cursor = last->next_sibling;
        last = slot_locked(scene, cursor);
    }
    last->next_sibling = child_handle;
    child->previous_sibling = cursor;
}

static void run_delete_record(tc_delete_record record) {
    if (record.deleter != NULL) {
        record.deleter(record.payload, record.user_data);
    }
}

static bool free_slot_locked(tc_visual_scene* scene, tc_handle handle) {
    if (!tc_pool_free_slot(&scene->items, handle)) return false;
    if (scene->items.generations[handle.index] == 0) {
        scene->items.generations[handle.index] = 1;
    }
    return true;
}

tc_visual_scene* tc_visual_scene_create(void) {
    tc_visual_scene* scene = (tc_visual_scene*)calloc(1, sizeof(tc_visual_scene));
    if (scene == NULL) {
        tc_log_error("tc_visual_scene_create: allocation failed");
        return NULL;
    }
    scene->id = atomic_fetch_add_explicit(
        &g_next_scene_id, 1, memory_order_relaxed);
    if (scene->id == 0) {
        scene->id = atomic_fetch_add_explicit(
            &g_next_scene_id, 1, memory_order_relaxed);
    }
    if (mtx_init(&scene->mutex, mtx_plain) != thrd_success) {
        tc_log_error("tc_visual_scene_create: mutex initialization failed");
        free(scene);
        return NULL;
    }
    tc_pool_config config = {
        .max_capacity = 0,
        .initial_generation = 1,
        .allocate_low_indices_first = true,
        .name = "GraphicItem",
    };
    if (!tc_pool_init_ex(&scene->items, sizeof(tc_graphic_item_slot), 16, &config)) {
        tc_log_error("tc_visual_scene_create: item pool initialization failed");
        mtx_destroy(&scene->mutex);
        free(scene);
        return NULL;
    }
    return scene;
}

void tc_visual_scene_destroy(tc_visual_scene* scene) {
    if (scene == NULL) return;
    tc_visual_scene_clear(scene);
    tc_pool_free(&scene->items);
    mtx_destroy(&scene->mutex);
    free(scene);
}

uint64_t tc_visual_scene_id(const tc_visual_scene* scene) {
    return scene != NULL ? scene->id : 0;
}

size_t tc_visual_scene_item_count(tc_visual_scene* scene) {
    if (!lock_scene(scene)) return 0;
    const size_t count = tc_pool_count(&scene->items);
    mtx_unlock(&scene->mutex);
    return count;
}

bool tc_visual_scene_adopt(
    tc_visual_scene* scene,
    void* payload,
    tc_graphic_item_deleter deleter,
    void* deleter_user_data,
    tc_graphic_item_handle parent,
    tc_graphic_item_handle* out_handle)
{
    if (out_handle == NULL) {
        tc_log_error("tc_visual_scene_adopt: out_handle is NULL");
        if (deleter != NULL) deleter(payload, deleter_user_data);
        return false;
    }
    *out_handle = tc_graphic_item_handle_invalid();
    if (!lock_scene(scene)) {
        if (deleter != NULL) deleter(payload, deleter_user_data);
        return false;
    }
    const bool has_parent = !tc_graphic_item_handle_is_invalid(parent);
    if (scene->clearing) {
        tc_log_error("tc_visual_scene_adopt: scene teardown is in progress");
        mtx_unlock(&scene->mutex);
        if (deleter != NULL) deleter(payload, deleter_user_data);
        return false;
    }
    if (has_parent && !valid_locked(scene, parent, "tc_visual_scene_adopt")) {
        mtx_unlock(&scene->mutex);
        if (deleter != NULL) deleter(payload, deleter_user_data);
        return false;
    }

    tc_handle local = tc_pool_alloc(&scene->items);
    if (tc_handle_is_invalid(local)) {
        tc_log_error("tc_visual_scene_adopt: item allocation failed");
        mtx_unlock(&scene->mutex);
        if (deleter != NULL) deleter(payload, deleter_user_data);
        return false;
    }
    tc_graphic_item_slot* item = slot_locked(scene, local);
    *item = (tc_graphic_item_slot){
        .payload = payload,
        .deleter = deleter,
        .deleter_user_data = deleter_user_data,
        .parent = TC_HANDLE_INVALID,
        .first_child = TC_HANDLE_INVALID,
        .next_sibling = TC_HANDLE_INVALID,
        .previous_sibling = TC_HANDLE_INVALID,
    };
    if (has_parent) append_child_locked(scene, local_handle(parent), local);
    *out_handle = public_handle(scene, local);
    mtx_unlock(&scene->mutex);
    return true;
}

bool tc_visual_scene_create_item(
    tc_visual_scene* scene,
    tc_graphic_item_handle parent,
    tc_graphic_item_handle* out_handle)
{
    return tc_visual_scene_adopt(
        scene, NULL, NULL, NULL, parent, out_handle);
}

bool tc_visual_scene_resolve(
    tc_visual_scene* scene,
    tc_graphic_item_handle handle,
    tc_graphic_item_view* out_view)
{
    if (out_view == NULL) {
        tc_log_error("tc_visual_scene_resolve: out_view is NULL");
        return false;
    }
    if (!lock_scene(scene)) return false;
    if (!valid_locked(scene, handle, "tc_visual_scene_resolve")) {
        mtx_unlock(&scene->mutex);
        return false;
    }
    tc_graphic_item_slot* item = slot_locked(scene, local_handle(handle));
    *out_view = (tc_graphic_item_view){
        .payload = item->payload,
        .parent = public_handle(scene, item->parent),
        .first_child = public_handle(scene, item->first_child),
        .next_sibling = public_handle(scene, item->next_sibling),
        .previous_sibling = public_handle(scene, item->previous_sibling),
    };
    mtx_unlock(&scene->mutex);
    return true;
}

bool tc_visual_scene_reparent(
    tc_visual_scene* scene,
    tc_graphic_item_handle item,
    tc_graphic_item_handle new_parent)
{
    if (!lock_scene(scene)) return false;
    if (!valid_locked(scene, item, "tc_visual_scene_reparent") ||
        !valid_locked(scene, new_parent, "tc_visual_scene_reparent")) {
        mtx_unlock(&scene->mutex);
        return false;
    }
    const tc_handle item_local = local_handle(item);
    tc_handle cursor = local_handle(new_parent);
    while (!tc_handle_is_invalid(cursor)) {
        if (tc_handle_eq(cursor, item_local)) {
            tc_log_error("tc_visual_scene_reparent: cyclic topology rejected");
            mtx_unlock(&scene->mutex);
            return false;
        }
        tc_graphic_item_slot* ancestor = slot_locked(scene, cursor);
        cursor = ancestor->parent;
    }
    unlink_locked(scene, item_local);
    append_child_locked(scene, local_handle(new_parent), item_local);
    mtx_unlock(&scene->mutex);
    return true;
}

bool tc_visual_scene_detach(tc_visual_scene* scene, tc_graphic_item_handle item) {
    if (!lock_scene(scene)) return false;
    if (!valid_locked(scene, item, "tc_visual_scene_detach")) {
        mtx_unlock(&scene->mutex);
        return false;
    }
    unlink_locked(scene, local_handle(item));
    mtx_unlock(&scene->mutex);
    return true;
}

bool tc_visual_scene_destroy_leaf(
    tc_visual_scene* scene,
    tc_graphic_item_handle item)
{
    if (!lock_scene(scene)) return false;
    if (!valid_locked(scene, item, "tc_visual_scene_destroy_leaf")) {
        mtx_unlock(&scene->mutex);
        return false;
    }
    const tc_handle local = local_handle(item);
    tc_graphic_item_slot* slot = slot_locked(scene, local);
    if (!tc_handle_is_invalid(slot->first_child)) {
        tc_log_error("tc_visual_scene_destroy_leaf: item has children");
        mtx_unlock(&scene->mutex);
        return false;
    }
    tc_delete_record record = {slot->payload, slot->deleter, slot->deleter_user_data};
    unlink_locked(scene, local);
    free_slot_locked(scene, local);
    mtx_unlock(&scene->mutex);
    run_delete_record(record);
    return true;
}

bool tc_visual_scene_destroy_subtree(
    tc_visual_scene* scene,
    tc_graphic_item_handle root)
{
    if (!lock_scene(scene)) return false;
    if (!valid_locked(scene, root, "tc_visual_scene_destroy_subtree")) {
        mtx_unlock(&scene->mutex);
        return false;
    }
    const size_t count = tc_pool_count(&scene->items);
    tc_walk_entry* stack = (tc_walk_entry*)malloc(sizeof(tc_walk_entry) * count * 2);
    tc_handle* order = (tc_handle*)malloc(sizeof(tc_handle) * count);
    tc_delete_record* records =
        (tc_delete_record*)malloc(sizeof(tc_delete_record) * count);
    if (stack == NULL || order == NULL || records == NULL) {
        tc_log_error("tc_visual_scene_destroy_subtree: traversal allocation failed");
        free(stack);
        free(order);
        free(records);
        mtx_unlock(&scene->mutex);
        return false;
    }

    size_t stack_size = 0;
    size_t order_size = 0;
    stack[stack_size++] = (tc_walk_entry){local_handle(root), false};
    while (stack_size > 0) {
        tc_walk_entry entry = stack[--stack_size];
        tc_graphic_item_slot* item = slot_locked(scene, entry.handle);
        if (entry.expanded) {
            order[order_size++] = entry.handle;
            continue;
        }
        stack[stack_size++] = (tc_walk_entry){entry.handle, true};
        tc_handle child = item->first_child;
        while (!tc_handle_is_invalid(child)) {
            stack[stack_size++] = (tc_walk_entry){child, false};
            child = slot_locked(scene, child)->next_sibling;
        }
    }

    unlink_locked(scene, local_handle(root));
    for (size_t i = 0; i < order_size; ++i) {
        tc_graphic_item_slot* item = slot_locked(scene, order[i]);
        records[i] = (tc_delete_record){
            item->payload, item->deleter, item->deleter_user_data};
        free_slot_locked(scene, order[i]);
    }
    mtx_unlock(&scene->mutex);

    for (size_t i = 0; i < order_size; ++i) run_delete_record(records[i]);
    free(stack);
    free(order);
    free(records);
    return true;
}

void tc_visual_scene_clear(tc_visual_scene* scene) {
    if (!lock_scene(scene)) return;
    if (scene->clearing) {
        tc_log_error("tc_visual_scene_clear: recursive teardown rejected");
        mtx_unlock(&scene->mutex);
        return;
    }
    scene->clearing = true;
    while (scene->items.count > 0) {
        uint32_t index = 0;
        while (index < scene->items.capacity &&
               scene->items.states[index] != TC_SLOT_OCCUPIED) {
            ++index;
        }
        if (index == scene->items.capacity) {
            tc_log_error("tc_visual_scene_clear: pool count/state mismatch");
            break;
        }
        tc_graphic_item_slot* item =
            (tc_graphic_item_slot*)tc_pool_get_unchecked(&scene->items, index);
        tc_delete_record record = (tc_delete_record){
            item->payload, item->deleter, item->deleter_user_data};
        tc_handle handle = {index, scene->items.generations[index]};
        free_slot_locked(scene, handle);
        mtx_unlock(&scene->mutex);
        run_delete_record(record);
        if (!lock_scene(scene)) return;
    }
    scene->clearing = false;
    mtx_unlock(&scene->mutex);
}
