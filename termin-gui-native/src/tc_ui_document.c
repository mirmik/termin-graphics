#include <termin/gui_native/tc_ui_document.h>

#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include <tcbase/tc_log.h>

typedef struct tc_widget_slot {
    tc_widget* widget;
    uint32_t generation;
    bool destroying;
} tc_widget_slot;

struct tc_ui_document {
    tc_widget_slot* slots;
    size_t slot_count;
    size_t slot_capacity;

    uint32_t* free_slots;
    size_t free_slot_count;
    size_t free_slot_capacity;

    tc_widget_handle* roots;
    size_t root_count;
    size_t root_capacity;

    tc_widget_handle hovered_widget;
    tc_widget_handle pointer_capture;
    tc_widget_handle focused_widget;
    size_t live_count;
};

static bool same_handle(tc_widget_handle lhs, tc_widget_handle rhs) {
    return tc_widget_handle_eq(lhs, rhs);
}

static bool reserve_array(void** data, size_t item_size, size_t* capacity, size_t required) {
    size_t next_capacity;
    void* next_data;

    if (required <= *capacity) {
        return true;
    }
    if (item_size == 0 || required > SIZE_MAX / item_size) {
        tc_log_error("[termin-gui-native] dynamic array size overflow");
        return false;
    }

    next_capacity = *capacity > 0 ? *capacity : 4;
    while (next_capacity < required) {
        if (next_capacity > SIZE_MAX / 2) {
            next_capacity = required;
            break;
        }
        next_capacity *= 2;
    }
    if (next_capacity > SIZE_MAX / item_size) {
        tc_log_error("[termin-gui-native] dynamic array capacity overflow");
        return false;
    }

    next_data = realloc(*data, next_capacity * item_size);
    if (!next_data) {
        tc_log_error("[termin-gui-native] failed to grow dynamic array to %zu items", next_capacity);
        return false;
    }
    *data = next_data;
    *capacity = next_capacity;
    return true;
}

static tc_widget_slot* resolve_slot(tc_ui_document* document, tc_widget_handle handle) {
    tc_widget_slot* slot;
    if (!document || tc_widget_handle_is_invalid(handle) || handle.index >= document->slot_count) {
        return NULL;
    }
    slot = &document->slots[handle.index];
    if (slot->generation != handle.generation || !slot->widget) {
        return NULL;
    }
    return slot;
}

static const tc_widget_slot* resolve_slot_const(
    const tc_ui_document* document,
    tc_widget_handle handle
) {
    const tc_widget_slot* slot;
    if (!document || tc_widget_handle_is_invalid(handle) || handle.index >= document->slot_count) {
        return NULL;
    }
    slot = &document->slots[handle.index];
    if (slot->generation != handle.generation || !slot->widget) {
        return NULL;
    }
    return slot;
}

static bool widget_is_live_pointer(const tc_widget* widget) {
    const tc_widget_slot* slot;
    if (!widget || !widget->document || tc_widget_handle_is_invalid(widget->handle)) {
        return false;
    }
    slot = resolve_slot_const(widget->document, widget->handle);
    return slot && !slot->destroying && slot->widget == widget;
}

static size_t find_child_index(const tc_widget* parent, const tc_widget* child) {
    size_t index;
    if (!parent || !child) {
        return SIZE_MAX;
    }
    for (index = 0; index < parent->child_count; ++index) {
        if (parent->children[index] == child) {
            return index;
        }
    }
    return SIZE_MAX;
}

static void remove_child_at(tc_widget* parent, size_t index) {
    tc_widget* child;
    if (!parent || index >= parent->child_count) {
        return;
    }
    child = parent->children[index];
    if (index + 1 < parent->child_count) {
        memmove(
            &parent->children[index],
            &parent->children[index + 1],
            (parent->child_count - index - 1) * sizeof(tc_widget*)
        );
    }
    parent->child_count -= 1;
    if (child && child->parent == parent) {
        child->parent = NULL;
    }
}

