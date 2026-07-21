#ifndef TERMIN_GUI_NATIVE_TC_UI_DOCUMENT_INTERNAL_H
#define TERMIN_GUI_NATIVE_TC_UI_DOCUMENT_INTERNAL_H

#include <termin/gui_native/tc_ui_document.h>

#include <stddef.h>

#if defined(_WIN32)
#define TC_UI_INTERNAL
#else
#define TC_UI_INTERNAL __attribute__((visibility("hidden")))
#endif

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
    tc_ui_document_handle handle;
    char debug_name[TC_UI_DOCUMENT_DEBUG_NAME_CAPACITY];

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
    tc_ui_cursor_intent cursor_intent;
    tc_ui_cursor_changed_fn cursor_changed;
    void* cursor_changed_user_data;
    size_t live_count;

    tc_ui_theme theme;
    uint64_t theme_revision;

    tc_ui_text_measure_fn measure_text;
    void* text_measurer_user_data;
    bool missing_text_measurer_logged;
    bool text_measure_failure_logged;

    tc_ui_clipboard_get_text_fn clipboard_get_text;
    tc_ui_clipboard_set_text_fn clipboard_set_text;
    void* clipboard_user_data;
};

TC_UI_INTERNAL tc_ui_document* tc_ui_internal_resolve_document(
    tc_ui_document_handle handle
);
TC_UI_INTERNAL tc_ui_document* tc_ui_internal_resolve_document_checked(
    tc_ui_document_handle handle,
    const char* operation
);

TC_UI_INTERNAL bool tc_ui_internal_same_handle(tc_widget_handle lhs, tc_widget_handle rhs);
TC_UI_INTERNAL bool tc_ui_internal_reserve_array(
    void** data,
    size_t item_size,
    size_t* capacity,
    size_t required
);
TC_UI_INTERNAL tc_widget_slot* tc_ui_internal_resolve_slot(
    tc_ui_document* document,
    tc_widget_handle handle
);
TC_UI_INTERNAL const tc_widget_slot* tc_ui_internal_resolve_slot_const(
    const tc_ui_document* document,
    tc_widget_handle handle
);
TC_UI_INTERNAL bool tc_ui_internal_widget_is_live_pointer(const tc_widget* widget);
TC_UI_INTERNAL void tc_ui_internal_release_widget_metadata(tc_widget* widget);
TC_UI_INTERNAL size_t tc_ui_internal_find_child_index(
    const tc_widget* parent,
    const tc_widget* child
);
TC_UI_INTERNAL void tc_ui_internal_remove_child_at(tc_widget* parent, size_t index);
TC_UI_INTERNAL bool tc_ui_internal_detach_widget(tc_widget* widget);
TC_UI_INTERNAL void tc_ui_internal_remove_root_references(
    tc_ui_document* document,
    tc_widget_handle handle
);
TC_UI_INTERNAL size_t tc_ui_internal_find_overlay_index(
    const tc_ui_document* document,
    tc_widget_handle handle
);
TC_UI_INTERNAL void tc_ui_internal_remove_overlay_at(tc_ui_document* document, size_t index);
TC_UI_INTERNAL void tc_ui_internal_remove_overlay_references(
    tc_ui_document* document,
    tc_widget_handle handle
);
TC_UI_INTERNAL void tc_ui_internal_clear_document_state_references(
    tc_ui_document* document,
    tc_widget_handle handle
);
TC_UI_INTERNAL bool tc_ui_internal_widget_effectively_interactive(const tc_widget* widget);
TC_UI_INTERNAL bool tc_ui_internal_widget_effectively_enabled(const tc_widget* widget);
TC_UI_INTERNAL bool tc_ui_internal_widget_is_descendant_of(
    const tc_widget* widget,
    const tc_widget* ancestor
);
TC_UI_INTERNAL bool tc_ui_internal_handle_is_in_subtree(
    tc_ui_document* document,
    tc_widget_handle handle,
    const tc_widget* root
);
TC_UI_INTERNAL void tc_ui_internal_invalidate_subtree_interaction_state(tc_widget* root);
TC_UI_INTERNAL bool tc_ui_internal_valid_style_override(
    const tc_ui_style_override* style_override
);
TC_UI_INTERNAL bool tc_ui_internal_change_focus(
    tc_ui_document* document,
    tc_widget_handle next
);
TC_UI_INTERNAL void tc_ui_internal_update_hover(
    tc_ui_document* document,
    tc_widget_handle next,
    const tc_ui_pointer_event* source
);
TC_UI_INTERNAL void tc_ui_internal_refresh_cursor(tc_ui_document* document);

#endif
