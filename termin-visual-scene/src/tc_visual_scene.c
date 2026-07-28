#include "termin_visual_scene/tc_visual_scene.h"

#include <math.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <threads.h>

#include <tcbase/tc_log.h>
#include <tcbase/tc_pool.h>

typedef struct tc_graphic_item_slot {
    tc_graphic_item* item;
} tc_graphic_item_slot;

struct tc_visual_scene {
    uint64_t id;
    tc_pool items;
    mtx_t mutex;
    bool clearing;
    uint64_t next_stable_order;
    uint64_t revision;
};

typedef struct tc_delete_record {
    tc_graphic_item* item;
    tc_graphic_item_deleter deleter;
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
    tc_graphic_item_slot* slot =
        (tc_graphic_item_slot*)tc_pool_get(
            &scene->items, local_handle(handle));
    if (slot == NULL || slot->item == NULL ||
        slot->item->scene != scene ||
        slot->item->handle.scene_id != handle.scene_id ||
        slot->item->handle.index != handle.index ||
        slot->item->handle.generation != handle.generation) {
        tc_log_error("%s: graphic item slot/object identity mismatch", operation);
        return false;
    }
    return true;
}

static tc_graphic_item_slot* slot_locked(tc_visual_scene* scene, tc_handle handle) {
    return (tc_graphic_item_slot*)tc_pool_get(&scene->items, handle);
}

static tc_graphic_item* item_locked(tc_visual_scene* scene, tc_handle handle) {
    tc_graphic_item_slot* slot = slot_locked(scene, handle);
    return slot != NULL ? slot->item : NULL;
}

static void unlink_locked(tc_visual_scene* scene, tc_handle item_handle) {
    tc_graphic_item* item = item_locked(scene, item_handle);
    if (item == NULL || tc_graphic_item_handle_is_invalid(item->parent)) {
        if (item != NULL) {
            item->previous_sibling = tc_graphic_item_handle_invalid();
            item->next_sibling = tc_graphic_item_handle_invalid();
            item->parent = tc_graphic_item_handle_invalid();
        }
        return;
    }

    tc_graphic_item* parent = item_locked(scene, local_handle(item->parent));
    if (parent != NULL &&
        parent->first_child.index == item_handle.index &&
        parent->first_child.generation == item_handle.generation) {
        parent->first_child = item->next_sibling;
    }
    if (!tc_graphic_item_handle_is_invalid(item->previous_sibling)) {
        tc_graphic_item* previous =
            item_locked(scene, local_handle(item->previous_sibling));
        if (previous != NULL) previous->next_sibling = item->next_sibling;
    }
    if (!tc_graphic_item_handle_is_invalid(item->next_sibling)) {
        tc_graphic_item* next =
            item_locked(scene, local_handle(item->next_sibling));
        if (next != NULL) next->previous_sibling = item->previous_sibling;
    }
    item->parent = tc_graphic_item_handle_invalid();
    item->previous_sibling = tc_graphic_item_handle_invalid();
    item->next_sibling = tc_graphic_item_handle_invalid();
}

static void append_child_locked(
    tc_visual_scene* scene,
    tc_handle parent_handle,
    tc_handle child_handle)
{
    tc_graphic_item* parent = item_locked(scene, parent_handle);
    tc_graphic_item* child = item_locked(scene, child_handle);
    child->parent = public_handle(scene, parent_handle);
    if (tc_graphic_item_handle_is_invalid(parent->first_child)) {
        parent->first_child = public_handle(scene, child_handle);
        return;
    }
    tc_handle cursor = local_handle(parent->first_child);
    tc_graphic_item* last = item_locked(scene, cursor);
    while (!tc_graphic_item_handle_is_invalid(last->next_sibling)) {
        cursor = local_handle(last->next_sibling);
        last = item_locked(scene, cursor);
    }
    last->next_sibling = public_handle(scene, child_handle);
    child->previous_sibling = public_handle(scene, cursor);
}

static void run_delete_record(tc_delete_record record) {
    if (record.item == NULL) return;
    if (record.item->vtable != NULL &&
        record.item->vtable->on_destroy != NULL) {
        record.item->vtable->on_destroy(record.item);
    }
    tc_runtime_type_registry_unlink_instance(
        &record.item->runtime_type_link);
    if (record.deleter != NULL) {
        record.deleter(record.item);
    }
}