static bool detach_widget_internal(tc_widget* widget) {
    size_t index;
    if (!widget || !widget->parent) {
        return false;
    }
    index = find_child_index(widget->parent, widget);
    if (index == SIZE_MAX) {
        tc_log_error("[termin-gui-native] widget parent link is not mirrored by its parent child list");
        widget->parent = NULL;
        return false;
    }
    remove_child_at(widget->parent, index);
    return true;
}

static void remove_root_references(tc_ui_document* document, tc_widget_handle handle) {
    size_t read_index;
    size_t write_index = 0;
    if (!document) {
        return;
    }
    for (read_index = 0; read_index < document->root_count; ++read_index) {
        if (!same_handle(document->roots[read_index], handle)) {
            document->roots[write_index++] = document->roots[read_index];
        }
    }
    document->root_count = write_index;
}

static void clear_document_state_references(tc_ui_document* document, tc_widget_handle handle) {
    if (!document) {
        return;
    }
    if (same_handle(document->hovered_widget, handle)) {
        document->hovered_widget = tc_widget_handle_invalid();
    }
    if (same_handle(document->pointer_capture, handle)) {
        document->pointer_capture = tc_widget_handle_invalid();
    }
    if (same_handle(document->focused_widget, handle)) {
        document->focused_widget = tc_widget_handle_invalid();
    }
}

static bool append_free_slot(tc_ui_document* document, uint32_t index) {
    if (!reserve_array(
            (void**)&document->free_slots,
            sizeof(uint32_t),
            &document->free_slot_capacity,
            document->free_slot_count + 1)) {
        return false;
    }
    document->free_slots[document->free_slot_count++] = index;
    return true;
}

static tc_ui_event_result dispatch_pointer_event_to_widget(
    tc_ui_document* document,
    tc_widget_handle handle,
    const tc_ui_pointer_event* event
) {
    tc_widget* widget = tc_ui_document_resolve_widget(document, handle);
    if (!widget || !tc_widget_is_enabled(widget) || !widget->vtable || !widget->vtable->pointer_event) {
        return TC_UI_EVENT_IGNORED;
    }
    return widget->vtable->pointer_event(widget, document, event);
}

static tc_ui_event_result dispatch_key_event_to_widget(
    tc_ui_document* document,
    tc_widget_handle handle,
    const tc_ui_key_event* event
) {
    tc_widget* widget = tc_ui_document_resolve_widget(document, handle);
    if (!widget || !tc_widget_is_enabled(widget) || !widget->vtable || !widget->vtable->key_event) {
        return TC_UI_EVENT_IGNORED;
    }
    return widget->vtable->key_event(widget, document, event);
}

static tc_ui_event_result dispatch_text_event_to_widget(
    tc_ui_document* document,
    tc_widget_handle handle,
    const tc_ui_text_event* event
) {
    tc_widget* widget = tc_ui_document_resolve_widget(document, handle);
    if (!widget || !tc_widget_is_enabled(widget) || !widget->vtable || !widget->vtable->text_event) {
        return TC_UI_EVENT_IGNORED;
    }
    return widget->vtable->text_event(widget, document, event);
}

static tc_widget_handle hit_test_document_inner(tc_ui_document* document, float x, float y) {
    size_t index;
    if (!document) {
        return tc_widget_handle_invalid();
    }
    for (index = document->root_count; index > 0; --index) {
        tc_widget_handle root = document->roots[index - 1];
        tc_widget* widget = tc_ui_document_resolve_widget(document, root);
        tc_widget_handle hit;
        if (!widget) {
            tc_log_error(
                "[termin-gui-native] skipping stale root handle index=%u generation=%u during hit-test",
                root.index,
                root.generation
            );
            continue;
        }
        if (!tc_widget_is_visible(widget) || !widget->vtable || !widget->vtable->hit_test) {
            continue;
        }
        hit = widget->vtable->hit_test(widget, document, x, y);
        if (!tc_widget_handle_is_invalid(hit)) {
            return hit;
        }
    }
    return tc_widget_handle_invalid();
}

