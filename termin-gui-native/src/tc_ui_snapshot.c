#include <termin/gui_native/tc_ui_snapshot.h>

#include "tc_ui_document_internal.h"

#include <stdlib.h>
#include <string.h>

#include <tcbase/tc_log.h>

static char* copy_optional_string(const char* value) {
    size_t length;
    char* copy;
    if (!value) {
        return NULL;
    }
    length = strlen(value);
    copy = (char*)malloc(length + 1);
    if (!copy) {
        return NULL;
    }
    memcpy(copy, value, length + 1);
    return copy;
}

static bool copy_widget_strings(tc_ui_widget_snapshot* snapshot, const tc_widget* widget) {
    snapshot->type_name = copy_optional_string(tc_widget_type_name(widget));
    snapshot->stable_id = copy_optional_string(widget->stable_id);
    snapshot->name = copy_optional_string(widget->name);
    snapshot->debug_name = copy_optional_string(widget->debug_name);
    return snapshot->type_name && (!widget->stable_id || snapshot->stable_id) &&
           (!widget->name || snapshot->name) && (!widget->debug_name || snapshot->debug_name);
}

static bool handle_is_coherent(tc_ui_document_handle document, tc_widget_handle handle) {
    return tc_widget_handle_is_invalid(handle) || tc_ui_document_is_alive(document, handle);
}

void tc_ui_document_snapshot_destroy(tc_ui_document_inspect_snapshot* snapshot) {
    size_t index;
    if (!snapshot) {
        return;
    }
    if (snapshot->widgets) {
        for (index = 0; index < snapshot->widget_count; ++index) {
            free(snapshot->widgets[index].type_name);
            free(snapshot->widgets[index].stable_id);
            free(snapshot->widgets[index].name);
            free(snapshot->widgets[index].debug_name);
        }
    }
    free(snapshot->widgets);
    free(snapshot->children);
    free(snapshot->roots);
    free(snapshot->overlays);
    memset(snapshot, 0, sizeof(*snapshot));
}

