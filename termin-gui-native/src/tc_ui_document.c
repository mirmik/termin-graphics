#include <termin/gui_native/tc_ui_document.h>

#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include <tcbase/tc_log.h>

typedef struct tc_widget_slot {
    tc_widget* widget;
    uint32_t generation;
    bool destroying;
} tc_widget_slot;

typedef struct tc_ui_overlay_entry {
    tc_widget_handle handle;
    uint32_t flags;
} tc_ui_overlay_entry;

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

    tc_ui_overlay_entry* overlays;
    size_t overlay_count;
    size_t overlay_capacity;

    tc_widget_handle hovered_widget;
    tc_widget_handle pointer_capture;
    tc_widget_handle pressed_widget;
    tc_widget_handle focused_widget;
    tc_ui_pointer_event last_pointer_event;
    bool has_pointer_event;
    size_t live_count;

    tc_ui_text_measure_fn measure_text;
    void* text_measurer_user_data;
    bool missing_text_measurer_logged;
    bool text_measure_failure_logged;
};

static bool change_focus(tc_ui_document* document, tc_widget_handle next);
static bool widget_effectively_interactive(const tc_widget* widget);
static void update_hover(
    tc_ui_document* document,
    tc_widget_handle next,
    const tc_ui_pointer_event* source
);

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