static bool destroy_widget_inner(tc_ui_document* document, tc_widget_handle handle, bool recursive) {
    tc_widget_slot* slot = resolve_slot(document, handle);
    tc_widget* widget;
    tc_widget_deleter deleter;
    bool ok = true;

    if (!slot) {
        tc_log_error(
            "[termin-gui-native] cannot destroy invalid widget handle index=%u generation=%u",
            handle.index,
            handle.generation
        );
        return false;
    }
    if (slot->destroying) {
        tc_log_error(
            "[termin-gui-native] recursive destroy cycle at widget handle index=%u generation=%u",
            handle.index,
            handle.generation
        );
        return false;
    }

    widget = slot->widget;
    slot->destroying = true;

    if (recursive) {
        while (widget->child_count > 0) {
            tc_widget* child = widget->children[widget->child_count - 1];
            if (!child || child->parent != widget || child->document != document) {
                tc_log_error("[termin-gui-native] invalid canonical child link during recursive destroy");
                remove_child_at(widget, widget->child_count - 1);
                ok = false;
                continue;
            }
            {
                tc_widget_handle child_handle = child->handle;
                if (!destroy_widget_inner(document, child_handle, true)) {
                    tc_widget* remaining = tc_ui_document_resolve_widget(document, child_handle);
                    if (remaining && remaining->parent == widget) {
                        detach_widget_internal(remaining);
                    }
                    ok = false;
                }
            }
        }
    } else {
        while (widget->child_count > 0) {
            remove_child_at(widget, widget->child_count - 1);
        }
    }

    detach_widget_internal(widget);
    if (widget->vtable && widget->vtable->on_destroy) {
        widget->vtable->on_destroy(widget, document);
    }
    remove_root_references(document, handle);
    clear_document_state_references(document, handle);

    deleter = widget->deleter;
    free(widget->children);
    widget->children = NULL;
    widget->child_count = 0;
    widget->child_capacity = 0;
    widget->parent = NULL;
    widget->document = NULL;
    widget->handle = tc_widget_handle_invalid();

    slot->widget = NULL;
    slot->destroying = false;
    slot->generation += 1;
    if (slot->generation == 0) {
        slot->generation = 1;
    }
    if (!append_free_slot(document, handle.index)) {
        ok = false;
    }
    document->live_count -= 1;

    if (deleter) {
        deleter(widget);
    }
    return ok;
}

tc_widget_handle tc_widget_handle_invalid_value(void) {
    return tc_widget_handle_invalid();
}

bool tc_widget_handle_valid_value(tc_widget_handle handle) {
    return !tc_widget_handle_is_invalid(handle);
}

void tc_widget_init(
    tc_widget* widget,
    const tc_widget_vtable* vtable,
    tc_widget_deleter deleter,
    tc_language native_language,
    void* body
) {
    if (!widget) {
        tc_log_error("[termin-gui-native] tc_widget_init called with null widget");
        return;
    }
    memset(widget, 0, sizeof(*widget));
    widget->vtable = vtable;
    widget->deleter = deleter;
    widget->handle = tc_widget_handle_invalid();
    widget->native_language = native_language;
    widget->body = body;
    widget->flags = TC_WIDGET_VISIBLE | TC_WIDGET_ENABLED;
}

static void set_widget_flag(tc_widget* widget, uint32_t flag, bool enabled) {
    if (!widget) {
        tc_log_error("[termin-gui-native] cannot update flags on null widget");
        return;
    }
    if (enabled) {
        widget->flags |= flag;
    } else {
        widget->flags &= ~flag;
    }
}

void tc_widget_set_focusable(tc_widget* widget, bool focusable) {
    set_widget_flag(widget, TC_WIDGET_FOCUSABLE, focusable);
}

bool tc_widget_is_focusable(const tc_widget* widget) {
    return widget && (widget->flags & TC_WIDGET_FOCUSABLE) != 0;
}

void tc_widget_set_visible(tc_widget* widget, bool visible) {
    bool changed = widget && tc_widget_is_visible(widget) != visible;
    set_widget_flag(widget, TC_WIDGET_VISIBLE, visible);
    if (changed) {
        tc_widget_mark_dirty(widget, TC_WIDGET_DIRTY_LAYOUT | TC_WIDGET_DIRTY_PAINT | TC_WIDGET_DIRTY_STATE);
    }
}

