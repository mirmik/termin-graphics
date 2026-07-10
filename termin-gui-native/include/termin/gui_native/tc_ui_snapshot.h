#ifndef TERMIN_GUI_NATIVE_TC_UI_SNAPSHOT_H
#define TERMIN_GUI_NATIVE_TC_UI_SNAPSHOT_H

#include <termin/gui_native/tc_ui_document.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct tc_ui_widget_snapshot {
    tc_widget_handle handle;
    tc_widget_handle parent;
    char* type_name;
    char* stable_id;
    char* name;
    char* debug_name;
    tc_language native_language;
    tc_widget_ownership_policy ownership;
    tc_ui_rect bounds;
    tc_ui_size min_size;
    tc_ui_size preferred_size;
    tc_ui_size max_size;
    uint32_t flags;
    uint32_t dirty_flags;
    tc_ui_style_role style_role;
    tc_ui_style_override style_override;
    size_t child_offset;
    size_t child_count;
} tc_ui_widget_snapshot;

typedef struct tc_ui_overlay_snapshot {
    tc_widget_handle handle;
    uint32_t flags;
} tc_ui_overlay_snapshot;

typedef struct tc_ui_document_inspect_snapshot {
    tc_ui_widget_snapshot* widgets;
    size_t widget_count;
    tc_widget_handle* children;
    size_t child_count;
    tc_widget_handle* roots;
    size_t root_count;
    tc_ui_overlay_snapshot* overlays;
    size_t overlay_count;
    tc_widget_handle hovered;
    tc_widget_handle pressed;
    tc_widget_handle pointer_capture;
    tc_widget_handle focused;
    uint64_t theme_revision;
} tc_ui_document_inspect_snapshot;

/* Captures an owner-thread, point-in-time copy. On failure out_snapshot is unchanged. */
TERMIN_GUI_NATIVE_API bool
tc_ui_document_capture_snapshot(const tc_ui_document* document,
                                tc_ui_document_inspect_snapshot* out_snapshot);

/* Releases every allocation and resets the snapshot to its zero state. */
TERMIN_GUI_NATIVE_API void
tc_ui_document_snapshot_destroy(tc_ui_document_inspect_snapshot* snapshot);

#ifdef __cplusplus
}
#endif

#endif // TERMIN_GUI_NATIVE_TC_UI_SNAPSHOT_H