static bool free_slot_locked(tc_visual_scene* scene, tc_handle handle) {
    if (!tc_pool_free_slot(&scene->items, handle)) return false;
    if (scene->items.generations[handle.index] == 0) {
        scene->items.generations[handle.index] = 1;
    }
    return true;
}

static tc_delete_record prepare_delete_locked(
    tc_visual_scene* scene,
    tc_handle handle)
{
    tc_graphic_item_slot* slot = slot_locked(scene, handle);
    tc_graphic_item* item = slot != NULL ? slot->item : NULL;
    tc_delete_record record = {
        item,
        item != NULL ? item->deleter : NULL,
    };
    if (item == NULL) {
        return record;
    }
    slot->item = NULL;
    item->scene = NULL;
    item->handle = tc_graphic_item_handle_invalid();
    item->parent = tc_graphic_item_handle_invalid();
    item->first_child = tc_graphic_item_handle_invalid();
    item->next_sibling = tc_graphic_item_handle_invalid();
    item->previous_sibling = tc_graphic_item_handle_invalid();
    item->deleter = NULL;
    item->dirty_flags = TC_GRAPHIC_ITEM_DIRTY_ALL;
    free_slot_locked(scene, handle);
    return record;
}

static const tc_graphic_item_vtable g_generic_item_vtable = {
    .type_name = "termin.visual.GraphicItem",
};