bool tc_widget_is_visible(const tc_widget* widget) {
    return widget && (widget->flags & TC_WIDGET_VISIBLE) != 0;
}

void tc_widget_set_enabled(tc_widget* widget, bool enabled) {
    bool changed = widget && tc_widget_is_enabled(widget) != enabled;
    set_widget_flag(widget, TC_WIDGET_ENABLED, enabled);
    if (changed) {
        tc_widget_mark_dirty(widget, TC_WIDGET_DIRTY_PAINT | TC_WIDGET_DIRTY_STATE);
    }
}

bool tc_widget_is_enabled(const tc_widget* widget) {
    return widget && (widget->flags & TC_WIDGET_ENABLED) != 0;
}

void tc_widget_set_mouse_transparent(tc_widget* widget, bool mouse_transparent) {
    bool changed = widget && tc_widget_is_mouse_transparent(widget) != mouse_transparent;
    set_widget_flag(widget, TC_WIDGET_MOUSE_TRANSPARENT, mouse_transparent);
    if (changed) {
        tc_widget_mark_dirty(widget, TC_WIDGET_DIRTY_STATE);
    }
}

bool tc_widget_is_mouse_transparent(const tc_widget* widget) {
    return widget && (widget->flags & TC_WIDGET_MOUSE_TRANSPARENT) != 0;
}

tc_ui_rect tc_widget_bounds(const tc_widget* widget) {
    return widget ? widget->bounds : (tc_ui_rect){0.0f, 0.0f, 0.0f, 0.0f};
}

void tc_widget_set_bounds(tc_widget* widget, tc_ui_rect bounds) {
    if (!widget) {
        tc_log_error("[termin-gui-native] cannot set bounds on null widget");
        return;
    }
    widget->bounds = bounds;
}

tc_ui_size tc_widget_min_size(const tc_widget* widget) {
    return widget ? widget->min_size : (tc_ui_size){0.0f, 0.0f};
}

void tc_widget_set_min_size(tc_widget* widget, tc_ui_size size) {
    if (!widget) {
        tc_log_error("[termin-gui-native] cannot set minimum size on null widget");
        return;
    }
    widget->min_size = size;
    tc_widget_mark_dirty(widget, TC_WIDGET_DIRTY_LAYOUT | TC_WIDGET_DIRTY_PAINT);
}

tc_ui_size tc_widget_preferred_size(const tc_widget* widget) {
    return widget ? widget->preferred_size : (tc_ui_size){0.0f, 0.0f};
}

void tc_widget_set_preferred_size(tc_widget* widget, tc_ui_size size) {
    if (!widget) {
        tc_log_error("[termin-gui-native] cannot set preferred size on null widget");
        return;
    }
    widget->preferred_size = size;
    tc_widget_mark_dirty(widget, TC_WIDGET_DIRTY_LAYOUT | TC_WIDGET_DIRTY_PAINT);
}

tc_ui_size tc_widget_max_size(const tc_widget* widget) {
    return widget ? widget->max_size : (tc_ui_size){0.0f, 0.0f};
}

void tc_widget_set_max_size(tc_widget* widget, tc_ui_size size) {
    if (!widget) {
        tc_log_error("[termin-gui-native] cannot set maximum size on null widget");
        return;
    }
    widget->max_size = size;
    tc_widget_mark_dirty(widget, TC_WIDGET_DIRTY_LAYOUT | TC_WIDGET_DIRTY_PAINT);
}

tc_widget* tc_widget_parent(tc_widget* widget) {
    return widget ? widget->parent : NULL;
}

const tc_widget* tc_widget_parent_const(const tc_widget* widget) {
    return widget ? widget->parent : NULL;
}

size_t tc_widget_child_count(const tc_widget* widget) {
    return widget ? widget->child_count : 0;
}

tc_widget* tc_widget_child_at(tc_widget* widget, size_t index) {
    return widget && index < widget->child_count ? widget->children[index] : NULL;
}

const tc_widget* tc_widget_child_at_const(const tc_widget* widget, size_t index) {
    return widget && index < widget->child_count ? widget->children[index] : NULL;
}