static bool valid_text_metric(float value) {
    return isfinite(value) && value >= 0.0f;
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

static size_t find_overlay_index(
    const tc_ui_document* document,
    tc_widget_handle handle
) {
    size_t index;
    if (!document) {
        return SIZE_MAX;
    }
    for (index = 0; index < document->overlay_count; ++index) {
        if (same_handle(document->overlays[index].handle, handle)) {
            return index;
        }
    }
    return SIZE_MAX;
}

static void remove_overlay_at(tc_ui_document* document, size_t index) {
    if (!document || index >= document->overlay_count) {
        return;
    }
    if (index + 1 < document->overlay_count) {
        memmove(
            &document->overlays[index],
            &document->overlays[index + 1],
            (document->overlay_count - index - 1) * sizeof(*document->overlays)
        );
    }
    document->overlay_count -= 1;
}

static void remove_overlay_references(tc_ui_document* document, tc_widget_handle handle) {
    size_t index;
    if (!document) {
        return;
    }
    for (index = document->overlay_count; index > 0; --index) {
        if (same_handle(document->overlays[index - 1].handle, handle)) {
            remove_overlay_at(document, index - 1);
        }
    }
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
    if (same_handle(document->pressed_widget, handle)) {
        document->pressed_widget = tc_widget_handle_invalid();
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
    if (!widget || !widget_effectively_interactive(widget) ||
        !widget->vtable || !widget->vtable->pointer_event) {
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
    if (!widget || !widget_effectively_interactive(widget) ||
        !widget->vtable || !widget->vtable->key_event) {
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
    if (!widget || !widget_effectively_interactive(widget) ||
        !widget->vtable || !widget->vtable->text_event) {
        return TC_UI_EVENT_IGNORED;
    }
    return widget->vtable->text_event(widget, document, event);
}

static void dispatch_focus_event_to_widget(
    tc_ui_document* document,
    tc_widget_handle handle,
    bool focused
) {
    tc_widget* widget = tc_ui_document_resolve_widget(document, handle);
    if (widget && widget->vtable && widget->vtable->focus_event) {
        widget->vtable->focus_event(widget, document, focused);
    }
}

static bool widget_effectively_interactive(const tc_widget* widget) {
    const tc_widget* current = widget;
    while (current) {
        if (!tc_widget_is_visible(current) || !tc_widget_is_enabled(current)) {
            return false;
        }
        current = current->parent;
    }
    return true;
}

static bool widget_is_descendant_of(const tc_widget* widget, const tc_widget* ancestor) {
    const tc_widget* current = widget;
    while (current) {
        if (current == ancestor) {
            return true;
        }
        current = current->parent;
    }
    return false;
}

static tc_widget_handle nearest_interactive_ancestor(
    tc_ui_document* document,
    tc_widget_handle handle
) {
    tc_widget* widget = tc_ui_document_resolve_widget(document, handle);
    while (widget && !widget_effectively_interactive(widget)) {
        widget = widget->parent;
    }
    return widget ? widget->handle : tc_widget_handle_invalid();
}

static bool snapshot_route(
    tc_ui_document* document,
    tc_widget_handle target,
    tc_widget_handle** out_route,
    size_t* out_count
) {
    tc_widget_handle* route = NULL;
    size_t count = 0;
    size_t capacity = 0;
    tc_widget* widget = tc_ui_document_resolve_widget(document, target);

    while (widget) {
        if (count > document->live_count) {
            tc_log_error("[termin-gui-native] cycle detected while snapshotting event route");
            free(route);
            return false;
        }
        if (!reserve_array((void**)&route, sizeof(*route), &capacity, count + 1)) {
            free(route);
            return false;
        }
        route[count++] = widget->handle;
        if (widget->parent && widget->parent->document != document) {
            tc_log_error("[termin-gui-native] event route crosses document boundary");
            free(route);
            return false;
        }
        widget = widget->parent;
    }
    *out_route = route;
    *out_count = count;
    return true;
}

static tc_ui_event_result dispatch_pointer_route(
    tc_ui_document* document,
    tc_widget_handle target,
    const tc_ui_pointer_event* event,
    tc_widget_handle* out_handler
) {
    tc_widget_handle* route = NULL;
    size_t count = 0;
    size_t index;
    tc_ui_event_result result = TC_UI_EVENT_IGNORED;

    if (out_handler) {
        *out_handler = tc_widget_handle_invalid();
    }
    if (!snapshot_route(document, target, &route, &count)) {
        return TC_UI_EVENT_IGNORED;
    }
    for (index = 0; index < count; ++index) {
        if (dispatch_pointer_event_to_widget(document, route[index], event) == TC_UI_EVENT_HANDLED) {
            result = TC_UI_EVENT_HANDLED;
            if (out_handler) {
                *out_handler = route[index];
            }
            break;
        }
    }
    free(route);
    return result;
}

static tc_ui_event_result dispatch_key_route(
    tc_ui_document* document,
    tc_widget_handle target,
    const tc_ui_key_event* event
) {
    tc_widget_handle* route = NULL;
    size_t count = 0;
    size_t index;
    tc_ui_event_result result = TC_UI_EVENT_IGNORED;

    if (!snapshot_route(document, target, &route, &count)) {
        return result;
    }
    for (index = 0; index < count; ++index) {
        if (dispatch_key_event_to_widget(document, route[index], event) == TC_UI_EVENT_HANDLED) {
            result = TC_UI_EVENT_HANDLED;
            break;
        }
    }
    free(route);
    return result;
}

static tc_ui_event_result dispatch_text_route(
    tc_ui_document* document,
    tc_widget_handle target,
    const tc_ui_text_event* event
) {
    tc_widget_handle* route = NULL;
    size_t count = 0;
    size_t index;
    tc_ui_event_result result = TC_UI_EVENT_IGNORED;

    if (!snapshot_route(document, target, &route, &count)) {
        return result;
    }
    for (index = 0; index < count; ++index) {
        if (dispatch_text_event_to_widget(document, route[index], event) == TC_UI_EVENT_HANDLED) {
            result = TC_UI_EVENT_HANDLED;
            break;
        }
    }
    free(route);
    return result;
}

typedef struct tc_ui_hit_resolution {
    tc_widget_handle target;
    tc_widget_handle blocker;
    bool blocked;
    bool dismissed;
} tc_ui_hit_resolution;

static tc_widget_handle hit_test_entry(
    tc_ui_document* document,
    tc_widget_handle entry,
    float x,
    float y,
    const char* kind
) {
    tc_widget* widget = tc_ui_document_resolve_widget(document, entry);
    tc_widget_handle hit;
    if (!widget || !tc_widget_is_visible(widget) ||
        !widget->vtable || !widget->vtable->hit_test) {
        return tc_widget_handle_invalid();
    }
    hit = widget->vtable->hit_test(widget, document, x, y);
    if (tc_widget_handle_is_invalid(hit)) {
        return hit;
    }
    {
        tc_widget* hit_widget = tc_ui_document_resolve_widget(document, hit);
        if (!hit_widget || !widget_is_descendant_of(hit_widget, widget)) {
            tc_log_error(
                "[termin-gui-native] %s hit-test returned a foreign or stale handle",
                kind
            );
            return tc_widget_handle_invalid();
        }
    }
    return nearest_interactive_ancestor(document, hit);
}

static tc_ui_hit_resolution resolve_document_hit(
    tc_ui_document* document,
    float x,
    float y,
    bool dismiss_outside
) {
    tc_ui_hit_resolution result = {
        tc_widget_handle_invalid(),
        tc_widget_handle_invalid(),
        false,
        false
    };
    tc_ui_overlay_entry* overlays = NULL;
    size_t overlay_count;
    size_t index;
    if (!document) {
        return result;
    }
    overlay_count = document->overlay_count;
    if (overlay_count > 0) {
        overlays = (tc_ui_overlay_entry*)malloc(overlay_count * sizeof(*overlays));
        if (!overlays) {
            tc_log_error("[termin-gui-native] failed to snapshot overlays for hit-test");
            for (index = overlay_count; index > 0; --index) {
                if ((document->overlays[index - 1].flags & TC_UI_OVERLAY_MODAL) != 0) {
                    result.blocked = true;
                    result.blocker = document->overlays[index - 1].handle;
                    return result;
                }
            }
            return result;
        }
        memcpy(overlays, document->overlays, overlay_count * sizeof(*overlays));
    }
    for (index = overlay_count; index > 0; --index) {
        const tc_ui_overlay_entry overlay = overlays[index - 1];
        tc_widget* overlay_widget = tc_ui_document_resolve_widget(document, overlay.handle);
        if (!overlay_widget || !tc_widget_is_visible(overlay_widget)) {
            continue;
        }
        if ((overlay.flags & (TC_UI_OVERLAY_POINTER_TRANSPARENT | TC_UI_OVERLAY_TOOLTIP)) == 0) {
            result.target = hit_test_entry(document, overlay.handle, x, y, "overlay");
            if (!tc_widget_handle_is_invalid(result.target)) {
                free(overlays);
                return result;
            }
        }
        overlay_widget = tc_ui_document_resolve_widget(document, overlay.handle);
        if (!overlay_widget || find_overlay_index(document, overlay.handle) == SIZE_MAX) {
            continue;
        }
        if (dismiss_outside && (overlay.flags & TC_UI_OVERLAY_DISMISS_ON_OUTSIDE) != 0) {
            tc_ui_document_dismiss_overlay(
                document,
                overlay.handle,
                TC_UI_OVERLAY_DISMISS_OUTSIDE
            );
            result.dismissed = true;
            free(overlays);
            return result;
        }
        if ((overlay.flags & TC_UI_OVERLAY_MODAL) != 0) {
            result.blocked = true;
            result.blocker = overlay.handle;
            free(overlays);
            return result;
        }
    }
    free(overlays);
    for (index = document->root_count; index > 0; --index) {
        tc_widget_handle root = document->roots[index - 1];
        result.target = hit_test_entry(document, root, x, y, "root");
        if (!tc_widget_handle_is_invalid(result.target)) {
            return result;
        }
    }
    return result;
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
    if (same_handle(document->focused_widget, handle)) {
        document->focused_widget = tc_widget_handle_invalid();
        if (widget->vtable && widget->vtable->focus_event) {
            widget->vtable->focus_event(widget, document, false);
        }
    }
    if (widget->vtable && widget->vtable->on_destroy) {
        widget->vtable->on_destroy(widget, document);
    }
    remove_root_references(document, handle);
    remove_overlay_references(document, handle);
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

static bool handle_is_in_subtree(
    tc_ui_document* document,
    tc_widget_handle handle,
    const tc_widget* root
) {
    tc_widget* widget = tc_ui_document_resolve_widget(document, handle);
    while (widget) {
        if (widget == root) {
            return true;
        }
        widget = widget->parent;
    }
    return false;
}

static void invalidate_subtree_interaction_state(tc_widget* root) {
    tc_ui_document* document;
    bool clear_hover;
    bool clear_capture;
    bool clear_pressed;
    bool clear_focus;
    if (!root || !(document = root->document)) {
        return;
    }
    clear_hover = handle_is_in_subtree(document, document->hovered_widget, root);
    clear_capture = handle_is_in_subtree(document, document->pointer_capture, root);
    clear_pressed = handle_is_in_subtree(document, document->pressed_widget, root);
    clear_focus = handle_is_in_subtree(document, document->focused_widget, root);
    if (clear_hover) {
        if (document->has_pointer_event) {
            update_hover(document, tc_widget_handle_invalid(), &document->last_pointer_event);
        } else {
            document->hovered_widget = tc_widget_handle_invalid();
        }
    }
    if (clear_capture) {
        document->pointer_capture = tc_widget_handle_invalid();
    }
    if (clear_pressed) {
        document->pressed_widget = tc_widget_handle_invalid();
    }
    if (clear_focus) {
        change_focus(document, tc_widget_handle_invalid());
    }
}

void tc_widget_set_focusable(tc_widget* widget, bool focusable) {
    set_widget_flag(widget, TC_WIDGET_FOCUSABLE, focusable);
    if (widget && !focusable && widget->document &&
        same_handle(widget->document->focused_widget, widget->handle)) {
        change_focus(widget->document, tc_widget_handle_invalid());
    }
}

bool tc_widget_is_focusable(const tc_widget* widget) {
    return widget && (widget->flags & TC_WIDGET_FOCUSABLE) != 0;
}

void tc_widget_set_visible(tc_widget* widget, bool visible) {
    tc_ui_document* document = widget ? widget->document : NULL;
    tc_widget_handle handle = widget ? widget->handle : tc_widget_handle_invalid();
    bool changed = widget && tc_widget_is_visible(widget) != visible;
    set_widget_flag(widget, TC_WIDGET_VISIBLE, visible);
    if (changed && !visible) {
        invalidate_subtree_interaction_state(widget);
    }
    if (changed) {
        tc_widget* live_widget = document
            ? tc_ui_document_resolve_widget(document, handle)
            : widget;
        if (live_widget) {
            tc_widget_mark_dirty(
                live_widget,
                TC_WIDGET_DIRTY_LAYOUT | TC_WIDGET_DIRTY_PAINT | TC_WIDGET_DIRTY_STATE
            );
        }
    }
}

bool tc_widget_is_visible(const tc_widget* widget) {
    return widget && (widget->flags & TC_WIDGET_VISIBLE) != 0;
}

void tc_widget_set_enabled(tc_widget* widget, bool enabled) {
    tc_ui_document* document = widget ? widget->document : NULL;
    tc_widget_handle handle = widget ? widget->handle : tc_widget_handle_invalid();
    bool changed = widget && tc_widget_is_enabled(widget) != enabled;
    set_widget_flag(widget, TC_WIDGET_ENABLED, enabled);
    if (changed && !enabled) {
        invalidate_subtree_interaction_state(widget);
    }
    if (changed) {
        tc_widget* live_widget = document
            ? tc_ui_document_resolve_widget(document, handle)
            : widget;
        if (live_widget) {
            tc_widget_mark_dirty(live_widget, TC_WIDGET_DIRTY_PAINT | TC_WIDGET_DIRTY_STATE);
        }
    }
}

bool tc_widget_is_enabled(const tc_widget* widget) {
    return widget && (widget->flags & TC_WIDGET_ENABLED) != 0;
}

void tc_widget_set_mouse_transparent(tc_widget* widget, bool mouse_transparent) {
    tc_ui_document* document = widget ? widget->document : NULL;
    tc_widget_handle handle = widget ? widget->handle : tc_widget_handle_invalid();
    bool changed = widget && tc_widget_is_mouse_transparent(widget) != mouse_transparent;
    set_widget_flag(widget, TC_WIDGET_MOUSE_TRANSPARENT, mouse_transparent);
    if (changed && mouse_transparent && widget && widget->document &&
        same_handle(widget->document->hovered_widget, widget->handle)) {
        if (widget->document->has_pointer_event) {
            update_hover(
                widget->document,
                tc_widget_handle_invalid(),
                &widget->document->last_pointer_event
            );
        } else {
            widget->document->hovered_widget = tc_widget_handle_invalid();
        }
    }
    if (changed) {
        tc_widget* live_widget = document
            ? tc_ui_document_resolve_widget(document, handle)
            : widget;
        if (live_widget) {
            tc_widget_mark_dirty(live_widget, TC_WIDGET_DIRTY_STATE);
        }
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
    if (find_overlay_index(child->document, child->handle) != SIZE_MAX) {
        tc_log_error("[termin-gui-native] cannot parent a widget while it is an active overlay");
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
    document->pressed_widget = tc_widget_handle_invalid();
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
    free(document->overlays);
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

void tc_ui_document_set_text_measurer(
    tc_ui_document* document,
    tc_ui_text_measure_fn measure,
    void* user_data
) {
    if (!document) {
        tc_log_error("[termin-gui-native] cannot configure text measurement on null document");
        return;
    }
    document->measure_text = measure;
    document->text_measurer_user_data = measure ? user_data : NULL;
    document->missing_text_measurer_logged = false;
    document->text_measure_failure_logged = false;
}

bool tc_ui_document_measure_text(
    tc_ui_document* document,
    const char* text_utf8,
    size_t text_byte_length,
    float font_size,
    tc_ui_text_metrics* out_metrics
) {
    if (!document || !out_metrics || (!text_utf8 && text_byte_length > 0) || font_size <= 0.0f) {
        tc_log_error("[termin-gui-native] invalid text measurement request");
        return false;
    }
    memset(out_metrics, 0, sizeof(*out_metrics));
    if (!document->measure_text) {
        if (!document->missing_text_measurer_logged) {
            tc_log_error("[termin-gui-native] UI document has no text measurement service");
            document->missing_text_measurer_logged = true;
        }
        return false;
    }
    if (!document->measure_text(
            document->text_measurer_user_data,
            text_utf8 ? text_utf8 : "",
            text_byte_length,
            font_size,
            out_metrics) ||
        !valid_text_metric(out_metrics->width) ||
        !valid_text_metric(out_metrics->height) ||
        !valid_text_metric(out_metrics->ascent) ||
        !valid_text_metric(out_metrics->descent) ||
        !valid_text_metric(out_metrics->line_height)) {
        if (!document->text_measure_failure_logged) {
            tc_log_error("[termin-gui-native] text measurement service failed");
            document->text_measure_failure_logged = true;
        }
        memset(out_metrics, 0, sizeof(*out_metrics));
        return false;
    }
    return true;
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
    if (find_overlay_index(document, handle) != SIZE_MAX) {
        tc_log_error("[termin-gui-native] cannot add an active overlay as a root");
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

void tc_ui_document_paint(tc_ui_document* document, tc_ui_paint_context* context) {
    tc_ui_overlay_entry* overlays = NULL;
    size_t count;
    size_t index;
    if (!document) {
        tc_log_error("[termin-gui-native] cannot paint null document");
        return;
    }
    tc_ui_document_paint_roots(document, context);
    count = document->overlay_count;
    if (count > 0) {
        overlays = (tc_ui_overlay_entry*)malloc(count * sizeof(*overlays));
        if (!overlays) {
            tc_log_error("[termin-gui-native] failed to snapshot overlays for paint");
            return;
        }
        memcpy(overlays, document->overlays, count * sizeof(*overlays));
    }
    for (index = 0; index < count; ++index) {
        tc_widget* widget = tc_ui_document_resolve_widget(document, overlays[index].handle);
        if (widget && tc_widget_is_visible(widget) && widget->vtable && widget->vtable->paint) {
            widget->vtable->paint(widget, document, context);
        }
    }
    free(overlays);
}

static bool handle_is_root(const tc_ui_document* document, tc_widget_handle handle) {
    size_t index;
    if (!document) {
        return false;
    }
    for (index = 0; index < document->root_count; ++index) {
        if (same_handle(document->roots[index], handle)) {
            return true;
        }
    }
    return false;
}

static void invalidate_interaction_outside_subtree(tc_widget* root) {
    tc_ui_document* document;
    bool clear_hover;
    bool clear_capture;
    bool clear_pressed;
    bool clear_focus;
    if (!root || !(document = root->document)) {
        return;
    }
    clear_hover = !tc_widget_handle_is_invalid(document->hovered_widget) &&
        !handle_is_in_subtree(document, document->hovered_widget, root);
    clear_capture = !tc_widget_handle_is_invalid(document->pointer_capture) &&
        !handle_is_in_subtree(document, document->pointer_capture, root);
    clear_pressed = !tc_widget_handle_is_invalid(document->pressed_widget) &&
        !handle_is_in_subtree(document, document->pressed_widget, root);
    clear_focus = !tc_widget_handle_is_invalid(document->focused_widget) &&
        !handle_is_in_subtree(document, document->focused_widget, root);
    if (clear_hover) {
        if (document->has_pointer_event) {
            update_hover(document, tc_widget_handle_invalid(), &document->last_pointer_event);
        } else {
            document->hovered_widget = tc_widget_handle_invalid();
        }
    }
    if (clear_capture) {
        document->pointer_capture = tc_widget_handle_invalid();
    }
    if (clear_pressed) {
        document->pressed_widget = tc_widget_handle_invalid();
    }
    if (clear_focus) {
        change_focus(document, tc_widget_handle_invalid());
    }
}

bool tc_ui_document_show_overlay(
    tc_ui_document* document,
    tc_widget_handle handle,
    uint32_t flags
) {
    const uint32_t known_flags = TC_UI_OVERLAY_MODAL |
        TC_UI_OVERLAY_DISMISS_ON_OUTSIDE |
        TC_UI_OVERLAY_POINTER_TRANSPARENT |
        TC_UI_OVERLAY_TOOLTIP;
    tc_widget* widget = tc_ui_document_resolve_widget(document, handle);
    size_t existing;
    if (!widget || widget->parent || handle_is_root(document, handle)) {
        tc_log_error("[termin-gui-native] overlay must be a live unparented non-root widget");
        return false;
    }
    if ((flags & ~known_flags) != 0) {
        tc_log_error("[termin-gui-native] overlay has unknown flags");
        return false;
    }
    if ((flags & TC_UI_OVERLAY_TOOLTIP) != 0) {
        flags |= TC_UI_OVERLAY_POINTER_TRANSPARENT;
        flags &= ~TC_UI_OVERLAY_DISMISS_ON_OUTSIDE;
    }
    if ((flags & TC_UI_OVERLAY_TOOLTIP) != 0 && (flags & TC_UI_OVERLAY_MODAL) != 0) {
        tc_log_error("[termin-gui-native] tooltip overlay cannot be modal");
        return false;
    }
    existing = find_overlay_index(document, handle);
    if (existing != SIZE_MAX) {
        remove_overlay_at(document, existing);
    }
    if (!reserve_array(
            (void**)&document->overlays,
            sizeof(*document->overlays),
            &document->overlay_capacity,
            document->overlay_count + 1)) {
        return false;
    }
    document->overlays[document->overlay_count++] = (tc_ui_overlay_entry){handle, flags};
    if ((flags & TC_UI_OVERLAY_MODAL) != 0) {
        invalidate_interaction_outside_subtree(widget);
    }
    return tc_ui_document_is_alive(document, handle) &&
        find_overlay_index(document, handle) != SIZE_MAX;
}

bool tc_ui_document_dismiss_overlay(
    tc_ui_document* document,
    tc_widget_handle handle,
    tc_ui_overlay_dismiss_reason reason
) {
    size_t index = find_overlay_index(document, handle);
    tc_widget* widget;
    if (reason < TC_UI_OVERLAY_DISMISS_PROGRAMMATIC ||
        reason > TC_UI_OVERLAY_DISMISS_ESCAPE) {
        tc_log_error("[termin-gui-native] invalid overlay dismissal reason");
        return false;
    }
    if (index == SIZE_MAX) {
        return false;
    }
    remove_overlay_at(document, index);
    widget = tc_ui_document_resolve_widget(document, handle);
    if (widget) {
        invalidate_subtree_interaction_state(widget);
    }
    widget = tc_ui_document_resolve_widget(document, handle);
    if (widget && widget->vtable && widget->vtable->overlay_dismissed) {
        widget->vtable->overlay_dismissed(widget, document, reason);
    }
    return true;
}

size_t tc_ui_document_overlay_count(const tc_ui_document* document) {
    return document ? document->overlay_count : 0;
}

tc_widget_handle tc_ui_document_overlay_at(const tc_ui_document* document, size_t index) {
    return document && index < document->overlay_count
        ? document->overlays[index].handle
        : tc_widget_handle_invalid();
}

uint32_t tc_ui_document_overlay_flags_at(const tc_ui_document* document, size_t index) {
    return document && index < document->overlay_count
        ? document->overlays[index].flags
        : 0;
}

tc_ui_rect tc_ui_tooltip_rect(
    tc_ui_rect viewport,
    tc_ui_point anchor,
    tc_ui_size preferred_size,
    tc_ui_point offset,
    float margin
) {
    tc_ui_rect result;
    float available_width;
    float available_height;
    margin = fmaxf(0.0f, margin);
    available_width = fmaxf(0.0f, viewport.width - margin * 2.0f);
    available_height = fmaxf(0.0f, viewport.height - margin * 2.0f);
    result.width = fminf(fmaxf(0.0f, preferred_size.width), available_width);
    result.height = fminf(fmaxf(0.0f, preferred_size.height), available_height);
    result.x = anchor.x + offset.x;
    result.y = anchor.y + offset.y;
    result.x = fminf(result.x, viewport.x + viewport.width - margin - result.width);
    result.y = fminf(result.y, viewport.y + viewport.height - margin - result.height);
    result.x = fmaxf(result.x, viewport.x + margin);
    result.y = fmaxf(result.y, viewport.y + margin);
    return result;
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

static bool change_focus(tc_ui_document* document, tc_widget_handle next) {
    tc_widget_handle previous;
    if (!document || same_handle(document->focused_widget, next)) {
        return document != NULL;
    }
    previous = document->focused_widget;
    document->focused_widget = next;
    if (!tc_widget_handle_is_invalid(previous)) {
        dispatch_focus_event_to_widget(document, previous, false);
    }
    if (!tc_widget_handle_is_invalid(next) && same_handle(document->focused_widget, next)) {
        dispatch_focus_event_to_widget(document, next, true);
    }
    return true;
}

static void focus_from_pointer_target(tc_ui_document* document, tc_widget_handle target) {
    tc_widget* widget = tc_ui_document_resolve_widget(document, target);
    while (widget) {
        if (widget_effectively_interactive(widget) && tc_widget_is_focusable(widget)) {
            change_focus(document, widget->handle);
            return;
        }
        widget = widget->parent;
    }
    change_focus(document, tc_widget_handle_invalid());
}

static void update_hover(
    tc_ui_document* document,
    tc_widget_handle next,
    const tc_ui_pointer_event* source
) {
    tc_widget_handle previous = document->hovered_widget;
    tc_ui_pointer_event transition = *source;
    if (same_handle(previous, next)) {
        return;
    }
    document->hovered_widget = next;
    if (!tc_widget_handle_is_invalid(previous)) {
        tc_widget* widget = tc_ui_document_resolve_widget(document, previous);
        transition.type = TC_UI_POINTER_LEAVE;
        if (widget && widget->vtable && widget->vtable->pointer_event) {
            widget->vtable->pointer_event(widget, document, &transition);
        }
    }
    if (!tc_widget_handle_is_invalid(next) && same_handle(document->hovered_widget, next)) {
        tc_widget* widget = tc_ui_document_resolve_widget(document, next);
        transition.type = TC_UI_POINTER_ENTER;
        if (widget && widget->vtable && widget->vtable->pointer_event) {
            widget->vtable->pointer_event(widget, document, &transition);
        }
    }
}

static bool collect_focusables_in_tree(
    tc_ui_document* document,
    tc_widget_handle handle,
    bool ancestors_interactive,
    tc_widget_handle** handles,
    size_t* count,
    size_t* capacity
) {
    size_t index;
    tc_widget* widget = tc_ui_document_resolve_widget(document, handle);
    bool interactive;
    if (!widget) {
        return true;
    }
    interactive = ancestors_interactive && tc_widget_is_visible(widget) && tc_widget_is_enabled(widget);
    if (!interactive) {
        return true;
    }
    if (tc_widget_is_focusable(widget)) {
        if (!reserve_array((void**)handles, sizeof(**handles), capacity, *count + 1)) {
            return false;
        }
        (*handles)[(*count)++] = handle;
    }
    for (index = 0; index < widget->child_count; ++index) {
        tc_widget* child = widget->children[index];
        if (child && !collect_focusables_in_tree(
                document,
                child->handle,
                interactive,
                handles,
                count,
                capacity)) {
            return false;
        }
    }
    return true;
}

static size_t top_modal_overlay_index(const tc_ui_document* document) {
    size_t index;
    if (!document) {
        return SIZE_MAX;
    }
    for (index = document->overlay_count; index > 0; --index) {
        const tc_ui_overlay_entry* overlay = &document->overlays[index - 1];
        const tc_widget* widget = tc_ui_document_resolve_widget_const(document, overlay->handle);
        if (widget && tc_widget_is_visible(widget) &&
            (overlay->flags & TC_UI_OVERLAY_MODAL) != 0) {
            return index - 1;
        }
    }
    return SIZE_MAX;
}

static tc_widget_handle top_modal_overlay(const tc_ui_document* document) {
    size_t index = top_modal_overlay_index(document);
    return index == SIZE_MAX
        ? tc_widget_handle_invalid()
        : document->overlays[index].handle;
}

static bool handle_is_in_modal_scope(
    tc_ui_document* document,
    tc_widget_handle handle,
    size_t modal_index
) {
    size_t index;
    tc_widget* widget = tc_ui_document_resolve_widget(document, handle);
    if (!widget || modal_index == SIZE_MAX || modal_index >= document->overlay_count) {
        return false;
    }
    for (index = modal_index; index < document->overlay_count; ++index) {
        tc_widget* overlay = tc_ui_document_resolve_widget(
            document,
            document->overlays[index].handle
        );
        if (overlay && widget_is_descendant_of(widget, overlay)) {
            return true;
        }
    }
    return false;
}

static bool move_focus(tc_ui_document* document, bool reverse) {
    tc_widget_handle* focusables = NULL;
    size_t count = 0;
    size_t capacity = 0;
    size_t root_index;
    size_t overlay_index;
    size_t selected = 0;
    bool found = false;
    size_t modal_index;

    if (!document) {
        tc_log_error("[termin-gui-native] cannot traverse focus in null document");
        return false;
    }
    modal_index = top_modal_overlay_index(document);
    if (modal_index != SIZE_MAX) {
        for (overlay_index = modal_index; overlay_index < document->overlay_count; ++overlay_index) {
            const tc_ui_overlay_entry* overlay = &document->overlays[overlay_index];
            if ((overlay->flags & TC_UI_OVERLAY_TOOLTIP) != 0) {
                continue;
            }
            if (!collect_focusables_in_tree(
                    document,
                    overlay->handle,
                    true,
                    &focusables,
                    &count,
                    &capacity)) {
                free(focusables);
                return false;
            }
        }
    } else {
        for (root_index = 0; root_index < document->root_count; ++root_index) {
            if (!collect_focusables_in_tree(
                    document,
                    document->roots[root_index],
                    true,
                    &focusables,
                    &count,
                    &capacity)) {
                free(focusables);
                return false;
            }
        }
        for (overlay_index = 0; overlay_index < document->overlay_count; ++overlay_index) {
            const tc_ui_overlay_entry* overlay = &document->overlays[overlay_index];
            if ((overlay->flags & TC_UI_OVERLAY_TOOLTIP) != 0) {
                continue;
            }
            if (!collect_focusables_in_tree(
                    document,
                    overlay->handle,
                    true,
                    &focusables,
                    &count,
                    &capacity)) {
                free(focusables);
                return false;
            }
        }
    }
    if (count == 0) {
        free(focusables);
        return false;
    }
    for (selected = 0; selected < count; ++selected) {
        if (same_handle(focusables[selected], document->focused_widget)) {
            found = true;
            break;
        }
    }
    if (!found) {
        selected = reverse ? count - 1 : 0;
    } else if (reverse) {
        selected = selected == 0 ? count - 1 : selected - 1;
    } else {
        selected = (selected + 1) % count;
    }
    {
        tc_widget_handle next = focusables[selected];
        free(focusables);
        return change_focus(document, next);
    }
}

tc_ui_event_result tc_ui_document_dispatch_pointer_event(
    tc_ui_document* document,
    const tc_ui_pointer_event* event
) {
    tc_ui_hit_resolution hit_resolution;
    tc_widget_handle modal_at_start;
    tc_widget_handle hit;
    tc_widget_handle target;
    tc_widget_handle handler = tc_widget_handle_invalid();
    tc_ui_event_result result;
    if (!document || !event) {
        tc_log_error("[termin-gui-native] cannot dispatch pointer event without document/event");
        return TC_UI_EVENT_IGNORED;
    }
    document->last_pointer_event = *event;
    document->has_pointer_event = true;
    modal_at_start = top_modal_overlay(document);
    hit_resolution = resolve_document_hit(
        document,
        event->x,
        event->y,
        event->type == TC_UI_POINTER_DOWN
    );
    hit = hit_resolution.target;
    update_hover(document, hit, event);
    if (event->type == TC_UI_POINTER_DOWN) {
        document->pressed_widget = tc_widget_handle_invalid();
        if (!hit_resolution.blocked) {
            focus_from_pointer_target(document, hit);
        }
    }
    if (hit_resolution.dismissed) {
        return TC_UI_EVENT_HANDLED;
    }
    if (!tc_widget_handle_is_invalid(document->pointer_capture) &&
        !tc_ui_document_is_alive(document, document->pointer_capture)) {
        document->pointer_capture = tc_widget_handle_invalid();
    }
    if (!tc_widget_handle_is_invalid(document->pressed_widget) &&
        !tc_ui_document_is_alive(document, document->pressed_widget)) {
        document->pressed_widget = tc_widget_handle_invalid();
    }

    target = hit;
    if (event->type != TC_UI_POINTER_WHEEL &&
        !tc_widget_handle_is_invalid(document->pointer_capture)) {
        target = document->pointer_capture;
    } else if ((event->type == TC_UI_POINTER_MOVE || event->type == TC_UI_POINTER_UP) &&
               !tc_widget_handle_is_invalid(document->pressed_widget)) {
        target = document->pressed_widget;
    }
    if (event->type == TC_UI_POINTER_UP) {
        document->pressed_widget = tc_widget_handle_invalid();
    }
    if (hit_resolution.blocked) {
        tc_widget* blocker = tc_ui_document_resolve_widget(document, hit_resolution.blocker);
        tc_widget* target_widget = tc_ui_document_resolve_widget(document, target);
        if (!blocker || !target_widget || !widget_is_descendant_of(target_widget, blocker)) {
            return TC_UI_EVENT_HANDLED;
        }
    }
    result = dispatch_pointer_route(document, target, event, &handler);
    if (event->type == TC_UI_POINTER_DOWN && result == TC_UI_EVENT_HANDLED &&
        tc_ui_document_is_alive(document, handler)) {
        tc_widget* handled_widget = tc_ui_document_resolve_widget(document, handler);
        if (widget_effectively_interactive(handled_widget)) {
            document->pressed_widget = handler;
        }
    }
    return result == TC_UI_EVENT_IGNORED && !tc_widget_handle_is_invalid(modal_at_start)
        ? TC_UI_EVENT_HANDLED
        : result;
}

tc_widget_handle tc_ui_document_hit_test(tc_ui_document* document, float x, float y) {
    if (!document) {
        tc_log_error("[termin-gui-native] cannot hit-test roots of null document");
        return tc_widget_handle_invalid();
    }
    return resolve_document_hit(document, x, y, false).target;
}

tc_widget_handle tc_ui_document_hovered_widget(const tc_ui_document* document) {
    return document ? document->hovered_widget : tc_widget_handle_invalid();
}

tc_widget_handle tc_ui_document_pointer_capture(const tc_ui_document* document) {
    return document ? document->pointer_capture : tc_widget_handle_invalid();
}

tc_widget_handle tc_ui_document_pressed_widget(const tc_ui_document* document) {
    return document ? document->pressed_widget : tc_widget_handle_invalid();
}

bool tc_ui_document_set_pointer_capture(tc_ui_document* document, tc_widget_handle handle) {
    tc_widget* widget = tc_ui_document_resolve_widget(document, handle);
    size_t modal_index = top_modal_overlay_index(document);
    if (!widget || !widget_effectively_interactive(widget)) {
        tc_log_error("[termin-gui-native] cannot capture pointer for invalid or disabled widget");
        return false;
    }
    if (modal_index != SIZE_MAX && !handle_is_in_modal_scope(document, handle, modal_index)) {
        tc_log_error("[termin-gui-native] cannot capture pointer outside active modal scope");
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
    size_t modal_index = top_modal_overlay_index(document);
    if (!widget || !widget_effectively_interactive(widget) || !tc_widget_is_focusable(widget)) {
        tc_log_error("[termin-gui-native] cannot focus invalid, disabled or non-focusable widget");
        return false;
    }
    if (modal_index != SIZE_MAX && !handle_is_in_modal_scope(document, handle, modal_index)) {
        tc_log_error("[termin-gui-native] cannot focus widget outside active modal scope");
        return false;
    }
    return change_focus(document, handle);
}

bool tc_ui_document_clear_focus(tc_ui_document* document, tc_widget_handle handle) {
    if (!document || !same_handle(document->focused_widget, handle)) {
        return false;
    }
    return change_focus(document, tc_widget_handle_invalid());
}

bool tc_ui_document_focus_next(tc_ui_document* document) {
    return move_focus(document, false);
}

bool tc_ui_document_focus_previous(tc_ui_document* document) {
    return move_focus(document, true);
}

tc_ui_event_result tc_ui_document_dispatch_key_event(
    tc_ui_document* document,
    const tc_ui_key_event* event
) {
    tc_ui_event_result result = TC_UI_EVENT_IGNORED;
    tc_widget_handle modal;
    tc_widget_handle target;
    size_t modal_index;
    if (!document || !event) {
        return TC_UI_EVENT_IGNORED;
    }
    if (event->type == TC_UI_KEY_DOWN && event->key == TC_UI_KEY_ESCAPE &&
        document->overlay_count > 0) {
        tc_widget_handle top = document->overlays[document->overlay_count - 1].handle;
        return tc_ui_document_dismiss_overlay(
            document,
            top,
            TC_UI_OVERLAY_DISMISS_ESCAPE
        ) ? TC_UI_EVENT_HANDLED : TC_UI_EVENT_IGNORED;
    }
    if (!tc_widget_handle_is_invalid(document->focused_widget) &&
        !tc_ui_document_is_alive(document, document->focused_widget)) {
        document->focused_widget = tc_widget_handle_invalid();
    }
    modal = top_modal_overlay(document);
    modal_index = top_modal_overlay_index(document);
    target = document->focused_widget;
    if (!tc_widget_handle_is_invalid(modal)) {
        if (!handle_is_in_modal_scope(document, target, modal_index)) {
            target = modal;
        }
    }
    if (!tc_widget_handle_is_invalid(target)) {
        result = dispatch_key_route(document, target, event);
    }
    if (result == TC_UI_EVENT_IGNORED && event->type == TC_UI_KEY_DOWN &&
        event->key == TC_UI_KEY_TAB) {
        if (move_focus(document, (event->modifiers & TC_UI_MOD_SHIFT) != 0)) {
            return TC_UI_EVENT_HANDLED;
        }
        return tc_widget_handle_is_invalid(modal)
            ? TC_UI_EVENT_IGNORED
            : TC_UI_EVENT_HANDLED;
    }
    if (result == TC_UI_EVENT_IGNORED && !tc_widget_handle_is_invalid(modal)) {
        return TC_UI_EVENT_HANDLED;
    }
    return result;
}

tc_ui_event_result tc_ui_document_dispatch_text_event(
    tc_ui_document* document,
    const tc_ui_text_event* event
) {
    tc_widget_handle modal;
    tc_widget_handle target;
    size_t modal_index;
    tc_ui_event_result result;
    if (!document || !event) {
        return TC_UI_EVENT_IGNORED;
    }
    if (!tc_widget_handle_is_invalid(document->focused_widget) &&
        !tc_ui_document_is_alive(document, document->focused_widget)) {
        document->focused_widget = tc_widget_handle_invalid();
    }
    modal = top_modal_overlay(document);
    modal_index = top_modal_overlay_index(document);
    target = document->focused_widget;
    if (!tc_widget_handle_is_invalid(modal)) {
        if (!handle_is_in_modal_scope(document, target, modal_index)) {
            target = modal;
        }
    }
    if (tc_widget_handle_is_invalid(target)) {
        return TC_UI_EVENT_IGNORED;
    }
    result = dispatch_text_route(document, target, event);
    return result == TC_UI_EVENT_IGNORED && !tc_widget_handle_is_invalid(modal)
        ? TC_UI_EVENT_HANDLED
        : result;
}
