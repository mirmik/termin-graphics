#include "termin_visual_scene/tc_visual_item3d.h"

#include <stdlib.h>
#include <string.h>

#include <tcbase/tc_log.h>

#include "tc_visual_scene3d_internal.h"

void tc_visual_item3d_init_unowned(tc_visual_item3d* item,
                                   const tc_visual_item3d_vtable* vtable,
                                   tc_language native_language,
                                   void* body) {
    if (item == NULL) {
        tc_log_error("tc_visual_item3d_init_unowned called with null item");
        return;
    }
    memset(item, 0, sizeof(*item));
    item->vtable = vtable;
    item->handle = tc_visual_item3d_handle_invalid();
    item->native_language = native_language;
    item->body = body;
    item->local_transform = tc_affine3d_identity();
    item->visible = true;
    item->enabled = true;
    tc_runtime_type_instance_link_init(&item->runtime_type_link);
}

bool tc_visual_item3d_is_attached(const tc_visual_item3d* item) {
    return item != NULL && item->scene != NULL && !tc_visual_item3d_handle_is_invalid(item->handle);
}

const char* tc_visual_item3d_type_name(const tc_visual_item3d* item) {
    if (item == NULL)
        return NULL;
    if (item->declared_type_name != NULL && item->declared_type_name[0] != '\0') {
        return item->declared_type_name;
    }
    return item->vtable != NULL ? item->vtable->type_name : NULL;
}

void tc_visual_item3d_set_declared_type_name(tc_visual_item3d* item, const char* type_name) {
    if (item != NULL)
        item->declared_type_name = type_name;
}

static bool is_ancestor(const tc_visual_item3d* candidate, const tc_visual_item3d* item) {
    const tc_visual_item3d* cursor = item;
    while (cursor != NULL) {
        if (cursor == candidate)
            return true;
        cursor = cursor->parent;
    }
    return false;
}

static bool reserve_children(tc_visual_item3d* item, size_t required) {
    if (required <= item->child_capacity)
        return true;
    size_t capacity = item->child_capacity != 0 ? item->child_capacity : 4;
    while (capacity < required) {
        if (capacity > SIZE_MAX / 2) {
            tc_log_error("visual item3d child capacity overflow");
            return false;
        }
        capacity *= 2;
    }
    tc_visual_item3d** replacement = (tc_visual_item3d**)realloc(item->children, capacity * sizeof(tc_visual_item3d*));
    if (replacement == NULL) {
        tc_log_error("failed to allocate visual item3d children");
        return false;
    }
    item->children = replacement;
    item->child_capacity = capacity;
    return true;
}

bool tc_visual_item3d_insert_child(tc_visual_item3d* parent, size_t index, tc_visual_item3d* child) {
    if (parent == NULL || child == NULL) {
        tc_log_error("cannot insert null visual item3d");
        return false;
    }
    if (parent == child || is_ancestor(child, parent)) {
        tc_log_error("visual item3d tree cycle rejected");
        return false;
    }
    if (parent->scene == NULL || child->scene != parent->scene) {
        tc_log_error("visual item3d tree requires one owning scene");
        return false;
    }
    if (index > parent->child_count) {
        tc_log_error("visual item3d child index is out of range");
        return false;
    }
    if (child->parent == parent) {
        tc_log_error("visual item3d is already a child of this parent");
        return false;
    }
    if (!reserve_children(parent, parent->child_count + 1)) {
        return false;
    }
    if (child->parent != NULL && !tc_visual_item3d_detach(child)) {
        return false;
    }
    memmove(&parent->children[index + 1],
            &parent->children[index],
            (parent->child_count - index) * sizeof(tc_visual_item3d*));
    parent->children[index] = child;
    parent->child_count += 1;
    child->parent = parent;
    tc_visual_scene3d_touch_order(parent->scene);
    return true;
}

bool tc_visual_item3d_append_child(tc_visual_item3d* parent, tc_visual_item3d* child) {
    return parent != NULL && tc_visual_item3d_insert_child(parent, parent->child_count, child);
}

bool tc_visual_item3d_remove_child(tc_visual_item3d* parent, tc_visual_item3d* child) {
    if (parent == NULL || child == NULL || child->parent != parent) {
        return false;
    }
    size_t index = 0;
    while (index < parent->child_count && parent->children[index] != child) {
        ++index;
    }
    if (index == parent->child_count)
        return false;
    memmove(&parent->children[index],
            &parent->children[index + 1],
            (parent->child_count - index - 1) * sizeof(tc_visual_item3d*));
    parent->child_count -= 1;
    child->parent = NULL;
    tc_visual_scene3d_touch_order(parent->scene);
    return true;
}

bool tc_visual_item3d_detach(tc_visual_item3d* item) {
    return item != NULL && (item->parent == NULL || tc_visual_item3d_remove_child(item->parent, item));
}

tc_visual_item3d* tc_visual_item3d_parent(tc_visual_item3d* item) {
    return item != NULL ? item->parent : NULL;
}

const tc_visual_item3d* tc_visual_item3d_parent_const(const tc_visual_item3d* item) {
    return item != NULL ? item->parent : NULL;
}

size_t tc_visual_item3d_child_count(const tc_visual_item3d* item) {
    return item != NULL ? item->child_count : 0;
}

tc_visual_item3d* tc_visual_item3d_child_at(tc_visual_item3d* item, size_t index) {
    return item != NULL && index < item->child_count ? item->children[index] : NULL;
}

