#include "tc_ui_document_internal.h"

#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include <tcbase/tc_log.h>

static tc_ui_event_result dispatch_pointer_event_to_widget(
    tc_ui_document* document,
    tc_widget_handle handle,
    const tc_ui_pointer_event* event
) {
    tc_widget* widget = tc_ui_document_resolve_widget(document, handle);
    if (!widget || !tc_ui_internal_widget_effectively_interactive(widget) ||
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
    if (!widget || !tc_ui_internal_widget_effectively_interactive(widget) ||
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
    if (!widget || !tc_ui_internal_widget_effectively_interactive(widget) ||
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

bool tc_ui_internal_widget_effectively_interactive(const tc_widget* widget) {
    const tc_widget* current = widget;
    while (current) {
        if (!tc_widget_is_visible(current) || !tc_widget_is_enabled(current)) {
            return false;
        }
        current = current->parent;
    }
    return true;
}

bool tc_ui_internal_widget_effectively_enabled(const tc_widget* widget) {
    const tc_widget* current = widget;
    while (current) {
        if (!tc_widget_is_enabled(current)) {
            return false;
        }
        current = current->parent;
    }
    return true;
}

bool tc_ui_internal_widget_is_descendant_of(const tc_widget* widget, const tc_widget* ancestor) {
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
    while (widget && !tc_ui_internal_widget_effectively_interactive(widget)) {
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
        if (!tc_ui_internal_reserve_array((void**)&route, sizeof(*route), &capacity, count + 1)) {
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

static bool handle_is_root(const tc_ui_document* document, tc_widget_handle handle);

static tc_widget_handle hit_test_entry(
    tc_ui_document* document,
    tc_widget_handle entry,
    float x,
    float y,
    const char* kind,
    bool allow_root_hit
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
        tc_widget* hit_root = hit_widget;
        while (hit_root && hit_root->parent) {
            hit_root = hit_root->parent;
        }
        const bool allowed_root_hit = allow_root_hit && hit_root &&
            handle_is_root(document, hit_root->handle);
        if (!hit_widget ||
            (!tc_ui_internal_widget_is_descendant_of(hit_widget, widget) &&
             !allowed_root_hit)) {
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
            result.target = hit_test_entry(
                document,
                overlay.handle,
                x,
                y,
                "overlay",
                (overlay.flags & TC_UI_OVERLAY_ALLOW_ROOT_HIT) != 0
            );
            if (!tc_widget_handle_is_invalid(result.target)) {
                free(overlays);
                return result;
            }
        }
        overlay_widget = tc_ui_document_resolve_widget(document, overlay.handle);
        if (!overlay_widget || tc_ui_internal_find_overlay_index(document, overlay.handle) == SIZE_MAX) {
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
        result.target = hit_test_entry(document, root, x, y, "root", false);
        if (!tc_widget_handle_is_invalid(result.target)) {
            return result;
        }
    }
    return result;
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
        if (tc_ui_internal_same_handle(document->roots[index], handle)) {
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
        !tc_ui_internal_handle_is_in_subtree(document, document->hovered_widget, root);
    clear_capture = !tc_widget_handle_is_invalid(document->pointer_capture) &&
        !tc_ui_internal_handle_is_in_subtree(document, document->pointer_capture, root);
    clear_pressed = !tc_widget_handle_is_invalid(document->pressed_widget) &&
        !tc_ui_internal_handle_is_in_subtree(document, document->pressed_widget, root);
    clear_focus = !tc_widget_handle_is_invalid(document->focused_widget) &&
        !tc_ui_internal_handle_is_in_subtree(document, document->focused_widget, root);
    if (clear_hover) {
        if (document->has_pointer_event) {
            tc_ui_internal_update_hover(document, tc_widget_handle_invalid(), &document->last_pointer_event);
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
        tc_ui_internal_change_focus(document, tc_widget_handle_invalid());
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
        TC_UI_OVERLAY_TOOLTIP |
        TC_UI_OVERLAY_ALLOW_ROOT_HIT |
        TC_UI_OVERLAY_BLOCK_ESCAPE;
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
    if ((flags & TC_UI_OVERLAY_ALLOW_ROOT_HIT) != 0 &&
        (flags & TC_UI_OVERLAY_MODAL) != 0) {
        tc_log_error("[termin-gui-native] modal overlay cannot route hits to root widgets");
        return false;
    }
    existing = tc_ui_internal_find_overlay_index(document, handle);
    if (existing != SIZE_MAX) {
        tc_ui_internal_remove_overlay_at(document, existing);
    }
    if (!tc_ui_internal_reserve_array(
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
        tc_ui_internal_find_overlay_index(document, handle) != SIZE_MAX;
}

bool tc_ui_document_dismiss_overlay(
    tc_ui_document* document,
    tc_widget_handle handle,
    tc_ui_overlay_dismiss_reason reason
) {
    size_t index = tc_ui_internal_find_overlay_index(document, handle);
    tc_widget* widget;
    if (reason < TC_UI_OVERLAY_DISMISS_PROGRAMMATIC ||
        reason > TC_UI_OVERLAY_DISMISS_ESCAPE) {
        tc_log_error("[termin-gui-native] invalid overlay dismissal reason");
        return false;
    }
    if (index == SIZE_MAX) {
        return false;
    }
    tc_ui_internal_remove_overlay_at(document, index);
    widget = tc_ui_document_resolve_widget(document, handle);
    if (widget) {
        tc_ui_internal_invalidate_subtree_interaction_state(widget);
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

bool tc_ui_internal_change_focus(tc_ui_document* document, tc_widget_handle next) {
    tc_widget_handle previous;
    if (!document || tc_ui_internal_same_handle(document->focused_widget, next)) {
        return document != NULL;
    }
    previous = document->focused_widget;
    document->focused_widget = next;
    if (!tc_widget_handle_is_invalid(previous)) {
        dispatch_focus_event_to_widget(document, previous, false);
    }
    if (!tc_widget_handle_is_invalid(next) && tc_ui_internal_same_handle(document->focused_widget, next)) {
        dispatch_focus_event_to_widget(document, next, true);
    }
    return true;
}

static void focus_from_pointer_target(tc_ui_document* document, tc_widget_handle target) {
    tc_widget* widget = tc_ui_document_resolve_widget(document, target);
    while (widget) {
        if (tc_ui_internal_widget_effectively_interactive(widget) && tc_widget_is_focusable(widget)) {
            tc_ui_internal_change_focus(document, widget->handle);
            return;
        }
        widget = widget->parent;
    }
    tc_ui_internal_change_focus(document, tc_widget_handle_invalid());
}

void tc_ui_internal_update_hover(
    tc_ui_document* document,
    tc_widget_handle next,
    const tc_ui_pointer_event* source
) {
    tc_widget_handle previous = document->hovered_widget;
    tc_ui_pointer_event transition = *source;
    if (tc_ui_internal_same_handle(previous, next)) {
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
    if (!tc_widget_handle_is_invalid(next) && tc_ui_internal_same_handle(document->hovered_widget, next)) {
        tc_widget* widget = tc_ui_document_resolve_widget(document, next);
        transition.type = TC_UI_POINTER_ENTER;
        if (widget && widget->vtable && widget->vtable->pointer_event) {
            widget->vtable->pointer_event(widget, document, &transition);
        }
    }
    tc_ui_internal_refresh_cursor(document);
}

void tc_ui_internal_refresh_cursor(tc_ui_document* document) {
    tc_ui_cursor_intent next = TC_UI_CURSOR_DEFAULT;
    tc_widget* widget;
    size_t depth = 0;
    if (!document) {
        return;
    }
    widget = tc_ui_document_resolve_widget(document, document->hovered_widget);
    if (widget && !tc_ui_internal_widget_effectively_interactive(widget)) {
        widget = NULL;
    }
    while (widget) {
        if (depth++ >= document->live_count || widget->document != document) {
            tc_log_error("[termin-gui-native] invalid canonical tree while resolving cursor intent");
            next = TC_UI_CURSOR_DEFAULT;
            break;
        }
        if (widget->cursor_intent != TC_UI_CURSOR_INHERIT) {
            if (widget->cursor_intent < TC_UI_CURSOR_DEFAULT ||
                widget->cursor_intent >= TC_UI_CURSOR_INTENT_COUNT) {
                tc_log_error(
                    "[termin-gui-native] invalid cursor intent %d in hovered route",
                    (int)widget->cursor_intent
                );
                next = TC_UI_CURSOR_DEFAULT;
            } else {
                next = widget->cursor_intent;
            }
            break;
        }
        widget = widget->parent;
    }
    if (document->cursor_intent == next) {
        return;
    }
    document->cursor_intent = next;
    if (document->cursor_changed) {
        document->cursor_changed(document->cursor_changed_user_data, next);
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
        if (!tc_ui_internal_reserve_array((void**)handles, sizeof(**handles), capacity, *count + 1)) {
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
        if (overlay && tc_ui_internal_widget_is_descendant_of(widget, overlay)) {
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
        if (tc_ui_internal_same_handle(focusables[selected], document->focused_widget)) {
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
        return tc_ui_internal_change_focus(document, next);
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
    hit_resolution = event->type == TC_UI_POINTER_LEAVE
        ? (tc_ui_hit_resolution){tc_widget_handle_invalid(), tc_widget_handle_invalid(), false, false}
        : resolve_document_hit(
              document,
              event->x,
              event->y,
              event->type == TC_UI_POINTER_DOWN
          );
    hit = hit_resolution.target;
    tc_ui_internal_update_hover(document, hit, event);
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
        if (!blocker || !target_widget || !tc_ui_internal_widget_is_descendant_of(target_widget, blocker)) {
            return TC_UI_EVENT_HANDLED;
        }
    }
    result = dispatch_pointer_route(document, target, event, &handler);
    if (event->type == TC_UI_POINTER_DOWN && result == TC_UI_EVENT_HANDLED &&
        tc_ui_document_is_alive(document, handler)) {
        tc_widget* handled_widget = tc_ui_document_resolve_widget(document, handler);
        if (tc_ui_internal_widget_effectively_interactive(handled_widget)) {
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

tc_ui_cursor_intent tc_ui_document_cursor_intent(const tc_ui_document* document) {
    return document ? document->cursor_intent : TC_UI_CURSOR_DEFAULT;
}

void tc_ui_document_set_cursor_changed_callback(
    tc_ui_document* document,
    tc_ui_cursor_changed_fn callback,
    void* user_data
) {
    if (!document) {
        tc_log_error("[termin-gui-native] cannot set cursor callback on null document");
        return;
    }
    document->cursor_changed = callback;
    document->cursor_changed_user_data = callback ? user_data : NULL;
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
    if (!widget || !tc_ui_internal_widget_effectively_interactive(widget)) {
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
    if (!document || !tc_ui_internal_same_handle(document->pointer_capture, handle)) {
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
    if (!widget || !tc_ui_internal_widget_effectively_interactive(widget) || !tc_widget_is_focusable(widget)) {
        tc_log_error("[termin-gui-native] cannot focus invalid, disabled or non-focusable widget");
        return false;
    }
    if (modal_index != SIZE_MAX && !handle_is_in_modal_scope(document, handle, modal_index)) {
        tc_log_error("[termin-gui-native] cannot focus widget outside active modal scope");
        return false;
    }
    return tc_ui_internal_change_focus(document, handle);
}

bool tc_ui_document_clear_focus(tc_ui_document* document, tc_widget_handle handle) {
    if (!document || !tc_ui_internal_same_handle(document->focused_widget, handle)) {
        return false;
    }
    return tc_ui_internal_change_focus(document, tc_widget_handle_invalid());
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
        const tc_ui_overlay_entry top = document->overlays[document->overlay_count - 1];
        if ((top.flags & TC_UI_OVERLAY_BLOCK_ESCAPE) != 0) {
            return TC_UI_EVENT_HANDLED;
        }
        return tc_ui_document_dismiss_overlay(
            document,
            top.handle,
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