bool tc_widget_insert_child(tc_widget* parent, size_t index, tc_widget* child) {
    tc_widget* ancestor;
    tc_widget* old_parent = child ? child->parent : NULL;
    size_t old_index = SIZE_MAX;
    if (!widget_is_live_pointer(parent) || !widget_is_live_pointer(child)) {
        tc_log_error("[termin-gui-native] cannot attach unadopted or stale widgets");
        return false;
    }
    if (parent->document != child->document) {
        tc_log_error("[termin-gui-native] cannot attach widgets from different documents");
        return false;
    }
    if (parent == child) {
        tc_log_error("[termin-gui-native] cannot attach widget to itself");
        return false;
    }
    for (ancestor = parent; ancestor; ancestor = ancestor->parent) {
        if (ancestor == child) {
            tc_log_error("[termin-gui-native] cannot create a cycle in the widget tree");
            return false;
        }
    }
    if (!reserve_array(
            (void**)&parent->children,
            sizeof(tc_widget*),
            &parent->child_capacity,
            parent->child_count + (child->parent == parent ? 0 : 1))) {
        return false;
    }

    if (child->parent == parent) {
        old_index = find_child_index(parent, child);
        if (old_index == SIZE_MAX) {
            tc_log_error("[termin-gui-native] inconsistent same-parent child link");
            return false;
        }
        remove_child_at(parent, old_index);
    } else if (child->parent) {
        if (!detach_widget_internal(child)) {
            return false;
        }
    }
    remove_root_references(parent->document, child->handle);

    if (index > parent->child_count) {
        index = parent->child_count;
    }
    if (index < parent->child_count) {
        memmove(
            &parent->children[index + 1],
            &parent->children[index],
            (parent->child_count - index) * sizeof(tc_widget*)
        );
    }
    parent->children[index] = child;
    parent->child_count += 1;
    child->parent = parent;
    if (old_parent && old_parent != parent) {
        tc_widget_mark_dirty(old_parent, TC_WIDGET_DIRTY_LAYOUT | TC_WIDGET_DIRTY_PAINT);
    }
    tc_widget_mark_dirty(parent, TC_WIDGET_DIRTY_LAYOUT | TC_WIDGET_DIRTY_PAINT);
    return true;
}

bool tc_widget_append_child(tc_widget* parent, tc_widget* child) {
    return tc_widget_insert_child(parent, SIZE_MAX, child);
}

bool tc_widget_remove_child(tc_widget* parent, tc_widget* child) {
    size_t index;
    if (!widget_is_live_pointer(parent) || !widget_is_live_pointer(child) ||
        parent->document != child->document || child->parent != parent) {
        return false;
    }
    index = find_child_index(parent, child);
    if (index == SIZE_MAX) {
        tc_log_error("[termin-gui-native] cannot remove inconsistent child link");
        return false;
    }
    remove_child_at(parent, index);
    tc_widget_mark_dirty(parent, TC_WIDGET_DIRTY_LAYOUT | TC_WIDGET_DIRTY_PAINT);
    return true;
}

bool tc_widget_detach(tc_widget* widget) {
    tc_widget* parent;
    if (!widget_is_live_pointer(widget) || !widget->parent) {
        return false;
    }
    parent = widget->parent;
    if (!detach_widget_internal(widget)) {
        return false;
    }
    tc_widget_mark_dirty(parent, TC_WIDGET_DIRTY_LAYOUT | TC_WIDGET_DIRTY_PAINT);
    return true;
}

const char* tc_widget_stable_id(const tc_widget* widget) {
    return widget ? widget->stable_id : NULL;
}

const char* tc_widget_name(const tc_widget* widget) {
    return widget ? widget->name : NULL;
}

const char* tc_widget_debug_name(const tc_widget* widget) {
    return widget ? widget->debug_name : NULL;
}

void tc_widget_mark_dirty(tc_widget* widget, uint32_t dirty_flags) {
    if (!widget) {
        tc_log_error("[termin-gui-native] cannot mark null widget dirty");
        return;
    }
    widget->flags |= dirty_flags & TC_WIDGET_DIRTY_MASK;
}

