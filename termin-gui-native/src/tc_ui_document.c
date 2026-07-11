#include "tc_ui_document_internal.h"

#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include <tcbase/tc_log.h>

bool tc_ui_internal_same_handle(tc_widget_handle lhs, tc_widget_handle rhs) {
    return tc_widget_handle_eq(lhs, rhs);
}

bool tc_ui_internal_reserve_array(void** data, size_t item_size, size_t* capacity, size_t required) {
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

static tc_ui_color style_color(float r, float g, float b, float a) {
    return (tc_ui_color){r, g, b, a};
}

static bool valid_style_color(tc_ui_color color) {
    return isfinite(color.r) && isfinite(color.g) && isfinite(color.b) && isfinite(color.a) &&
        color.r >= 0.0f && color.r <= 1.0f &&
        color.g >= 0.0f && color.g <= 1.0f &&
        color.b >= 0.0f && color.b <= 1.0f &&
        color.a >= 0.0f && color.a <= 1.0f;
}

static bool valid_style(const tc_ui_style* style) {
    return style &&
        valid_style_color(style->background) &&
        valid_style_color(style->foreground) &&
        valid_style_color(style->border) &&
        valid_style_color(style->accent) &&
        isfinite(style->padding_left) && style->padding_left >= 0.0f &&
        isfinite(style->padding_top) && style->padding_top >= 0.0f &&
        isfinite(style->padding_right) && style->padding_right >= 0.0f &&
        isfinite(style->padding_bottom) && style->padding_bottom >= 0.0f &&
        isfinite(style->spacing) && style->spacing >= 0.0f &&
        isfinite(style->border_width) && style->border_width >= 0.0f &&
        isfinite(style->corner_radius) && style->corner_radius >= 0.0f &&
        isfinite(style->font_size) && style->font_size > 0.0f &&
        isfinite(style->min_width) && style->min_width >= 0.0f &&
        isfinite(style->min_height) && style->min_height >= 0.0f &&
        style->font_role >= TC_UI_FONT_BODY && style->font_role <= TC_UI_FONT_MONOSPACE;
}

bool tc_ui_internal_valid_style_override(const tc_ui_style_override* style_override) {
    tc_ui_style_field_mask fields;
    if (!style_override || (style_override->fields & ~TC_UI_STYLE_ALL_FIELDS) != 0 ||
        (style_override->flags & ~TC_UI_STYLE_OVERRIDE_INHERIT) != 0) {
        return false;
    }
    fields = style_override->fields;
    return
        ((fields & TC_UI_STYLE_BACKGROUND) == 0 || valid_style_color(style_override->value.background)) &&
        ((fields & TC_UI_STYLE_FOREGROUND) == 0 || valid_style_color(style_override->value.foreground)) &&
        ((fields & TC_UI_STYLE_BORDER) == 0 || valid_style_color(style_override->value.border)) &&
        ((fields & TC_UI_STYLE_ACCENT) == 0 || valid_style_color(style_override->value.accent)) &&
        ((fields & TC_UI_STYLE_PADDING_LEFT) == 0 ||
            (isfinite(style_override->value.padding_left) && style_override->value.padding_left >= 0.0f)) &&
        ((fields & TC_UI_STYLE_PADDING_TOP) == 0 ||
            (isfinite(style_override->value.padding_top) && style_override->value.padding_top >= 0.0f)) &&
        ((fields & TC_UI_STYLE_PADDING_RIGHT) == 0 ||
            (isfinite(style_override->value.padding_right) && style_override->value.padding_right >= 0.0f)) &&
        ((fields & TC_UI_STYLE_PADDING_BOTTOM) == 0 ||
            (isfinite(style_override->value.padding_bottom) && style_override->value.padding_bottom >= 0.0f)) &&
        ((fields & TC_UI_STYLE_SPACING) == 0 ||
            (isfinite(style_override->value.spacing) && style_override->value.spacing >= 0.0f)) &&
        ((fields & TC_UI_STYLE_BORDER_WIDTH) == 0 ||
            (isfinite(style_override->value.border_width) && style_override->value.border_width >= 0.0f)) &&
        (!(fields & TC_UI_STYLE_CORNER_RADIUS) ||
            (isfinite(style_override->value.corner_radius) && style_override->value.corner_radius >= 0.0f)) &&
        ((fields & TC_UI_STYLE_FONT_SIZE) == 0 ||
            (isfinite(style_override->value.font_size) && style_override->value.font_size > 0.0f)) &&
        ((fields & TC_UI_STYLE_MIN_WIDTH) == 0 ||
            (isfinite(style_override->value.min_width) && style_override->value.min_width >= 0.0f)) &&
        ((fields & TC_UI_STYLE_MIN_HEIGHT) == 0 ||
            (isfinite(style_override->value.min_height) && style_override->value.min_height >= 0.0f)) &&
        ((fields & TC_UI_STYLE_FONT_ROLE) == 0 ||
            (style_override->value.font_role >= TC_UI_FONT_BODY &&
                style_override->value.font_role <= TC_UI_FONT_MONOSPACE));
}

static bool valid_theme(const tc_ui_theme* theme) {
    size_t index;
    if (!theme) {
        return false;
    }
    for (index = 0; index < TC_UI_STYLE_ROLE_COUNT; ++index) {
        const tc_ui_role_style* role = &theme->roles[index];
        if (!valid_style(&role->base) ||
            !tc_ui_internal_valid_style_override(&role->hovered) ||
            !tc_ui_internal_valid_style_override(&role->pressed) ||
            !tc_ui_internal_valid_style_override(&role->focused) ||
            !tc_ui_internal_valid_style_override(&role->disabled) ||
            !tc_ui_internal_valid_style_override(&role->checked)) {
            return false;
        }
    }
    return true;
}

static void apply_style_override(tc_ui_style* style, const tc_ui_style_override* style_override) {
    const tc_ui_style_field_mask fields = style_override->fields;
    if ((fields & TC_UI_STYLE_BACKGROUND) != 0) style->background = style_override->value.background;
    if ((fields & TC_UI_STYLE_FOREGROUND) != 0) style->foreground = style_override->value.foreground;
    if ((fields & TC_UI_STYLE_BORDER) != 0) style->border = style_override->value.border;
    if ((fields & TC_UI_STYLE_ACCENT) != 0) style->accent = style_override->value.accent;
    if ((fields & TC_UI_STYLE_PADDING_LEFT) != 0) style->padding_left = style_override->value.padding_left;
    if ((fields & TC_UI_STYLE_PADDING_TOP) != 0) style->padding_top = style_override->value.padding_top;
    if ((fields & TC_UI_STYLE_PADDING_RIGHT) != 0) style->padding_right = style_override->value.padding_right;
    if ((fields & TC_UI_STYLE_PADDING_BOTTOM) != 0) style->padding_bottom = style_override->value.padding_bottom;
    if ((fields & TC_UI_STYLE_SPACING) != 0) style->spacing = style_override->value.spacing;
    if ((fields & TC_UI_STYLE_BORDER_WIDTH) != 0) style->border_width = style_override->value.border_width;
    if ((fields & TC_UI_STYLE_FONT_SIZE) != 0) style->font_size = style_override->value.font_size;
    if ((fields & TC_UI_STYLE_MIN_WIDTH) != 0) style->min_width = style_override->value.min_width;
    if ((fields & TC_UI_STYLE_MIN_HEIGHT) != 0) style->min_height = style_override->value.min_height;
    if ((fields & TC_UI_STYLE_CORNER_RADIUS) != 0) style->corner_radius = style_override->value.corner_radius;
    if ((fields & TC_UI_STYLE_FONT_ROLE) != 0) style->font_role = style_override->value.font_role;
}

static tc_ui_style default_base_style(void) {
    tc_ui_style style;
    memset(&style, 0, sizeof(style));
    style.background = style_color(0.0f, 0.0f, 0.0f, 0.0f);
    style.foreground = style_color(0.90f, 0.92f, 0.96f, 1.0f);
    style.border = style_color(0.32f, 0.34f, 0.38f, 1.0f);
    style.accent = style_color(0.25f, 0.58f, 0.88f, 1.0f);
    style.border_width = 1.0f;
    style.corner_radius = 4.0f;
    style.font_size = 14.0f;
    style.font_role = TC_UI_FONT_BODY;
    return style;
}

static tc_ui_style_override color_override(
    tc_ui_style_field_mask field,
    tc_ui_color color
) {
    tc_ui_style_override result;
    memset(&result, 0, sizeof(result));
    result.value = default_base_style();
    result.fields = field;
    if (field == TC_UI_STYLE_BACKGROUND) result.value.background = color;
    if (field == TC_UI_STYLE_FOREGROUND) result.value.foreground = color;
    if (field == TC_UI_STYLE_BORDER) result.value.border = color;
    if (field == TC_UI_STYLE_ACCENT) result.value.accent = color;
    return result;
}

void tc_ui_theme_init_default(tc_ui_theme* theme) {
    size_t index;
    tc_ui_style base;
    if (!theme) {
        tc_log_error("[termin-gui-native] cannot initialize null UI theme");
        return;
    }
    memset(theme, 0, sizeof(*theme));
    base = default_base_style();
    for (index = 0; index < TC_UI_STYLE_ROLE_COUNT; ++index) {
        theme->roles[index].base = base;
        theme->roles[index].disabled = color_override(
            TC_UI_STYLE_FOREGROUND,
            style_color(0.52f, 0.54f, 0.58f, 1.0f)
        );
    }

    theme->roles[TC_UI_STYLE_PANEL].base.background = style_color(0.16f, 0.17f, 0.19f, 1.0f);
    theme->roles[TC_UI_STYLE_PANEL].base.min_width = 96.0f;
    theme->roles[TC_UI_STYLE_PANEL].base.min_height = 64.0f;

    theme->roles[TC_UI_STYLE_LABEL].base.font_size = 15.0f;

    theme->roles[TC_UI_STYLE_BUTTON].base.background = style_color(0.24f, 0.25f, 0.28f, 1.0f);
    theme->roles[TC_UI_STYLE_BUTTON].base.foreground = style_color(0.88f, 0.89f, 0.91f, 1.0f);
    theme->roles[TC_UI_STYLE_BUTTON].base.border = style_color(0.0f, 0.0f, 0.0f, 0.0f);
    theme->roles[TC_UI_STYLE_BUTTON].base.padding_left = 9.0f;
    theme->roles[TC_UI_STYLE_BUTTON].base.padding_top = 5.0f;
    theme->roles[TC_UI_STYLE_BUTTON].base.padding_right = 9.0f;
    theme->roles[TC_UI_STYLE_BUTTON].base.padding_bottom = 5.0f;
    theme->roles[TC_UI_STYLE_BUTTON].base.border_width = 0.0f;
    theme->roles[TC_UI_STYLE_BUTTON].base.min_width = 0.0f;
    theme->roles[TC_UI_STYLE_BUTTON].base.min_height = 28.0f;
    theme->roles[TC_UI_STYLE_BUTTON].hovered = color_override(
        TC_UI_STYLE_BACKGROUND,
        style_color(0.29f, 0.30f, 0.34f, 1.0f)
    );
    theme->roles[TC_UI_STYLE_BUTTON].pressed = color_override(
        TC_UI_STYLE_BACKGROUND,
        style_color(0.18f, 0.34f, 0.54f, 1.0f)
    );
    theme->roles[TC_UI_STYLE_BUTTON].focused = color_override(
        TC_UI_STYLE_BORDER,
        style_color(0.48f, 0.72f, 1.0f, 1.0f)
    );
    theme->roles[TC_UI_STYLE_BUTTON].disabled = color_override(
        TC_UI_STYLE_BACKGROUND,
        style_color(0.20f, 0.21f, 0.23f, 1.0f)
    );
    theme->roles[TC_UI_STYLE_BUTTON].disabled.fields |= TC_UI_STYLE_FOREGROUND;
    theme->roles[TC_UI_STYLE_BUTTON].disabled.value.foreground = style_color(0.55f, 0.57f, 0.61f, 1.0f);

    theme->roles[TC_UI_STYLE_TEXT_INPUT].base.background = style_color(0.08f, 0.09f, 0.11f, 1.0f);
    theme->roles[TC_UI_STYLE_TEXT_INPUT].base.foreground = style_color(0.94f, 0.96f, 0.98f, 1.0f);
    theme->roles[TC_UI_STYLE_TEXT_INPUT].base.padding_left = 8.0f;
    theme->roles[TC_UI_STYLE_TEXT_INPUT].base.padding_top = 2.0f;
    theme->roles[TC_UI_STYLE_TEXT_INPUT].base.padding_right = 8.0f;
    theme->roles[TC_UI_STYLE_TEXT_INPUT].base.padding_bottom = 2.0f;
    theme->roles[TC_UI_STYLE_TEXT_INPUT].base.min_width = 160.0f;
    theme->roles[TC_UI_STYLE_TEXT_INPUT].base.min_height = 34.0f;
    theme->roles[TC_UI_STYLE_TEXT_INPUT].base.corner_radius = 3.0f;
    theme->roles[TC_UI_STYLE_TEXT_INPUT].focused = color_override(
        TC_UI_STYLE_BORDER,
        style_color(0.38f, 0.62f, 0.92f, 1.0f)
    );

    theme->roles[TC_UI_STYLE_GROUP_BOX].base.background = style_color(0.11f, 0.12f, 0.14f, 1.0f);
    theme->roles[TC_UI_STYLE_GROUP_BOX].base.padding_left = 10.0f;
    theme->roles[TC_UI_STYLE_GROUP_BOX].base.padding_top = 8.0f;
    theme->roles[TC_UI_STYLE_GROUP_BOX].base.padding_right = 10.0f;
    theme->roles[TC_UI_STYLE_GROUP_BOX].base.padding_bottom = 10.0f;
    theme->roles[TC_UI_STYLE_GROUP_BOX].base.font_size = 13.0f;

    theme->roles[TC_UI_STYLE_TAB].base.background = style_color(0.13f, 0.14f, 0.16f, 1.0f);
    theme->roles[TC_UI_STYLE_TAB].base.foreground = style_color(0.88f, 0.91f, 0.96f, 1.0f);
    theme->roles[TC_UI_STYLE_TAB].base.border = style_color(0.34f, 0.36f, 0.40f, 1.0f);
    theme->roles[TC_UI_STYLE_TAB].base.padding_left = 8.0f;
    theme->roles[TC_UI_STYLE_TAB].base.font_size = 13.0f;
    theme->roles[TC_UI_STYLE_TAB].checked = color_override(
        TC_UI_STYLE_BACKGROUND,
        style_color(0.20f, 0.26f, 0.34f, 1.0f)
    );

    theme->roles[TC_UI_STYLE_CHECKBOX].base.background = style_color(0.15f, 0.16f, 0.18f, 1.0f);
    theme->roles[TC_UI_STYLE_CHECKBOX].base.border = style_color(0.36f, 0.38f, 0.42f, 1.0f);
    theme->roles[TC_UI_STYLE_CHECKBOX].base.accent = style_color(0.88f, 0.96f, 0.91f, 1.0f);
    theme->roles[TC_UI_STYLE_CHECKBOX].base.min_width = 18.0f;
    theme->roles[TC_UI_STYLE_CHECKBOX].base.min_height = 18.0f;
    theme->roles[TC_UI_STYLE_CHECKBOX].base.corner_radius = 3.0f;
    theme->roles[TC_UI_STYLE_CHECKBOX].checked = color_override(
        TC_UI_STYLE_BACKGROUND,
        style_color(0.18f, 0.58f, 0.34f, 1.0f)
    );

    theme->roles[TC_UI_STYLE_PROGRESS].base.background = style_color(0.09f, 0.10f, 0.12f, 1.0f);
    theme->roles[TC_UI_STYLE_PROGRESS].base.min_width = 120.0f;
    theme->roles[TC_UI_STYLE_PROGRESS].base.min_height = 20.0f;

    theme->roles[TC_UI_STYLE_SLIDER].base.border = style_color(0.32f, 0.34f, 0.38f, 1.0f);
    theme->roles[TC_UI_STYLE_SLIDER].base.accent = style_color(0.88f, 0.66f, 0.24f, 1.0f);
    theme->roles[TC_UI_STYLE_SLIDER].base.foreground = style_color(0.96f, 0.88f, 0.64f, 1.0f);
    theme->roles[TC_UI_STYLE_SLIDER].base.min_width = 140.0f;
    theme->roles[TC_UI_STYLE_SLIDER].base.min_height = 28.0f;

    theme->roles[TC_UI_STYLE_SEPARATOR].base.background = style_color(0.36f, 0.38f, 0.42f, 1.0f);
}

tc_widget_slot* tc_ui_internal_resolve_slot(tc_ui_document* document, tc_widget_handle handle) {
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

const tc_widget_slot* tc_ui_internal_resolve_slot_const(
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

bool tc_ui_internal_widget_is_live_pointer(const tc_widget* widget) {
    const tc_widget_slot* slot;
    if (!widget || !widget->document || tc_widget_handle_is_invalid(widget->handle)) {
        return false;
    }
    slot = tc_ui_internal_resolve_slot_const(widget->document, widget->handle);
    return slot && !slot->destroying && slot->widget == widget;
}
size_t tc_ui_internal_find_child_index(const tc_widget* parent, const tc_widget* child) {
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

void tc_ui_internal_remove_child_at(tc_widget* parent, size_t index) {
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

bool tc_ui_internal_detach_widget(tc_widget* widget) {
    size_t index;
    if (!widget || !widget->parent) {
        return false;
    }
    index = tc_ui_internal_find_child_index(widget->parent, widget);
    if (index == SIZE_MAX) {
        tc_log_error("[termin-gui-native] widget parent link is not mirrored by its parent child list");
        widget->parent = NULL;
        return false;
    }
    tc_ui_internal_remove_child_at(widget->parent, index);
    return true;
}

void tc_ui_internal_remove_root_references(tc_ui_document* document, tc_widget_handle handle) {
    size_t read_index;
    size_t write_index = 0;
    if (!document) {
        return;
    }
    for (read_index = 0; read_index < document->root_count; ++read_index) {
        if (!tc_ui_internal_same_handle(document->roots[read_index], handle)) {
            document->roots[write_index++] = document->roots[read_index];
        }
    }
    document->root_count = write_index;
}

size_t tc_ui_internal_find_overlay_index(
    const tc_ui_document* document,
    tc_widget_handle handle
) {
    size_t index;
    if (!document) {
        return SIZE_MAX;
    }
    for (index = 0; index < document->overlay_count; ++index) {
        if (tc_ui_internal_same_handle(document->overlays[index].handle, handle)) {
            return index;
        }
    }
    return SIZE_MAX;
}

void tc_ui_internal_remove_overlay_at(tc_ui_document* document, size_t index) {
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

void tc_ui_internal_remove_overlay_references(tc_ui_document* document, tc_widget_handle handle) {
    size_t index;
    if (!document) {
        return;
    }
    for (index = document->overlay_count; index > 0; --index) {
        if (tc_ui_internal_same_handle(document->overlays[index - 1].handle, handle)) {
            tc_ui_internal_remove_overlay_at(document, index - 1);
        }
    }
}

void tc_ui_internal_clear_document_state_references(tc_ui_document* document, tc_widget_handle handle) {
    if (!document) {
        return;
    }
    if (tc_ui_internal_same_handle(document->hovered_widget, handle)) {
        document->hovered_widget = tc_widget_handle_invalid();
    }
    if (tc_ui_internal_same_handle(document->pointer_capture, handle)) {
        document->pointer_capture = tc_widget_handle_invalid();
    }
    if (tc_ui_internal_same_handle(document->pressed_widget, handle)) {
        document->pressed_widget = tc_widget_handle_invalid();
    }
    if (tc_ui_internal_same_handle(document->focused_widget, handle)) {
        document->focused_widget = tc_widget_handle_invalid();
    }
}

static bool append_free_slot(tc_ui_document* document, uint32_t index) {
    if (!tc_ui_internal_reserve_array(
            (void**)&document->free_slots,
            sizeof(uint32_t),
            &document->free_slot_capacity,
            document->free_slot_count + 1)) {
        return false;
    }
    document->free_slots[document->free_slot_count++] = index;
    return true;
}

static bool destroy_widget_inner(tc_ui_document* document, tc_widget_handle handle, bool recursive) {
    tc_widget_slot* slot = tc_ui_internal_resolve_slot(document, handle);
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
                tc_ui_internal_remove_child_at(widget, widget->child_count - 1);
                ok = false;
                continue;
            }
            {
                tc_widget_handle child_handle = child->handle;
                if (!destroy_widget_inner(document, child_handle, true)) {
                    tc_widget* remaining = tc_ui_document_resolve_widget(document, child_handle);
                    if (remaining && remaining->parent == widget) {
                        tc_ui_internal_detach_widget(remaining);
                    }
                    ok = false;
                }
            }
        }
    } else {
        while (widget->child_count > 0) {
            tc_ui_internal_remove_child_at(widget, widget->child_count - 1);
        }
    }

    tc_ui_internal_detach_widget(widget);
    if (tc_ui_internal_same_handle(document->focused_widget, handle)) {
        document->focused_widget = tc_widget_handle_invalid();
        if (widget->vtable && widget->vtable->focus_event) {
            widget->vtable->focus_event(widget, document, false);
        }
    }
    if (widget->vtable && widget->vtable->on_destroy) {
        widget->vtable->on_destroy(widget, document);
    }
    tc_ui_internal_remove_root_references(document, handle);
    tc_ui_internal_remove_overlay_references(document, handle);
    tc_ui_internal_clear_document_state_references(document, handle);

    deleter = widget->deleter;
    tc_runtime_type_registry_unlink_instance(&widget->runtime_type_link);
    free(widget->children);
    tc_ui_internal_release_widget_metadata(widget);
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
    tc_ui_theme_init_default(&document->theme);
    document->theme_revision = 1;
    return document;
}

const tc_ui_theme* tc_ui_document_theme(const tc_ui_document* document) {
    return document ? &document->theme : NULL;
}

bool tc_ui_document_set_theme(tc_ui_document* document, const tc_ui_theme* theme) {
    size_t index;
    if (!document || !theme) {
        tc_log_error("[termin-gui-native] cannot set null UI document theme");
        return false;
    }
    if (!valid_theme(theme)) {
        tc_log_error("[termin-gui-native] rejected invalid UI document theme");
        return false;
    }
    document->theme = *theme;
    document->theme_revision += 1;
    if (document->theme_revision == 0) {
        document->theme_revision = 1;
    }
    for (index = 0; index < document->slot_count; ++index) {
        tc_widget_slot* slot = &document->slots[index];
        if (slot->widget && !slot->destroying) {
            tc_widget_mark_dirty(
                slot->widget,
                TC_WIDGET_DIRTY_LAYOUT | TC_WIDGET_DIRTY_PAINT | TC_WIDGET_DIRTY_STATE
            );
        }
    }
    return true;
}

uint64_t tc_ui_document_theme_revision(const tc_ui_document* document) {
    return document ? document->theme_revision : 0;
}

uint32_t tc_ui_document_widget_style_state(
    const tc_ui_document* document,
    const tc_widget* widget
) {
    uint32_t state = 0;
    if (!document || !widget || widget->document != document || !tc_ui_internal_widget_is_live_pointer(widget)) {
        return 0;
    }
    if (tc_ui_internal_same_handle(document->hovered_widget, widget->handle)) {
        state |= TC_UI_STYLE_STATE_HOVERED;
    }
    if (tc_ui_internal_same_handle(document->pressed_widget, widget->handle)) {
        state |= TC_UI_STYLE_STATE_PRESSED;
    }
    if (tc_ui_internal_same_handle(document->focused_widget, widget->handle)) {
        state |= TC_UI_STYLE_STATE_FOCUSED;
    }
    if (!tc_ui_internal_widget_effectively_enabled(widget)) {
        state |= TC_UI_STYLE_STATE_DISABLED;
    }
    return state;
}

static bool apply_inherited_style_ancestors(
    const tc_ui_document* document,
    const tc_widget* ancestor,
    size_t depth,
    tc_ui_style* style
) {
    if (!ancestor) {
        return true;
    }
    if (depth >= document->live_count || ancestor->document != document) {
        tc_log_error("[termin-gui-native] invalid canonical tree while resolving widget style");
        return false;
    }
    if (!apply_inherited_style_ancestors(
            document,
            ancestor->parent,
            depth + 1,
            style)) {
        return false;
    }
    if ((ancestor->style_override.flags & TC_UI_STYLE_OVERRIDE_INHERIT) != 0) {
        apply_style_override(style, &ancestor->style_override);
    }
    return true;
}

bool tc_ui_document_resolve_style(
    const tc_ui_document* document,
    const tc_widget* widget,
    uint32_t extra_state_flags,
    tc_ui_style* out_style
) {
    const uint32_t all_states = TC_UI_STYLE_STATE_HOVERED |
        TC_UI_STYLE_STATE_PRESSED |
        TC_UI_STYLE_STATE_FOCUSED |
        TC_UI_STYLE_STATE_DISABLED |
        TC_UI_STYLE_STATE_CHECKED;
    const tc_ui_role_style* role;
    uint32_t state;

    if (!document || !widget || !out_style) {
        tc_log_error("[termin-gui-native] cannot resolve style with null arguments");
        return false;
    }
    if (widget->document != document || !tc_ui_internal_widget_is_live_pointer(widget)) {
        tc_log_error("[termin-gui-native] cannot resolve style for foreign or stale widget");
        return false;
    }
    if (widget->style_role < TC_UI_STYLE_GENERIC || widget->style_role >= TC_UI_STYLE_ROLE_COUNT) {
        tc_log_error("[termin-gui-native] cannot resolve invalid widget style role");
        return false;
    }
    if ((extra_state_flags & ~all_states) != 0) {
        tc_log_error("[termin-gui-native] cannot resolve unknown widget style state flags");
        return false;
    }

    role = &document->theme.roles[widget->style_role];
    *out_style = role->base;
    state = tc_ui_document_widget_style_state(document, widget) | extra_state_flags;
    if ((state & TC_UI_STYLE_STATE_HOVERED) != 0) apply_style_override(out_style, &role->hovered);
    if ((state & TC_UI_STYLE_STATE_PRESSED) != 0) apply_style_override(out_style, &role->pressed);
    if ((state & TC_UI_STYLE_STATE_FOCUSED) != 0) apply_style_override(out_style, &role->focused);
    if ((state & TC_UI_STYLE_STATE_CHECKED) != 0) apply_style_override(out_style, &role->checked);
    if ((state & TC_UI_STYLE_STATE_DISABLED) != 0) apply_style_override(out_style, &role->disabled);

    if (!apply_inherited_style_ancestors(document, widget->parent, 0, out_style)) {
        return false;
    }
    apply_style_override(out_style, &widget->style_override);
    return true;
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
        if (!tc_ui_internal_reserve_array(
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
    const tc_widget_slot* slot = tc_ui_internal_resolve_slot_const(document, handle);
    return slot && !slot->destroying;
}

tc_widget* tc_ui_document_resolve_widget(tc_ui_document* document, tc_widget_handle handle) {
    tc_widget_slot* slot = tc_ui_internal_resolve_slot(document, handle);
    return slot && !slot->destroying ? slot->widget : NULL;
}

const tc_widget* tc_ui_document_resolve_widget_const(
    const tc_ui_document* document,
    tc_widget_handle handle
) {
    const tc_widget_slot* slot = tc_ui_internal_resolve_slot_const(document, handle);
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

void tc_ui_document_set_clipboard(
    tc_ui_document* document,
    tc_ui_clipboard_get_text_fn get_text,
    tc_ui_clipboard_set_text_fn set_text,
    void* user_data
) {
    if (!document) {
        tc_log_error("[termin-gui-native] cannot configure clipboard on null document");
        return;
    }
    document->clipboard_get_text = get_text;
    document->clipboard_set_text = set_text;
    document->clipboard_user_data = (get_text || set_text) ? user_data : NULL;
}

const char* tc_ui_document_clipboard_text(tc_ui_document* document) {
    const char* text;
    if (!document || !document->clipboard_get_text) {
        return NULL;
    }
    text = document->clipboard_get_text(document->clipboard_user_data);
    if (!text) {
        tc_log_error("[termin-gui-native] clipboard getter failed");
    }
    return text;
}

bool tc_ui_document_set_clipboard_text(
    tc_ui_document* document,
    const char* text_utf8,
    size_t text_byte_length
) {
    if (!document || !document->clipboard_set_text || (!text_utf8 && text_byte_length > 0)) {
        return false;
    }
    if (!document->clipboard_set_text(
        document->clipboard_user_data,
        text_utf8 ? text_utf8 : "",
        text_byte_length
    )) {
        tc_log_error("[termin-gui-native] clipboard setter failed");
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
    if (tc_ui_internal_find_overlay_index(document, handle) != SIZE_MAX) {
        tc_log_error("[termin-gui-native] cannot add an active overlay as a root");
        return false;
    }
    for (index = 0; index < document->root_count; ++index) {
        if (tc_ui_internal_same_handle(document->roots[index], handle)) {
            return true;
        }
    }
    if (!tc_ui_internal_reserve_array(
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
    tc_ui_internal_remove_root_references(document, handle);
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
