#include "tc_ui_document_internal.h"

#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include <tcbase/tc_log.h>

tc_widget_handle tc_widget_handle_invalid_value(void) {
    return tc_widget_handle_invalid();
}

bool tc_widget_handle_valid_value(tc_widget_handle handle) {
    return !tc_widget_handle_is_invalid(handle);
}

static bool replace_owned_string(const char* value, const char** view, char** owned) {
    char* replacement = NULL;
    if (value && value[0]) {
        const size_t length = strlen(value);
        replacement = (char*)malloc(length + 1);
        if (!replacement) {
            tc_log_error("[termin-gui-native] failed to allocate widget metadata string");
            return false;
        }
        memcpy(replacement, value, length + 1);
    }
    free(*owned);
    *owned = replacement;
    *view = replacement;
    return true;
}

void tc_ui_internal_release_widget_metadata(tc_widget* widget) {
    if (!widget) {
        return;
    }
    free(widget->owned_stable_id);
    free(widget->owned_name);
    free(widget->owned_debug_name);
    widget->owned_stable_id = NULL;
    widget->owned_name = NULL;
    widget->owned_debug_name = NULL;
    widget->stable_id = NULL;
    widget->name = NULL;
    widget->debug_name = NULL;
}

void tc_widget_init_unowned(tc_widget* widget,
                            const tc_widget_vtable* vtable,
                            tc_language native_language,
                            void* body) {
    if (!widget) {
        tc_log_error("[termin-gui-native] tc_widget_init_unowned called with null widget");
        return;
    }
    memset(widget, 0, sizeof(*widget));
    widget->vtable = vtable;
    widget->document = tc_ui_document_handle_invalid();
    widget->handle = tc_widget_handle_invalid();
    widget->native_language = native_language;
    widget->ownership_policy = TC_WIDGET_BORROWED;
    widget->body = body;
    tc_runtime_type_instance_link_init(&widget->runtime_type_link);
    widget->flags = TC_WIDGET_VISIBLE | TC_WIDGET_ENABLED | TC_WIDGET_TREE_PARTICIPATING;
    widget->cursor_intent = TC_UI_CURSOR_INHERIT;
    widget->style_role = TC_UI_STYLE_GENERIC;
    widget->layout_spec = tc_ui_widget_layout_spec_default();
}