void tc_widget_clear_dirty(tc_widget* widget, uint32_t dirty_flags) {
    if (!widget) {
        tc_log_error("[termin-gui-native] cannot clear dirty flags on null widget");
        return;
    }
    widget->flags &= ~(dirty_flags & TC_WIDGET_DIRTY_MASK);
}

uint32_t tc_widget_dirty_flags(const tc_widget* widget) {
    return widget ? widget->flags & TC_WIDGET_DIRTY_MASK : 0;
}

bool tc_widget_has_dirty_flags(const tc_widget* widget, uint32_t dirty_flags) {
    uint32_t requested = dirty_flags & TC_WIDGET_DIRTY_MASK;
    return requested != 0 && (tc_widget_dirty_flags(widget) & requested) == requested;
}

tc_ui_document* tc_ui_document_create(void) {
    tc_ui_document* document = (tc_ui_document*)calloc(1, sizeof(tc_ui_document));
    if (!document) {
        tc_log_error("[termin-gui-native] failed to allocate UI document");
        return NULL;
    }
    document->hovered_widget = tc_widget_handle_invalid();
    document->pointer_capture = tc_widget_handle_invalid();
    document->focused_widget = tc_widget_handle_invalid();
    return document;
}

void tc_ui_document_destroy(tc_ui_document* document) {
    size_t index;
    if (!document) {
        return;
    }
    while (document->live_count > 0) {
        bool destroyed_one = false;
        for (index = 0; index < document->slot_count; ++index) {
            tc_widget_slot* slot = &document->slots[index];
            if (slot->widget) {
                tc_widget_handle handle = {(uint32_t)index, slot->generation};
                destroy_widget_inner(document, handle, false);
                destroyed_one = true;
                break;
            }
        }
        if (!destroyed_one) {
            tc_log_error("[termin-gui-native] document live count is inconsistent during destroy");
            break;
        }
    }
    free(document->roots);
    free(document->free_slots);
    free(document->slots);
    free(document);
}

tc_widget_handle tc_ui_document_adopt_widget(tc_ui_document* document, tc_widget* widget) {
    uint32_t index;
    tc_widget_slot* slot;
    tc_widget_handle handle;
    if (!document || !widget) {
        tc_log_error("[termin-gui-native] cannot adopt widget without document/widget");
        return tc_widget_handle_invalid();
    }
    if (widget->document || !tc_widget_handle_is_invalid(widget->handle)) {
        tc_log_error("[termin-gui-native] cannot adopt widget that already belongs to a document");
        return tc_widget_handle_invalid();
    }
    if (widget->parent || widget->child_count != 0 || widget->children) {
        tc_log_error("[termin-gui-native] cannot adopt widget with pre-existing tree state");
        return tc_widget_handle_invalid();
    }

    if (document->free_slot_count > 0) {
        index = document->free_slots[--document->free_slot_count];
    } else {
        if (document->slot_count >= UINT32_MAX) {
            tc_log_error("[termin-gui-native] widget slot index overflow");
            return tc_widget_handle_invalid();
        }
        if (!reserve_array(
                (void**)&document->slots,
                sizeof(tc_widget_slot),
                &document->slot_capacity,
                document->slot_count + 1)) {
            return tc_widget_handle_invalid();
        }
        index = (uint32_t)document->slot_count++;
        memset(&document->slots[index], 0, sizeof(tc_widget_slot));
        document->slots[index].generation = 1;
    }

    slot = &document->slots[index];
    slot->widget = widget;
    slot->destroying = false;
    handle = (tc_widget_handle){index, slot->generation};
    widget->document = document;
    widget->handle = handle;
    document->live_count += 1;
    return handle;
}

bool tc_ui_document_is_alive(const tc_ui_document* document, tc_widget_handle handle) {
    const tc_widget_slot* slot = resolve_slot_const(document, handle);
    return slot && !slot->destroying;
}