const tc_visual_item3d* tc_visual_item3d_child_at_const(const tc_visual_item3d* item, size_t index) {
    return item != NULL && index < item->child_count ? item->children[index] : NULL;
}

static tc_visual_item3d* resolve(tc_visual_scene3d_handle scene, tc_visual_item3d_handle h) {
    return tc_visual_scene3d_resolve_item(scene, h);
}

bool tc_visual_item3d_get_local_transform(tc_visual_scene3d_handle scene, tc_visual_item3d_handle h, tc_affine3d* out) {
    tc_visual_item3d* item = resolve(scene, h);
    if (item == NULL || out == NULL)
        return false;
    *out = item->local_transform;
    return true;
}
bool tc_visual_item3d_set_local_transform(tc_visual_scene3d_handle scene,
                                          tc_visual_item3d_handle h,
                                          tc_affine3d value) {
    tc_visual_item3d* item = resolve(scene, h);
    if (item == NULL || !tc_affine3d_is_finite(value)) {
        tc_log_error("tc_visual_item3d: local transform must be finite");
        return false;
    }
    item->local_transform = value;
    return true;
}
#define TC_ITEM3D_BOOL_PROPERTY(name)                                                                                  \
    bool tc_visual_item3d_get_##name(tc_visual_scene3d_handle scene, tc_visual_item3d_handle h, bool* out) {           \
        tc_visual_item3d* i = resolve(scene, h);                                                                       \
        if (i == NULL || out == NULL)                                                                                  \
            return false;                                                                                              \
        *out = i->name;                                                                                                \
        return true;                                                                                                   \
    }                                                                                                                  \
    bool tc_visual_item3d_set_##name(tc_visual_scene3d_handle scene, tc_visual_item3d_handle h, bool value) {          \
        tc_visual_item3d* i = resolve(scene, h);                                                                       \
        if (i == NULL)                                                                                                 \
            return false;                                                                                              \
        i->name = value;                                                                                               \
        return true;                                                                                                   \
    }
TC_ITEM3D_BOOL_PROPERTY(visible)
TC_ITEM3D_BOOL_PROPERTY(enabled)
#undef TC_ITEM3D_BOOL_PROPERTY

bool tc_visual_item3d_local_bounds_in_scene(tc_visual_scene3d_handle scene,
                                            tc_visual_item3d_handle h,
                                            tc_visual_bounds3d* out) {
    tc_visual_item3d* item = resolve(scene, h);
    if (item == NULL || out == NULL || item->vtable == NULL || item->vtable->local_bounds == NULL)
        return false;
    tc_visual_bounds3d bounds;
    if (!item->vtable->local_bounds(item, &bounds))
        return false;
    if (!isfinite(bounds.min.x) || !isfinite(bounds.min.y) || !isfinite(bounds.min.z) || !isfinite(bounds.max.x) ||
        !isfinite(bounds.max.y) || !isfinite(bounds.max.z) || bounds.min.x > bounds.max.x ||
        bounds.min.y > bounds.max.y || bounds.min.z > bounds.max.z) {
        tc_log_error("tc_visual_item3d: local_bounds returned invalid bounds");
        return false;
    }
    *out = bounds;
    return true;
}

bool tc_visual_item3d_is_valid(tc_visual_scene3d_handle scene, tc_visual_item3d_handle h) {
    return resolve(scene, h) != NULL;
}
const char* tc_visual_item3d_type_name_in_scene(tc_visual_scene3d_handle scene, tc_visual_item3d_handle h) {
    tc_visual_item3d* i = resolve(scene, h);
    return i != NULL ? tc_visual_item3d_type_name(i) : NULL;
}
tc_visual_item3d_handle tc_visual_item3d_parent_in_scene(tc_visual_scene3d_handle scene, tc_visual_item3d_handle h) {
    tc_visual_item3d* i = resolve(scene, h);
    return i != NULL && i->parent != NULL ? i->parent->handle : tc_visual_item3d_handle_invalid();
}
size_t tc_visual_item3d_child_count_in_scene(tc_visual_scene3d_handle scene, tc_visual_item3d_handle h) {
    tc_visual_item3d* i = resolve(scene, h);
    return i != NULL ? i->child_count : 0;
}
tc_visual_item3d_handle
tc_visual_item3d_child_at_in_scene(tc_visual_scene3d_handle scene, tc_visual_item3d_handle h, size_t index) {
    tc_visual_item3d* i = resolve(scene, h);
    return i != NULL && index < i->child_count ? i->children[index]->handle : tc_visual_item3d_handle_invalid();
}
bool tc_visual_item3d_set_parent_in_scene(tc_visual_scene3d_handle scene,
                                          tc_visual_item3d_handle h,
                                          tc_visual_item3d_handle parent,
                                          size_t index) {
    tc_visual_item3d* i = resolve(scene, h);
    if (i == NULL)
        return false;
    if (tc_visual_item3d_handle_is_invalid(parent))
        return tc_visual_item3d_detach(i);
    tc_visual_item3d* p = resolve(scene, parent);
    if (p == NULL || index > p->child_count)
        return false;
    if (i->parent == p) {
        if (!tc_visual_item3d_detach(i))
            return false;
        if (index > p->child_count)
            index = p->child_count;
    }
    return tc_visual_item3d_insert_child(p, index, i);
}