static void mark_style_subtree_dirty(tc_widget* widget) {
    size_t index;
    if (!widget) {
        return;
    }
    tc_widget_mark_dirty(widget, TC_WIDGET_DIRTY_LAYOUT | TC_WIDGET_DIRTY_PAINT | TC_WIDGET_DIRTY_STATE);
    for (index = 0; index < widget->child_count; ++index) {
        mark_style_subtree_dirty(widget->children[index]);
    }
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

bool tc_ui_internal_handle_is_in_subtree(tc_ui_document* document, tc_widget_handle handle, const tc_widget* root) {
    tc_widget* widget = tc_ui_document_resolve_widget(document->handle, handle);
    while (widget) {
        if (widget == root) {
            return true;
        }
        widget = widget->parent;
    }
    return false;
}

bool tc_ui_internal_cancel_pointer_state(tc_ui_document* document,
                                         bool clear_capture,
                                         bool clear_pressed,
                                         tc_ui_pointer_cancel_reason reason) {
    tc_widget_handle targets[2];
    tc_ui_document_handle document_handle;
    tc_ui_pointer_event event;
    size_t target_count = 0;
    size_t index;
    bool notified = false;
    if (!document) {
        return false;
    }
    document_handle = document->handle;
    if (clear_capture && !tc_widget_handle_is_invalid(document->pointer_capture)) {
        targets[target_count++] = document->pointer_capture;
        document->pointer_capture = tc_widget_handle_invalid();
    }
    if (clear_pressed && !tc_widget_handle_is_invalid(document->pressed_widget)) {
        if (target_count == 0 || !tc_ui_internal_same_handle(targets[0], document->pressed_widget)) {
            targets[target_count++] = document->pressed_widget;
        }
        document->pressed_widget = tc_widget_handle_invalid();
    }
    if (target_count == 0) {
        return false;
    }
    event = document->has_pointer_event ? document->last_pointer_event : (tc_ui_pointer_event){0};
    event.type = TC_UI_POINTER_CANCEL;
    event.cancel_reason = reason;
    for (index = 0; index < target_count; ++index) {
        tc_widget* widget = tc_ui_document_resolve_widget(document_handle, targets[index]);
        if (widget && widget->vtable && widget->vtable->pointer_event) {
            widget->vtable->pointer_event(widget, document_handle, &event);
            notified = true;
        }
    }
    return notified;
}

void tc_ui_internal_invalidate_subtree_interaction_state(tc_widget* root, tc_ui_pointer_cancel_reason reason) {
    tc_ui_document* document;
    tc_ui_document_handle document_handle;
    tc_widget_handle old_hover;
    tc_widget_handle old_focus;
    bool clear_hover;
    bool clear_capture;
    bool clear_pressed;
    bool clear_focus;
    if (!root || !(document = tc_ui_internal_resolve_document(root->document))) {
        return;
    }
    document_handle = document->handle;
    old_hover = document->hovered_widget;
    old_focus = document->focused_widget;
    clear_hover = tc_ui_internal_handle_is_in_subtree(document, document->hovered_widget, root);
    clear_capture = tc_ui_internal_handle_is_in_subtree(document, document->pointer_capture, root);
    clear_pressed = tc_ui_internal_handle_is_in_subtree(document, document->pressed_widget, root);
    clear_focus = tc_ui_internal_handle_is_in_subtree(document, document->focused_widget, root);
    tc_ui_internal_cancel_pointer_state(document, clear_capture, clear_pressed, reason);
    document = tc_ui_internal_resolve_document(document_handle);
    if (!document) {
        return;
    }
    if (clear_hover && tc_ui_internal_same_handle(document->hovered_widget, old_hover)) {
        if (document->has_pointer_event) {
            tc_ui_internal_update_hover(document, tc_widget_handle_invalid(), &document->last_pointer_event);
        } else {
            document->hovered_widget = tc_widget_handle_invalid();
            tc_ui_internal_refresh_cursor(document);
        }
    }
    if (clear_focus && tc_ui_internal_same_handle(document->focused_widget, old_focus)) {
        tc_ui_internal_change_focus(document, tc_widget_handle_invalid());
    }
}

void tc_widget_set_focusable(tc_widget* widget, bool focusable) {
    set_widget_flag(widget, TC_WIDGET_FOCUSABLE, focusable);
    if (widget && !focusable) {
        tc_ui_document* document = tc_ui_internal_resolve_document(widget->document);
        if (document && tc_ui_internal_same_handle(document->focused_widget, widget->handle)) {
            tc_ui_internal_change_focus(document, tc_widget_handle_invalid());
        }
    }
}

bool tc_widget_is_focusable(const tc_widget* widget) {
    return widget && (widget->flags & TC_WIDGET_FOCUSABLE) != 0;
}

bool tc_widget_set_stable_id(tc_widget* widget, const char* stable_id) {
    if (!widget) {
        tc_log_error("[termin-gui-native] cannot set stable id on null widget");
        return false;
    }
    return replace_owned_string(stable_id, &widget->stable_id, &widget->owned_stable_id);
}

bool tc_widget_set_name(tc_widget* widget, const char* name) {
    if (!widget) {
        tc_log_error("[termin-gui-native] cannot set name on null widget");
        return false;
    }
    return replace_owned_string(name, &widget->name, &widget->owned_name);
}

bool tc_widget_set_debug_name(tc_widget* widget, const char* debug_name) {
    if (!widget) {
        tc_log_error("[termin-gui-native] cannot set debug name on null widget");
        return false;
    }
    return replace_owned_string(debug_name, &widget->debug_name, &widget->owned_debug_name);
}

void tc_widget_set_visible(tc_widget* widget, bool visible) {
    tc_ui_document_handle document = widget ? widget->document : tc_ui_document_handle_invalid();
    tc_widget_handle handle = widget ? widget->handle : tc_widget_handle_invalid();
    bool changed = widget && tc_widget_is_visible(widget) != visible;
    set_widget_flag(widget, TC_WIDGET_VISIBLE, visible);
    if (changed && !visible) {
        tc_ui_internal_invalidate_subtree_interaction_state(widget, TC_UI_POINTER_CANCEL_SUBTREE_INEFFECTIVE);
    }
    if (changed) {
        tc_widget* live_widget =
            !tc_ui_document_handle_is_invalid(document) ? tc_ui_document_resolve_widget(document, handle) : widget;
        if (live_widget) {
            tc_widget_mark_dirty(live_widget, TC_WIDGET_DIRTY_LAYOUT | TC_WIDGET_DIRTY_PAINT | TC_WIDGET_DIRTY_STATE);
        }
    }
}

bool tc_widget_is_visible(const tc_widget* widget) {
    return widget && (widget->flags & TC_WIDGET_VISIBLE) != 0;
}

void tc_widget_set_enabled(tc_widget* widget, bool enabled) {
    tc_ui_document_handle document = widget ? widget->document : tc_ui_document_handle_invalid();
    tc_widget_handle handle = widget ? widget->handle : tc_widget_handle_invalid();
    bool changed = widget && tc_widget_is_enabled(widget) != enabled;
    set_widget_flag(widget, TC_WIDGET_ENABLED, enabled);
    if (changed && !enabled) {
        tc_ui_internal_invalidate_subtree_interaction_state(widget, TC_UI_POINTER_CANCEL_SUBTREE_INEFFECTIVE);
    }
    if (changed) {
        tc_widget* live_widget =
            !tc_ui_document_handle_is_invalid(document) ? tc_ui_document_resolve_widget(document, handle) : widget;
        if (live_widget) {
            mark_style_subtree_dirty(live_widget);
        }
    }
}

bool tc_widget_is_enabled(const tc_widget* widget) {
    return widget && (widget->flags & TC_WIDGET_ENABLED) != 0;
}

void tc_widget_set_tree_participating(tc_widget* widget, bool participating) {
    bool changed = widget && tc_widget_is_tree_participating(widget) != participating;
    set_widget_flag(widget, TC_WIDGET_TREE_PARTICIPATING, participating);
    if (changed && !participating) {
        tc_ui_internal_invalidate_subtree_interaction_state(widget, TC_UI_POINTER_CANCEL_SUBTREE_INEFFECTIVE);
    }
}

bool tc_widget_is_tree_participating(const tc_widget* widget) {
    return widget && (widget->flags & TC_WIDGET_TREE_PARTICIPATING) != 0;
}

void tc_widget_set_mouse_transparent(tc_widget* widget, bool mouse_transparent) {
    tc_ui_document_handle document = widget ? widget->document : tc_ui_document_handle_invalid();
    tc_widget_handle handle = widget ? widget->handle : tc_widget_handle_invalid();
    bool changed = widget && tc_widget_is_mouse_transparent(widget) != mouse_transparent;
    set_widget_flag(widget, TC_WIDGET_MOUSE_TRANSPARENT, mouse_transparent);
    if (changed && mouse_transparent && widget) {
        tc_ui_document* owner = tc_ui_internal_resolve_document(widget->document);
        if (owner && tc_ui_internal_same_handle(owner->hovered_widget, widget->handle)) {
            if (owner->has_pointer_event) {
                tc_ui_internal_update_hover(owner, tc_widget_handle_invalid(), &owner->last_pointer_event);
            } else {
                owner->hovered_widget = tc_widget_handle_invalid();
                tc_ui_internal_refresh_cursor(owner);
            }
        }
    }
    if (changed) {
        tc_widget* live_widget =
            !tc_ui_document_handle_is_invalid(document) ? tc_ui_document_resolve_widget(document, handle) : widget;
        if (live_widget) {
            tc_widget_mark_dirty(live_widget, TC_WIDGET_DIRTY_STATE);
        }
    }
}

bool tc_widget_is_mouse_transparent(const tc_widget* widget) {
    return widget && (widget->flags & TC_WIDGET_MOUSE_TRANSPARENT) != 0;
}

bool tc_widget_set_cursor_intent(tc_widget* widget, tc_ui_cursor_intent cursor) {
    if (!widget) {
        tc_log_error("[termin-gui-native] cannot set cursor intent on null widget");
        return false;
    }
    if (cursor < TC_UI_CURSOR_INHERIT || cursor >= TC_UI_CURSOR_INTENT_COUNT) {
        tc_log_error("[termin-gui-native] rejected invalid widget cursor intent %d", (int)cursor);
        return false;
    }
    if (widget->cursor_intent == cursor) {
        return true;
    }
    widget->cursor_intent = cursor;
    if (!tc_ui_document_handle_is_invalid(widget->document)) {
        tc_ui_document* document = tc_ui_internal_resolve_document(widget->document);
        if (document) {
            tc_ui_internal_refresh_cursor(document);
        }
    }
    return true;
}

tc_ui_cursor_intent tc_widget_cursor_intent(const tc_widget* widget) {
    return widget ? widget->cursor_intent : TC_UI_CURSOR_INHERIT;
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

tc_ui_widget_layout_spec tc_ui_widget_layout_spec_default(void) {
    tc_ui_widget_layout_spec spec;
    memset(&spec, 0, sizeof(spec));
    spec.width.mode = TC_UI_LENGTH_AUTO;
    spec.height.mode = TC_UI_LENGTH_AUTO;
    spec.touch_target_policy = TC_UI_TOUCH_TARGET_NONE;
    return spec;
}

static bool normalize_length(tc_ui_length input, tc_ui_length* output) {
    if (!output || input.mode < TC_UI_LENGTH_AUTO || input.mode > TC_UI_LENGTH_PERCENT || !isfinite(input.value)) {
        return false;
    }
    if ((input.mode == TC_UI_LENGTH_FIXED && input.value < 0.0f) ||
        (input.mode == TC_UI_LENGTH_PERCENT && (input.value < 0.0f || input.value > 1.0f))) {
        return false;
    }
    *output = input;
    if (input.mode == TC_UI_LENGTH_AUTO || input.mode == TC_UI_LENGTH_FILL) {
        output->value = 0.0f;
    } else if (output->value == 0.0f) {
        output->value = 0.0f;
    }
    return true;
}

bool tc_ui_widget_layout_spec_normalize(const tc_ui_widget_layout_spec* spec, tc_ui_widget_layout_spec* out_spec) {
    tc_ui_widget_layout_spec normalized;
    if (!spec || !out_spec || !normalize_length(spec->width, &normalized.width) ||
        !normalize_length(spec->height, &normalized.height) || !isfinite(spec->min_width) || spec->min_width < 0.0f ||
        !isfinite(spec->min_height) || spec->min_height < 0.0f || !isfinite(spec->max_width) ||
        spec->max_width < 0.0f || !isfinite(spec->max_height) || spec->max_height < 0.0f ||
        (spec->max_width > 0.0f && spec->max_width < spec->min_width) ||
        (spec->max_height > 0.0f && spec->max_height < spec->min_height) || !isfinite(spec->margin.left) ||
        spec->margin.left < 0.0f || !isfinite(spec->margin.top) || spec->margin.top < 0.0f ||
        !isfinite(spec->margin.right) || spec->margin.right < 0.0f || !isfinite(spec->margin.bottom) ||
        spec->margin.bottom < 0.0f || !isfinite(spec->aspect_ratio) || spec->aspect_ratio < 0.0f ||
        spec->touch_target_policy < TC_UI_TOUCH_TARGET_NONE ||
        spec->touch_target_policy > TC_UI_TOUCH_TARGET_LAYOUT_MINIMUM || !isfinite(spec->minimum_touch_target.width) ||
        spec->minimum_touch_target.width < 0.0f || !isfinite(spec->minimum_touch_target.height) ||
        spec->minimum_touch_target.height < 0.0f) {
        return false;
    }
    if (spec->aspect_ratio > 0.0f && spec->width.mode != TC_UI_LENGTH_AUTO && spec->height.mode != TC_UI_LENGTH_AUTO) {
        return false;
    }
    if (spec->touch_target_policy == TC_UI_TOUCH_TARGET_LAYOUT_MINIMUM && spec->minimum_touch_target.width == 0.0f &&
        spec->minimum_touch_target.height == 0.0f) {
        return false;
    }
    if (spec->touch_target_policy == TC_UI_TOUCH_TARGET_LAYOUT_MINIMUM &&
        ((spec->max_width > 0.0f && spec->max_width < spec->minimum_touch_target.width) ||
         (spec->max_height > 0.0f && spec->max_height < spec->minimum_touch_target.height))) {
        return false;
    }
    normalized.min_width = spec->min_width == 0.0f ? 0.0f : spec->min_width;
    normalized.min_height = spec->min_height == 0.0f ? 0.0f : spec->min_height;
    normalized.max_width = spec->max_width == 0.0f ? 0.0f : spec->max_width;
    normalized.max_height = spec->max_height == 0.0f ? 0.0f : spec->max_height;
    normalized.margin = spec->margin;
    normalized.aspect_ratio = spec->aspect_ratio == 0.0f ? 0.0f : spec->aspect_ratio;
    normalized.touch_target_policy = spec->touch_target_policy;
    normalized.minimum_touch_target = spec->minimum_touch_target;
    if (normalized.touch_target_policy == TC_UI_TOUCH_TARGET_NONE) {
        normalized.minimum_touch_target = (tc_ui_size){0.0f, 0.0f};
    }
    *out_spec = normalized;
    return true;
}

static float resolve_length(tc_ui_length length, float intrinsic, float parent_extent, bool definite) {
    switch (length.mode) {
    case TC_UI_LENGTH_FIXED:
        return length.value;
    case TC_UI_LENGTH_FILL:
        return definite ? parent_extent : intrinsic;
    case TC_UI_LENGTH_PERCENT:
        return definite ? parent_extent * length.value : intrinsic;
    case TC_UI_LENGTH_AUTO:
    default:
        return intrinsic;
    }
}

static float clamp_layout_extent(float value, float minimum, float maximum) {
    value = fmaxf(value, minimum);
    return maximum > 0.0f ? fminf(value, maximum) : value;
}

bool tc_ui_widget_layout_spec_resolve_size(const tc_ui_widget_layout_spec* spec,
                                           tc_ui_size intrinsic_size,
                                           tc_ui_size parent_extent,
                                           bool width_definite,
                                           bool height_definite,
                                           tc_ui_size* out_size) {
    tc_ui_widget_layout_spec normalized;
    tc_ui_size resolved;
    if (!out_size || !tc_ui_widget_layout_spec_normalize(spec, &normalized) || !isfinite(intrinsic_size.width) ||
        intrinsic_size.width < 0.0f || !isfinite(intrinsic_size.height) || intrinsic_size.height < 0.0f ||
        (width_definite && (!isfinite(parent_extent.width) || parent_extent.width < 0.0f)) ||
        (height_definite && (!isfinite(parent_extent.height) || parent_extent.height < 0.0f))) {
        tc_log_error("[termin-gui-native] rejected invalid widget layout size resolution");
        return false;
    }
    resolved.width = resolve_length(normalized.width, intrinsic_size.width, parent_extent.width, width_definite);
    resolved.height = resolve_length(normalized.height, intrinsic_size.height, parent_extent.height, height_definite);
    if (normalized.aspect_ratio > 0.0f) {
        if (normalized.width.mode != TC_UI_LENGTH_AUTO) {
            resolved.height = resolved.width / normalized.aspect_ratio;
        } else if (normalized.height.mode != TC_UI_LENGTH_AUTO) {
            resolved.width = resolved.height * normalized.aspect_ratio;
        } else if (resolved.height > 0.0f) {
            resolved.width = resolved.height * normalized.aspect_ratio;
        }
    }
    if (normalized.touch_target_policy == TC_UI_TOUCH_TARGET_LAYOUT_MINIMUM) {
        resolved.width = fmaxf(resolved.width, normalized.minimum_touch_target.width);
        resolved.height = fmaxf(resolved.height, normalized.minimum_touch_target.height);
    }
    resolved.width = clamp_layout_extent(resolved.width, normalized.min_width, normalized.max_width);
    resolved.height = clamp_layout_extent(resolved.height, normalized.min_height, normalized.max_height);
    *out_size = resolved;
    return true;
}

tc_ui_widget_layout_spec tc_widget_layout_spec(const tc_widget* widget) {
    return widget ? widget->layout_spec : tc_ui_widget_layout_spec_default();
}

bool tc_widget_set_layout_spec(tc_widget* widget, const tc_ui_widget_layout_spec* spec) {
    tc_ui_widget_layout_spec normalized;
    if (!widget) {
        tc_log_error("[termin-gui-native] cannot set layout spec on null widget");
        return false;
    }
    if (!tc_ui_widget_layout_spec_normalize(spec, &normalized)) {
        tc_log_error("[termin-gui-native] rejected invalid widget layout spec");
        return false;
    }
    widget->layout_spec = normalized;
    tc_widget_mark_dirty(widget, TC_WIDGET_DIRTY_LAYOUT | TC_WIDGET_DIRTY_PAINT);
    return true;
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
    tc_widget* old_parent;
    tc_ui_document* document;
    size_t old_index = SIZE_MAX;
    if (!parent || !child || !tc_ui_internal_widget_is_live_pointer(parent) ||
        !tc_ui_internal_widget_is_live_pointer(child)) {
        tc_log_error("[termin-gui-native] cannot attach unadopted or stale widgets");
        return false;
    }
    old_parent = child->parent;
    if (!tc_ui_document_handle_eq(parent->document, child->document)) {
        tc_log_error("[termin-gui-native] cannot attach widgets from different documents");
        return false;
    }
    document = tc_ui_internal_resolve_document(child->document);
    if (!document || tc_ui_internal_find_overlay_index(document, child->handle) != SIZE_MAX) {
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
    if (!tc_ui_internal_reserve_array((void**)&parent->children,
                                      sizeof(tc_widget*),
                                      &parent->child_capacity,
                                      parent->child_count + (child->parent == parent ? 0 : 1))) {
        return false;
    }

    if (child->parent == parent) {
        old_index = tc_ui_internal_find_child_index(parent, child);
        if (old_index == SIZE_MAX) {
            tc_log_error("[termin-gui-native] inconsistent same-parent child link");
            return false;
        }
        tc_ui_internal_remove_child_at(parent, old_index);
    } else if (child->parent) {
        if (!tc_ui_internal_detach_widget(child)) {
            return false;
        }
    }
    document = tc_ui_internal_resolve_document(parent->document);
    if (!document) {
        tc_log_error("[termin-gui-native] cannot attach widget to an invalid document");
        return false;
    }
    tc_ui_internal_remove_root_references(document, child->handle);

    if (index > parent->child_count) {
        index = parent->child_count;
    }
    if (index < parent->child_count) {
        memmove(
            &parent->children[index + 1], &parent->children[index], (parent->child_count - index) * sizeof(tc_widget*));
    }
    parent->children[index] = child;
    parent->child_count += 1;
    child->parent = parent;
    if (old_parent && old_parent != parent) {
        tc_widget_mark_dirty(old_parent, TC_WIDGET_DIRTY_LAYOUT | TC_WIDGET_DIRTY_PAINT);
    }
    tc_widget_mark_dirty(parent, TC_WIDGET_DIRTY_LAYOUT | TC_WIDGET_DIRTY_PAINT);
    mark_style_subtree_dirty(child);
    tc_ui_internal_refresh_cursor(document);
    return true;
}

bool tc_widget_append_child(tc_widget* parent, tc_widget* child) {
    return tc_widget_insert_child(parent, SIZE_MAX, child);
}

bool tc_widget_remove_child(tc_widget* parent, tc_widget* child) {
    size_t index;
    if (!tc_ui_internal_widget_is_live_pointer(parent) || !tc_ui_internal_widget_is_live_pointer(child) ||
        !tc_ui_document_handle_eq(parent->document, child->document) || child->parent != parent) {
        return false;
    }
    index = tc_ui_internal_find_child_index(parent, child);
    if (index == SIZE_MAX) {
        tc_log_error("[termin-gui-native] cannot remove inconsistent child link");
        return false;
    }
    if (!tc_ui_internal_detach_widget(child)) {
        return false;
    }
    tc_widget_mark_dirty(parent, TC_WIDGET_DIRTY_LAYOUT | TC_WIDGET_DIRTY_PAINT);
    mark_style_subtree_dirty(child);
    tc_ui_document* document = tc_ui_internal_resolve_document(parent->document);
    if (document) {
        tc_ui_internal_refresh_cursor(document);
    }
    return true;
}

bool tc_widget_detach(tc_widget* widget) {
    tc_widget* parent;
    if (!tc_ui_internal_widget_is_live_pointer(widget) || !widget->parent) {
        return false;
    }
    parent = widget->parent;
    if (!tc_ui_internal_detach_widget(widget)) {
        return false;
    }
    tc_widget_mark_dirty(parent, TC_WIDGET_DIRTY_LAYOUT | TC_WIDGET_DIRTY_PAINT);
    mark_style_subtree_dirty(widget);
    tc_ui_document* document = tc_ui_internal_resolve_document(widget->document);
    if (document) {
        tc_ui_internal_refresh_cursor(document);
    }
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

const char* tc_widget_type_name(const tc_widget* widget) {
    if (!widget) {
        return NULL;
    }
    if (widget->runtime_type_link.type_name) {
        return widget->runtime_type_link.type_name;
    }
    return widget->vtable ? widget->vtable->type_name : NULL;
}

tc_widget_ownership_policy tc_widget_ownership(const tc_widget* widget) {
    return widget ? widget->ownership_policy : TC_WIDGET_BORROWED;
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

void tc_widget_set_style_role(tc_widget* widget, tc_ui_style_role role) {
    if (!widget) {
        tc_log_error("[termin-gui-native] cannot set style role on null widget");
        return;
    }
    if (role < TC_UI_STYLE_GENERIC || role >= TC_UI_STYLE_ROLE_COUNT) {
        tc_log_error("[termin-gui-native] cannot set invalid widget style role");
        return;
    }
    if (widget->style_role == role) {
        return;
    }
    widget->style_role = role;
    tc_widget_mark_dirty(widget, TC_WIDGET_DIRTY_LAYOUT | TC_WIDGET_DIRTY_PAINT | TC_WIDGET_DIRTY_STATE);
}

tc_ui_style_role tc_widget_style_role(const tc_widget* widget) {
    return widget ? widget->style_role : TC_UI_STYLE_GENERIC;
}

bool tc_widget_set_style_override(tc_widget* widget, const tc_ui_style_override* style_override) {
    bool inherited;
    if (!widget || !style_override) {
        tc_log_error("[termin-gui-native] cannot set null widget style override");
        return false;
    }
    if (!tc_ui_internal_valid_style_override(style_override)) {
        tc_log_error("[termin-gui-native] rejected invalid widget style override");
        return false;
    }
    inherited = ((widget->style_override.flags | style_override->flags) & TC_UI_STYLE_OVERRIDE_INHERIT) != 0;
    widget->style_override = *style_override;
    if (inherited) {
        mark_style_subtree_dirty(widget);
    } else {
        tc_widget_mark_dirty(widget, TC_WIDGET_DIRTY_LAYOUT | TC_WIDGET_DIRTY_PAINT | TC_WIDGET_DIRTY_STATE);
    }
    return true;
}

void tc_widget_clear_style_override(tc_widget* widget) {
    bool inherited;
    if (!widget) {
        tc_log_error("[termin-gui-native] cannot clear style override on null widget");
        return;
    }
    inherited = (widget->style_override.flags & TC_UI_STYLE_OVERRIDE_INHERIT) != 0;
    memset(&widget->style_override, 0, sizeof(widget->style_override));
    if (inherited) {
        mark_style_subtree_dirty(widget);
    } else {
        tc_widget_mark_dirty(widget, TC_WIDGET_DIRTY_LAYOUT | TC_WIDGET_DIRTY_PAINT | TC_WIDGET_DIRTY_STATE);
    }
}

tc_ui_style_override tc_widget_style_override(const tc_widget* widget) {
    tc_ui_style_override result;
    memset(&result, 0, sizeof(result));
    return widget ? widget->style_override : result;
}