bool tc_ui_document_capture_snapshot(tc_ui_document_handle document_handle,
                                     tc_ui_document_inspect_snapshot* out_snapshot) {
    tc_ui_document_inspect_snapshot snapshot = {0};
    tc_ui_document* document = tc_ui_internal_resolve_document_checked(
        document_handle, "tc_ui_document_capture_snapshot");
    size_t total_children = 0;
    size_t slot_index;
    size_t widget_index = 0;
    size_t child_index = 0;
    if (!document || !out_snapshot) {
        tc_log_error("[termin-gui-native] document snapshot requires document and output");
        return false;
    }

    for (slot_index = 0; slot_index < document->slot_count; ++slot_index) {
        const tc_widget_slot* slot = &document->slots[slot_index];
        if (slot->widget && !slot->destroying) {
            if (SIZE_MAX - total_children < slot->widget->child_count) {
                tc_log_error("[termin-gui-native] document snapshot child count overflow");
                return false;
            }
            total_children += slot->widget->child_count;
        }
    }

    snapshot.widget_count = document->live_count;
    snapshot.child_count = total_children;
    snapshot.root_count = document->root_count;
    snapshot.overlay_count = document->overlay_count;
    if (snapshot.widget_count) {
        snapshot.widgets =
            (tc_ui_widget_snapshot*)calloc(snapshot.widget_count, sizeof(tc_ui_widget_snapshot));
    }
    if (snapshot.child_count) {
        snapshot.children =
            (tc_widget_handle*)calloc(snapshot.child_count, sizeof(tc_widget_handle));
    }
    if (snapshot.root_count) {
        snapshot.roots = (tc_widget_handle*)malloc(snapshot.root_count * sizeof(tc_widget_handle));
    }
    if (snapshot.overlay_count) {
        snapshot.overlays =
            (tc_ui_overlay_snapshot*)calloc(snapshot.overlay_count, sizeof(tc_ui_overlay_snapshot));
    }
    if ((snapshot.widget_count && !snapshot.widgets) ||
        (snapshot.child_count && !snapshot.children) || (snapshot.root_count && !snapshot.roots) ||
        (snapshot.overlay_count && !snapshot.overlays)) {
        tc_log_error("[termin-gui-native] failed to allocate document inspect snapshot");
        tc_ui_document_snapshot_destroy(&snapshot);
        return false;
    }

    for (slot_index = 0; slot_index < document->slot_count; ++slot_index) {
        const tc_widget_slot* slot = &document->slots[slot_index];
        const tc_widget* widget = slot->widget;
        tc_ui_widget_snapshot* target;
        size_t index;
        if (!widget || slot->destroying) {
            continue;
        }
        if (widget_index >= snapshot.widget_count) {
            tc_log_error("[termin-gui-native] document live widget count changed during snapshot");
            tc_ui_document_snapshot_destroy(&snapshot);
            return false;
        }
        target = &snapshot.widgets[widget_index++];
        target->handle = widget->handle;
        if (widget->parent && (!tc_ui_document_handle_eq(
                                   widget->parent->document, document_handle) ||
                               tc_widget_handle_is_invalid(widget->parent->handle))) {
            tc_log_error("[termin-gui-native] invalid canonical parent during snapshot");
            tc_ui_document_snapshot_destroy(&snapshot);
            return false;
        }
        target->parent = widget->parent ? widget->parent->handle : tc_widget_handle_invalid_value();
        if (!copy_widget_strings(target, widget)) {
            tc_log_error("[termin-gui-native] failed to copy widget metadata into snapshot");
            tc_ui_document_snapshot_destroy(&snapshot);
            return false;
        }
        target->native_language = widget->native_language;
        target->ownership = widget->ownership_policy;
        target->bounds = widget->bounds;
        target->min_size = widget->min_size;
        target->preferred_size = widget->preferred_size;
        target->max_size = widget->max_size;
        target->flags = widget->flags;
        target->dirty_flags = tc_widget_dirty_flags(widget);
        target->cursor_intent = widget->cursor_intent;
        target->style_role = widget->style_role;
        target->style_override = widget->style_override;
        target->child_offset = child_index;
        target->child_count = widget->child_count;
        for (index = 0; index < widget->child_count; ++index) {
            const tc_widget* child = widget->children[index];
            if (!child || !tc_ui_document_handle_eq(child->document, document_handle) ||
                tc_widget_handle_is_invalid(child->handle)) {
                tc_log_error("[termin-gui-native] invalid canonical child during snapshot");
                tc_ui_document_snapshot_destroy(&snapshot);
                return false;
            }
            if (child_index >= snapshot.child_count || !snapshot.children) {
                tc_log_error("[termin-gui-native] document child topology changed during snapshot");
                tc_ui_document_snapshot_destroy(&snapshot);
                return false;
            }
            snapshot.children[child_index++] = child->handle;
        }
    }
    if (widget_index != snapshot.widget_count || child_index != snapshot.child_count) {
        tc_log_error("[termin-gui-native] document topology changed during snapshot");
        tc_ui_document_snapshot_destroy(&snapshot);
        return false;
    }

    if (snapshot.root_count) {
        memcpy(snapshot.roots, document->roots, snapshot.root_count * sizeof(tc_widget_handle));
    }
    for (slot_index = 0; slot_index < snapshot.root_count; ++slot_index) {
        if (!tc_ui_document_is_alive(document_handle, snapshot.roots[slot_index])) {
            tc_log_error("[termin-gui-native] stale root handle during snapshot");
            tc_ui_document_snapshot_destroy(&snapshot);
            return false;
        }
    }
    for (slot_index = 0; slot_index < snapshot.overlay_count; ++slot_index) {
        snapshot.overlays[slot_index].handle = document->overlays[slot_index].handle;
        snapshot.overlays[slot_index].flags = document->overlays[slot_index].flags;
        if (!tc_ui_document_is_alive(document_handle, snapshot.overlays[slot_index].handle)) {
            tc_log_error("[termin-gui-native] stale overlay handle during snapshot");
            tc_ui_document_snapshot_destroy(&snapshot);
            return false;
        }
    }
    snapshot.hovered = document->hovered_widget;
    snapshot.pressed = document->pressed_widget;
    snapshot.pointer_capture = document->pointer_capture;
    snapshot.focused = document->focused_widget;
    snapshot.cursor_intent = document->cursor_intent;
    if (!handle_is_coherent(document_handle, snapshot.hovered) ||
        !handle_is_coherent(document_handle, snapshot.pressed) ||
        !handle_is_coherent(document_handle, snapshot.pointer_capture) ||
        !handle_is_coherent(document_handle, snapshot.focused)) {
        tc_log_error("[termin-gui-native] stale interaction handle during snapshot");
        tc_ui_document_snapshot_destroy(&snapshot);
        return false;
    }
    snapshot.theme_revision = document->theme_revision;
    *out_snapshot = snapshot;
    return true;
}