tc_widget* tc_ui_document_resolve_widget(tc_ui_document* document, tc_widget_handle handle) {
    tc_widget_slot* slot = resolve_slot(document, handle);
    return slot && !slot->destroying ? slot->widget : NULL;
}

const tc_widget* tc_ui_document_resolve_widget_const(
    const tc_ui_document* document,
    tc_widget_handle handle
) {
    const tc_widget_slot* slot = resolve_slot_const(document, handle);
    return slot && !slot->destroying ? slot->widget : NULL;
}

bool tc_ui_document_destroy_widget(tc_ui_document* document, tc_widget_handle handle) {
    return destroy_widget_inner(document, handle, false);
}

bool tc_ui_document_destroy_widget_recursive(tc_ui_document* document, tc_widget_handle handle) {
    return destroy_widget_inner(document, handle, true);
}

size_t tc_ui_document_live_widget_count(const tc_ui_document* document) {
    return document ? document->live_count : 0;
}

bool tc_ui_document_add_root(tc_ui_document* document, tc_widget_handle handle) {
    tc_widget* widget = tc_ui_document_resolve_widget(document, handle);
    size_t index;
    if (!widget) {
        tc_log_error("[termin-gui-native] cannot add invalid root handle");
        return false;
    }
    if (widget->parent) {
        tc_log_error("[termin-gui-native] cannot add parented widget as a root");
        return false;
    }
    for (index = 0; index < document->root_count; ++index) {
        if (same_handle(document->roots[index], handle)) {
            return true;
        }
    }
    if (!reserve_array(
            (void**)&document->roots,
            sizeof(tc_widget_handle),
            &document->root_capacity,
            document->root_count + 1)) {
        return false;
    }
    document->roots[document->root_count++] = handle;
    return true;
}

bool tc_ui_document_remove_root(tc_ui_document* document, tc_widget_handle handle) {
    size_t before;
    if (!document) {
        return false;
    }
    before = document->root_count;
    remove_root_references(document, handle);
    return document->root_count != before;
}

size_t tc_ui_document_root_count(const tc_ui_document* document) {
    return document ? document->root_count : 0;
}

tc_widget_handle tc_ui_document_root_at(const tc_ui_document* document, size_t index) {
    return document && index < document->root_count
        ? document->roots[index]
        : tc_widget_handle_invalid();
}

void tc_ui_document_paint_roots(tc_ui_document* document, tc_ui_paint_context* context) {
    size_t index;
    if (!document) {
        tc_log_error("[termin-gui-native] cannot paint roots of null document");
        return;
    }
    for (index = 0; index < document->root_count; ++index) {
        tc_widget* widget = tc_ui_document_resolve_widget(document, document->roots[index]);
        if (widget && tc_widget_is_visible(widget) && widget->vtable && widget->vtable->paint) {
            widget->vtable->paint(widget, document, context);
        }
    }
}

void tc_ui_document_layout_roots(tc_ui_document* document, tc_ui_rect rect) {
    size_t index;
    if (!document) {
        tc_log_error("[termin-gui-native] cannot layout roots of null document");
        return;
    }
    for (index = 0; index < document->root_count; ++index) {
        tc_widget* widget = tc_ui_document_resolve_widget(document, document->roots[index]);
        if (widget && tc_widget_is_visible(widget) && widget->vtable && widget->vtable->layout) {
            widget->vtable->layout(widget, document, rect);
        }
    }
}

tc_ui_event_result tc_ui_document_dispatch_pointer_event(
    tc_ui_document* document,
    const tc_ui_pointer_event* event
) {
    size_t index;
    if (!document || !event) {
        tc_log_error("[termin-gui-native] cannot dispatch pointer event without document/event");
        return TC_UI_EVENT_IGNORED;
    }
    document->hovered_widget = hit_test_document_inner(document, event->x, event->y);
    if (event->type == TC_UI_POINTER_DOWN) {
        tc_widget* focused = tc_ui_document_resolve_widget(document, document->hovered_widget);
        document->focused_widget = focused && tc_widget_is_enabled(focused) && tc_widget_is_focusable(focused)
            ? focused->handle
            : tc_widget_handle_invalid();
    }
    if (!tc_widget_handle_is_invalid(document->pointer_capture)) {
        if (tc_ui_document_is_alive(document, document->pointer_capture)) {
            return dispatch_pointer_event_to_widget(document, document->pointer_capture, event);
        }
        document->pointer_capture = tc_widget_handle_invalid();
    }
    for (index = document->root_count; index > 0; --index) {
        tc_widget* widget = tc_ui_document_resolve_widget(document, document->roots[index - 1]);
        if (widget && tc_widget_is_visible(widget) && tc_widget_is_enabled(widget) &&
            widget->vtable && widget->vtable->pointer_event &&
            widget->vtable->pointer_event(widget, document, event) == TC_UI_EVENT_HANDLED) {
            return TC_UI_EVENT_HANDLED;
        }
    }
    return TC_UI_EVENT_IGNORED;
}

