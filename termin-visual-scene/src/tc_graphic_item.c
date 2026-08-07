#include "termin_visual_scene/tc_graphic_item.h"

#include <stdlib.h>
#include <string.h>

#include <tcbase/tc_log.h>

#include "tc_visual_scene_internal.h"

void tc_graphic_item_init_unowned(
    tc_graphic_item* item,
    const tc_graphic_item_vtable* vtable,
    tc_language native_language,
    void* body)
{
    if (item == NULL) {
        tc_log_error(
            "tc_graphic_item_init_unowned called with null item");
        return;
    }
    memset(item, 0, sizeof(*item));
    item->vtable = vtable;
    item->handle = tc_graphic_item_handle_invalid();
    item->native_language = native_language;
    item->body = body;
    item->local_transform = tc_affine2f_identity();
    item->visible = true;
    item->enabled = true;
    item->opacity = 1.0f;
    tc_runtime_type_instance_link_init(
        &item->runtime_type_link);
}

bool tc_graphic_item_is_attached(
    const tc_graphic_item* item)
{
    return item != NULL &&
        item->scene != NULL &&
        !tc_graphic_item_handle_is_invalid(item->handle);
}

const char* tc_graphic_item_type_name(
    const tc_graphic_item* item)
{
    if (item == NULL) return NULL;
    if (item->declared_type_name != NULL &&
        item->declared_type_name[0] != '\0') {
        return item->declared_type_name;
    }
    return item->vtable != NULL
        ? item->vtable->type_name
        : NULL;
}

void tc_graphic_item_set_declared_type_name(
    tc_graphic_item* item,
    const char* type_name)
{
    if (item != NULL) item->declared_type_name = type_name;
}

static bool is_ancestor(
    const tc_graphic_item* candidate,
    const tc_graphic_item* item)
{
    const tc_graphic_item* cursor = item;
    while (cursor != NULL) {
        if (cursor == candidate) return true;
        cursor = cursor->parent;
    }
    return false;
}

static bool reserve_children(
    tc_graphic_item* item,
    size_t required)
{
    if (required <= item->child_capacity) return true;
    size_t capacity =
        item->child_capacity != 0 ? item->child_capacity : 4;
    while (capacity < required) {
        if (capacity > SIZE_MAX / 2) {
            tc_log_error(
                "graphic item child capacity overflow");
            return false;
        }
        capacity *= 2;
    }
    tc_graphic_item** replacement =
        (tc_graphic_item**)realloc(
            item->children,
            capacity * sizeof(tc_graphic_item*));
    if (replacement == NULL) {
        tc_log_error(
            "failed to allocate graphic item children");
        return false;
    }
    item->children = replacement;
    item->child_capacity = capacity;
    return true;
}

bool tc_graphic_item_insert_child(
    tc_graphic_item* parent,
    size_t index,
    tc_graphic_item* child)
{
    if (parent == NULL || child == NULL) {
        tc_log_error(
            "cannot insert null graphic item");
        return false;
    }
    if (parent == child || is_ancestor(child, parent)) {
        tc_log_error(
            "graphic item tree cycle rejected");
        return false;
    }
    if (parent->scene == NULL ||
        child->scene != parent->scene) {
        tc_log_error(
            "graphic item tree requires one owning scene");
        return false;
    }
    if (index > parent->child_count) {
        tc_log_error(
            "graphic item child index is out of range");
        return false;
    }
    if (child->parent == parent) {
        tc_log_error(
            "graphic item is already a child of this parent");
        return false;
    }
    if (!reserve_children(
            parent, parent->child_count + 1)) {
        return false;
    }
    if (child->parent != NULL &&
        !tc_graphic_item_detach(child)) {
        return false;
    }
    memmove(
        &parent->children[index + 1],
        &parent->children[index],
        (parent->child_count - index) *
            sizeof(tc_graphic_item*));
    parent->children[index] = child;
    parent->child_count += 1;
    child->parent = parent;
    tc_visual_scene_touch_order(parent->scene);
    return true;
}

bool tc_graphic_item_append_child(
    tc_graphic_item* parent,
    tc_graphic_item* child)
{
    return parent != NULL &&
        tc_graphic_item_insert_child(
            parent, parent->child_count, child);
}

bool tc_graphic_item_remove_child(
    tc_graphic_item* parent,
    tc_graphic_item* child)
{
    if (parent == NULL || child == NULL ||
        child->parent != parent) {
        return false;
    }
    size_t index = 0;
    while (index < parent->child_count &&
           parent->children[index] != child) {
        ++index;
    }
    if (index == parent->child_count) return false;
    memmove(
        &parent->children[index],
        &parent->children[index + 1],
        (parent->child_count - index - 1) *
            sizeof(tc_graphic_item*));
    parent->child_count -= 1;
    child->parent = NULL;
    tc_visual_scene_touch_order(parent->scene);
    return true;
}

bool tc_graphic_item_detach(tc_graphic_item* item) {
    return item != NULL &&
        (item->parent == NULL ||
         tc_graphic_item_remove_child(
             item->parent, item));
}

tc_graphic_item* tc_graphic_item_parent(
    tc_graphic_item* item)
{
    return item != NULL ? item->parent : NULL;
}

const tc_graphic_item* tc_graphic_item_parent_const(
    const tc_graphic_item* item)
{
    return item != NULL ? item->parent : NULL;
}

size_t tc_graphic_item_child_count(
    const tc_graphic_item* item)
{
    return item != NULL ? item->child_count : 0;
}

tc_graphic_item* tc_graphic_item_child_at(
    tc_graphic_item* item,
    size_t index)
{
    return item != NULL && index < item->child_count
        ? item->children[index]
        : NULL;
}

const tc_graphic_item* tc_graphic_item_child_at_const(
    const tc_graphic_item* item,
    size_t index)
{
    return item != NULL && index < item->child_count
        ? item->children[index]
        : NULL;
}