static void delete_generic_item(tc_graphic_item* item) {
    free(item);
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
    scene->next_stable_order = 1;
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

size_t tc_visual_scene_copy_handles(
    tc_visual_scene* scene,
    tc_graphic_item_handle* out_handles,
    size_t capacity)
{
    if (out_handles == NULL && capacity != 0) {
        tc_log_error(
            "tc_visual_scene_copy_handles: out_handles is NULL with non-zero capacity");
        return 0;
    }
    if (!lock_scene(scene)) return 0;
    const size_t count = tc_pool_count(&scene->items);
    size_t written = 0;
    for (uint32_t index = 0;
         index < scene->items.capacity && written < capacity;
         ++index) {
        if (scene->items.states[index] != TC_SLOT_OCCUPIED) continue;
        const tc_handle local = {
            index,
            scene->items.generations[index],
        };
        out_handles[written++] = public_handle(scene, local);
    }
    mtx_unlock(&scene->mutex);
    return count;
}

bool tc_visual_scene_adopt(
    tc_visual_scene* scene,
    tc_graphic_item* item,
    tc_graphic_item_deleter deleter,
    tc_graphic_item_handle parent,
    tc_graphic_item_handle* out_handle)
{
    if (item == NULL) {
        tc_log_error("tc_visual_scene_adopt: item is NULL");
        return false;
    }
    if (deleter == NULL) {
        tc_log_error("tc_visual_scene_adopt: owned item adoption requires a deleter");
        return false;
    }
    if (item->vtable == NULL || item->body == NULL) {
        tc_log_error("tc_visual_scene_adopt: item is not fully initialized");
        deleter(item);
        return false;
    }
    if (tc_graphic_item_is_attached(item) || item->deleter != NULL) {
        tc_log_error("tc_visual_scene_adopt: item is already attached");
        return false;
    }
    if (out_handle == NULL) {
        tc_log_error("tc_visual_scene_adopt: out_handle is NULL");
        deleter(item);
        return false;
    }
    *out_handle = tc_graphic_item_handle_invalid();
    if (!lock_scene(scene)) {
        deleter(item);
        return false;
    }
    const bool has_parent = !tc_graphic_item_handle_is_invalid(parent);
    if (scene->clearing) {
        tc_log_error("tc_visual_scene_adopt: scene teardown is in progress");
        mtx_unlock(&scene->mutex);
        deleter(item);
        return false;
    }
    if (has_parent && !valid_locked(scene, parent, "tc_visual_scene_adopt")) {
        mtx_unlock(&scene->mutex);
        deleter(item);
        return false;
    }

    tc_handle local = tc_pool_alloc(&scene->items);
    if (tc_handle_is_invalid(local)) {
        tc_log_error("tc_visual_scene_adopt: item allocation failed");
        mtx_unlock(&scene->mutex);
        deleter(item);
        return false;
    }
    tc_graphic_item_slot* slot = slot_locked(scene, local);
    slot->item = item;
    item->deleter = deleter;
    item->scene = scene;
    item->handle = public_handle(scene, local);
    item->parent = tc_graphic_item_handle_invalid();
    item->first_child = tc_graphic_item_handle_invalid();
    item->next_sibling = tc_graphic_item_handle_invalid();
    item->previous_sibling = tc_graphic_item_handle_invalid();
    item->stable_order = scene->next_stable_order++;
    item->revision = ++scene->revision;
    item->topology_revision = item->revision;
    item->dirty_flags = TC_GRAPHIC_ITEM_DIRTY_ALL;
    const char* type_name = tc_graphic_item_type_name(item);
    if (type_name != NULL &&
        tc_runtime_type_registry_has_type(type_name) &&
        !tc_runtime_type_registry_link_instance(
            type_name, &item->runtime_type_link, item)) {
        tc_log_error(
            "tc_visual_scene_adopt: failed to link runtime type '%s'",
            type_name);
        slot->item = NULL;
        item->deleter = NULL;
        item->scene = NULL;
        item->handle = tc_graphic_item_handle_invalid();
        free_slot_locked(scene, local);
        mtx_unlock(&scene->mutex);
        deleter(item);
        return false;
    }
    if (has_parent) append_child_locked(scene, local_handle(parent), local);
    *out_handle = item->handle;
    mtx_unlock(&scene->mutex);
    return true;
}

bool tc_visual_scene_create_item(
    tc_visual_scene* scene,
    tc_graphic_item_handle parent,
    tc_graphic_item_handle* out_handle)
{
    tc_graphic_item* item =
        (tc_graphic_item*)calloc(1, sizeof(tc_graphic_item));
    if (item == NULL) {
        tc_log_error("tc_visual_scene_create_item: allocation failed");
        if (out_handle != NULL) {
            *out_handle = tc_graphic_item_handle_invalid();
        }
        return false;
    }
    tc_graphic_item_init_unowned(
        item, &g_generic_item_vtable, TC_LANGUAGE_C, item);
    return tc_visual_scene_adopt(
        scene, item, delete_generic_item, parent, out_handle);
}

bool tc_visual_scene_replace_item(
    tc_visual_scene* scene,
    tc_graphic_item_handle handle,
    tc_graphic_item* replacement,
    tc_graphic_item_deleter deleter)
{
    if (replacement == NULL || deleter == NULL) {
        tc_log_error(
            "tc_visual_scene_replace_item: replacement and deleter are required");
        return false;
    }
    if (replacement->vtable == NULL || replacement->body == NULL) {
        tc_log_error(
            "tc_visual_scene_replace_item: replacement is not initialized");
        deleter(replacement);
        return false;
    }
    if (tc_graphic_item_is_attached(replacement) ||
        replacement->deleter != NULL) {
        tc_log_error(
            "tc_visual_scene_replace_item: replacement is already attached");
        return false;
    }
    if (!lock_scene(scene)) {
        deleter(replacement);
        return false;
    }
    if (scene->clearing ||
        !valid_locked(scene, handle, "tc_visual_scene_replace_item")) {
        mtx_unlock(&scene->mutex);
        deleter(replacement);
        return false;
    }

    tc_graphic_item_slot* slot =
        slot_locked(scene, local_handle(handle));
    tc_graphic_item* previous = slot->item;
    const tc_delete_record previous_record = {
        previous,
        previous->deleter,
    };

    const char* type_name = tc_graphic_item_type_name(replacement);
    if (type_name != NULL &&
        tc_runtime_type_registry_has_type(type_name) &&
        !tc_runtime_type_registry_link_instance(
            type_name,
            &replacement->runtime_type_link,
            replacement)) {
        tc_log_error(
            "tc_visual_scene_replace_item: failed to link runtime type '%s'",
            type_name);
        mtx_unlock(&scene->mutex);
        deleter(replacement);
        return false;
    }

    replacement->deleter = deleter;
    replacement->scene = scene;
    replacement->handle = previous->handle;
    replacement->parent = previous->parent;
    replacement->first_child = previous->first_child;
    replacement->next_sibling = previous->next_sibling;
    replacement->previous_sibling = previous->previous_sibling;
    replacement->local_transform = previous->local_transform;
    replacement->visible = previous->visible;
    replacement->enabled = previous->enabled;
    replacement->opacity = previous->opacity;
    replacement->z_order = previous->z_order;
    replacement->stable_order = previous->stable_order;
    replacement->revision = ++scene->revision;
    replacement->topology_revision = previous->topology_revision;
    replacement->dirty_flags =
        previous->dirty_flags |
        TC_GRAPHIC_ITEM_DIRTY_VISUAL |
        TC_GRAPHIC_ITEM_DIRTY_INTERACTION;
    slot->item = replacement;

    previous->scene = NULL;
    previous->handle = tc_graphic_item_handle_invalid();
    previous->parent = tc_graphic_item_handle_invalid();
    previous->first_child = tc_graphic_item_handle_invalid();
    previous->next_sibling = tc_graphic_item_handle_invalid();
    previous->previous_sibling = tc_graphic_item_handle_invalid();
    previous->deleter = NULL;
    previous->dirty_flags = TC_GRAPHIC_ITEM_DIRTY_ALL;
    mtx_unlock(&scene->mutex);
    run_delete_record(previous_record);
    return true;
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
    tc_graphic_item* item = item_locked(scene, local_handle(handle));
    *out_view = (tc_graphic_item_view){
        .item = item,
        .parent = item->parent,
        .first_child = item->first_child,
        .next_sibling = item->next_sibling,
        .previous_sibling = item->previous_sibling,
    };
    mtx_unlock(&scene->mutex);
    return true;
}

bool tc_visual_scene_get_item_state(
    tc_visual_scene* scene,
    tc_graphic_item_handle handle,
    tc_graphic_item_state* out_state)
{
    if (out_state == NULL) {
        tc_log_error("tc_visual_scene_get_item_state: out_state is NULL");
        return false;
    }
    if (!lock_scene(scene)) return false;
    if (!valid_locked(scene, handle, "tc_visual_scene_get_item_state")) {
        mtx_unlock(&scene->mutex);
        return false;
    }
    const tc_graphic_item* item =
        item_locked(scene, local_handle(handle));
    *out_state = (tc_graphic_item_state){
        .local_transform = item->local_transform,
        .visible = item->visible,
        .enabled = item->enabled,
        .opacity = item->opacity,
        .z_order = item->z_order,
    };
    mtx_unlock(&scene->mutex);
    return true;
}

bool tc_visual_scene_set_item_state(
    tc_visual_scene* scene,
    tc_graphic_item_handle handle,
    const tc_graphic_item_state* state)
{
    if (state == NULL) {
        tc_log_error("tc_visual_scene_set_item_state: state is NULL");
        return false;
    }
    if (!tc_affine2f_is_finite(state->local_transform) ||
        !isfinite(state->opacity) ||
        state->opacity < 0.0f ||
        state->opacity > 1.0f) {
        tc_log_error("tc_visual_scene_set_item_state: invalid state");
        return false;
    }
    if (!lock_scene(scene)) return false;
    if (!valid_locked(scene, handle, "tc_visual_scene_set_item_state")) {
        mtx_unlock(&scene->mutex);
        return false;
    }
    tc_graphic_item* item = item_locked(scene, local_handle(handle));
    item->local_transform = state->local_transform;
    item->visible = state->visible;
    item->enabled = state->enabled;
    item->opacity = state->opacity;
    item->z_order = state->z_order;
    item->revision = ++scene->revision;
    item->dirty_flags |=
        TC_GRAPHIC_ITEM_DIRTY_TRANSFORM |
        TC_GRAPHIC_ITEM_DIRTY_VISUAL |
        TC_GRAPHIC_ITEM_DIRTY_INTERACTION;
    mtx_unlock(&scene->mutex);
    return true;
}

bool tc_visual_scene_mark_item_dirty(
    tc_visual_scene* scene,
    tc_graphic_item_handle handle,
    uint32_t dirty_flags)
{
    if ((dirty_flags & ~TC_GRAPHIC_ITEM_DIRTY_ALL) != 0) {
        tc_log_error(
            "tc_visual_scene_mark_item_dirty: unsupported dirty flags 0x%x",
            dirty_flags);
        return false;
    }
    if (!lock_scene(scene)) return false;
    if (!valid_locked(scene, handle, "tc_visual_scene_mark_item_dirty")) {
        mtx_unlock(&scene->mutex);
        return false;
    }
    tc_graphic_item* item = item_locked(scene, local_handle(handle));
    item->dirty_flags |= dirty_flags;
    item->revision = ++scene->revision;
    mtx_unlock(&scene->mutex);
    return true;
}

bool tc_visual_scene_restore_item_metadata(
    tc_visual_scene* scene,
    tc_graphic_item_handle handle,
    uint64_t stable_order,
    uint64_t revision,
    uint64_t topology_revision)
{
    if (stable_order == 0) {
        tc_log_error(
            "tc_visual_scene_restore_item_metadata: zero stable order rejected");
        return false;
    }
    if (!lock_scene(scene)) return false;
    if (!valid_locked(
            scene, handle,
            "tc_visual_scene_restore_item_metadata")) {
        mtx_unlock(&scene->mutex);
        return false;
    }
    tc_graphic_item* item = item_locked(scene, local_handle(handle));
    item->stable_order = stable_order;
    item->revision = revision;
    item->topology_revision = topology_revision;
    if (scene->next_stable_order <= stable_order) {
        scene->next_stable_order = stable_order + 1;
    }
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
        tc_graphic_item* ancestor = item_locked(scene, cursor);
        cursor = tc_graphic_item_handle_is_invalid(ancestor->parent)
            ? TC_HANDLE_INVALID
            : local_handle(ancestor->parent);
    }
    unlink_locked(scene, item_local);
    append_child_locked(scene, local_handle(new_parent), item_local);
    tc_graphic_item* moved = item_locked(scene, item_local);
    moved->revision = ++scene->revision;
    moved->topology_revision = moved->revision;
    moved->dirty_flags |= TC_GRAPHIC_ITEM_DIRTY_TOPOLOGY;
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
    tc_graphic_item* detached = item_locked(scene, local_handle(item));
    detached->revision = ++scene->revision;
    detached->topology_revision = detached->revision;
    detached->dirty_flags |= TC_GRAPHIC_ITEM_DIRTY_TOPOLOGY;
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
    tc_graphic_item* stored = item_locked(scene, local);
    if (!tc_graphic_item_handle_is_invalid(stored->first_child)) {
        tc_log_error("tc_visual_scene_destroy_leaf: item has children");
        mtx_unlock(&scene->mutex);
        return false;
    }
    unlink_locked(scene, local);
    const tc_delete_record record = prepare_delete_locked(scene, local);
    ++scene->revision;
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
        tc_graphic_item* item = item_locked(scene, entry.handle);
        if (entry.expanded) {
            order[order_size++] = entry.handle;
            continue;
        }
        stack[stack_size++] = (tc_walk_entry){entry.handle, true};
        tc_graphic_item_handle child_handle = item->first_child;
        while (!tc_graphic_item_handle_is_invalid(child_handle)) {
            const tc_handle child = local_handle(child_handle);
            stack[stack_size++] = (tc_walk_entry){child, false};
            child_handle = item_locked(scene, child)->next_sibling;
        }
    }

    unlink_locked(scene, local_handle(root));
    for (size_t i = 0; i < order_size; ++i) {
        records[i] = prepare_delete_locked(scene, order[i]);
    }
    ++scene->revision;
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
        tc_handle handle = {index, scene->items.generations[index]};
        const tc_delete_record record =
            prepare_delete_locked(scene, handle);
        ++scene->revision;
        mtx_unlock(&scene->mutex);
        run_delete_record(record);
        if (!lock_scene(scene)) return;
    }
    scene->clearing = false;
    mtx_unlock(&scene->mutex);
}