tc_widget_handle tc_ui_document_hit_test(tc_ui_document* document, float x, float y) {
    if (!document) {
        tc_log_error("[termin-gui-native] cannot hit-test roots of null document");
        return tc_widget_handle_invalid();
    }
    return hit_test_document_inner(document, x, y);
}

tc_widget_handle tc_ui_document_hovered_widget(const tc_ui_document* document) {
    return document ? document->hovered_widget : tc_widget_handle_invalid();
}

tc_widget_handle tc_ui_document_pointer_capture(const tc_ui_document* document) {
    return document ? document->pointer_capture : tc_widget_handle_invalid();
}

bool tc_ui_document_set_pointer_capture(tc_ui_document* document, tc_widget_handle handle) {
    tc_widget* widget = tc_ui_document_resolve_widget(document, handle);
    if (!widget || !tc_widget_is_enabled(widget)) {
        tc_log_error("[termin-gui-native] cannot capture pointer for invalid or disabled widget");
        return false;
    }
    document->pointer_capture = handle;
    return true;
}

bool tc_ui_document_release_pointer_capture(tc_ui_document* document, tc_widget_handle handle) {
    if (!document || !same_handle(document->pointer_capture, handle)) {
        return false;
    }
    document->pointer_capture = tc_widget_handle_invalid();
    return true;
}

tc_widget_handle tc_ui_document_focused_widget(const tc_ui_document* document) {
    return document ? document->focused_widget : tc_widget_handle_invalid();
}

bool tc_ui_document_set_focus(tc_ui_document* document, tc_widget_handle handle) {
    tc_widget* widget = tc_ui_document_resolve_widget(document, handle);
    if (!widget || !tc_widget_is_enabled(widget) || !tc_widget_is_focusable(widget)) {
        tc_log_error("[termin-gui-native] cannot focus invalid, disabled or non-focusable widget");
        return false;
    }
    document->focused_widget = handle;
    return true;
}

bool tc_ui_document_clear_focus(tc_ui_document* document, tc_widget_handle handle) {
    if (!document || !same_handle(document->focused_widget, handle)) {
        return false;
    }
    document->focused_widget = tc_widget_handle_invalid();
    return true;
}

tc_ui_event_result tc_ui_document_dispatch_key_event(
    tc_ui_document* document,
    const tc_ui_key_event* event
) {
    if (!document || !event || tc_widget_handle_is_invalid(document->focused_widget)) {
        return TC_UI_EVENT_IGNORED;
    }
    if (!tc_ui_document_is_alive(document, document->focused_widget)) {
        document->focused_widget = tc_widget_handle_invalid();
        return TC_UI_EVENT_IGNORED;
    }
    return dispatch_key_event_to_widget(document, document->focused_widget, event);
}

tc_ui_event_result tc_ui_document_dispatch_text_event(
    tc_ui_document* document,
    const tc_ui_text_event* event
) {
    if (!document || !event || tc_widget_handle_is_invalid(document->focused_widget)) {
        return TC_UI_EVENT_IGNORED;
    }
    if (!tc_ui_document_is_alive(document, document->focused_widget)) {
        document->focused_widget = tc_widget_handle_invalid();
        return TC_UI_EVENT_IGNORED;
    }
    return dispatch_text_event_to_widget(document, document->focused_widget, event);
}
