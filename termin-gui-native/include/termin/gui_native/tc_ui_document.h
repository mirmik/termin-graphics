#ifndef TERMIN_GUI_NATIVE_TC_UI_DOCUMENT_H
#define TERMIN_GUI_NATIVE_TC_UI_DOCUMENT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <tcbase/tc_binding_types.h>
#include <termin/gui_native/export.h>

#ifdef __cplusplus
extern "C" {
#endif

TC_DEFINE_HANDLE(tc_widget_handle)

typedef struct tc_ui_document tc_ui_document;
typedef struct tc_widget tc_widget;
typedef struct tc_ui_draw_list tc_ui_draw_list;
typedef struct tc_ui_paint_context tc_ui_paint_context;

typedef struct tc_ui_size {
    float width;
    float height;
} tc_ui_size;

typedef struct tc_ui_rect {
    float x;
    float y;
    float width;
    float height;
} tc_ui_rect;

typedef struct tc_ui_point {
    float x;
    float y;
} tc_ui_point;

typedef struct tc_ui_color {
    float r;
    float g;
    float b;
    float a;
} tc_ui_color;

typedef struct tc_ui_constraints {
    tc_ui_size min_size;
    tc_ui_size max_size;
} tc_ui_constraints;

typedef struct tc_ui_text_metrics {
    float width;
    float height;
    float ascent;
    float descent;
    float line_height;
} tc_ui_text_metrics;

typedef bool (*tc_ui_text_measure_fn)(
    void* user_data,
    const char* text_utf8,
    size_t text_byte_length,
    float font_size,
    tc_ui_text_metrics* out_metrics
);

typedef enum tc_ui_draw_command_type {
    TC_UI_DRAW_FILL_RECT = 0,
    TC_UI_DRAW_STROKE_RECT = 1,
    TC_UI_DRAW_LINE = 2,
    TC_UI_DRAW_PUSH_CLIP = 3,
    TC_UI_DRAW_POP_CLIP = 4,
    TC_UI_DRAW_TEXT = 5
} tc_ui_draw_command_type;

typedef struct tc_ui_draw_command {
    tc_ui_draw_command_type type;
    tc_ui_rect rect;
    tc_ui_point p0;
    tc_ui_point p1;
    tc_ui_color color;
    float thickness;
    const char* text;
    float font_size;
} tc_ui_draw_command;

typedef enum tc_ui_event_result {
    TC_UI_EVENT_IGNORED = 0,
    TC_UI_EVENT_HANDLED = 1
} tc_ui_event_result;

typedef enum tc_ui_pointer_event_type {
    TC_UI_POINTER_MOVE = 0,
    TC_UI_POINTER_DOWN = 1,
    TC_UI_POINTER_UP = 2,
    TC_UI_POINTER_WHEEL = 3
} tc_ui_pointer_event_type;

typedef enum tc_widget_flag {
    TC_WIDGET_FOCUSABLE = 1u << 0,
    TC_WIDGET_DIRTY_LAYOUT = 1u << 1,
    TC_WIDGET_DIRTY_PAINT = 1u << 2,
    TC_WIDGET_DIRTY_STATE = 1u << 3,
    TC_WIDGET_VISIBLE = 1u << 4,
    TC_WIDGET_ENABLED = 1u << 5,
    TC_WIDGET_MOUSE_TRANSPARENT = 1u << 6,
    TC_WIDGET_DIRTY_MASK = TC_WIDGET_DIRTY_LAYOUT | TC_WIDGET_DIRTY_PAINT | TC_WIDGET_DIRTY_STATE
} tc_widget_flag;

typedef struct tc_ui_pointer_event {
    tc_ui_pointer_event_type type;
    float x;
    float y;
    int32_t button;
    int32_t modifiers;
    float wheel_x;
    float wheel_y;
} tc_ui_pointer_event;

typedef enum tc_ui_key_event_type {
    TC_UI_KEY_DOWN = 0,
    TC_UI_KEY_UP = 1
} tc_ui_key_event_type;

typedef enum tc_ui_key_code {
    TC_UI_KEY_UNKNOWN = 0,
    TC_UI_KEY_BACKSPACE = 8,
    TC_UI_KEY_TAB = 9,
    TC_UI_KEY_ENTER = 13,
    TC_UI_KEY_ESCAPE = 27,
    TC_UI_KEY_DELETE = 127,
    TC_UI_KEY_LEFT = 1000,
    TC_UI_KEY_RIGHT = 1001,
    TC_UI_KEY_HOME = 1002,
    TC_UI_KEY_END = 1003
} tc_ui_key_code;

typedef struct tc_ui_key_event {
    tc_ui_key_event_type type;
    int32_t key;
    int32_t scancode;
    int32_t modifiers;
    bool repeat;
} tc_ui_key_event;

typedef struct tc_ui_text_event {
    const char* text;
} tc_ui_text_event;

typedef void (*tc_widget_deleter)(tc_widget* widget);

typedef struct tc_widget_vtable {
    const char* type_name;

    tc_ui_size (*measure)(
        tc_widget* widget,
        tc_ui_document* document,
        tc_ui_constraints constraints
    );
    void (*layout)(
        tc_widget* widget,
        tc_ui_document* document,
        tc_ui_rect rect
    );
    void (*paint)(
        tc_widget* widget,
        tc_ui_document* document,
        tc_ui_paint_context* context
    );
    tc_ui_event_result (*pointer_event)(
        tc_widget* widget,
        tc_ui_document* document,
        const tc_ui_pointer_event* event
    );
    tc_widget_handle (*hit_test)(
        tc_widget* widget,
        tc_ui_document* document,
        float x,
        float y
    );
    tc_ui_event_result (*key_event)(
        tc_widget* widget,
        tc_ui_document* document,
        const tc_ui_key_event* event
    );
    tc_ui_event_result (*text_event)(
        tc_widget* widget,
        tc_ui_document* document,
        const tc_ui_text_event* event
    );

    void (*on_destroy)(tc_widget* widget, tc_ui_document* document);
} tc_widget_vtable;

struct tc_widget {
    const tc_widget_vtable* vtable;
    tc_widget_deleter deleter;
    tc_ui_document* document;
    tc_widget_handle handle;
    tc_language native_language;
    void* body;

    tc_widget* parent;
    tc_widget** children;
    size_t child_count;
    size_t child_capacity;

    tc_ui_rect bounds;
    tc_ui_size min_size;
    tc_ui_size preferred_size;
    tc_ui_size max_size;

    const char* stable_id;
    const char* name;
    const char* debug_name;
    uint32_t flags;
};

TERMIN_GUI_NATIVE_API tc_widget_handle tc_widget_handle_invalid_value(void);
TERMIN_GUI_NATIVE_API bool tc_widget_handle_valid_value(tc_widget_handle handle);

TERMIN_GUI_NATIVE_API void tc_widget_init(
    tc_widget* widget,
    const tc_widget_vtable* vtable,
    tc_widget_deleter deleter,
    tc_language native_language,
    void* body
);

TERMIN_GUI_NATIVE_API void tc_widget_set_focusable(tc_widget* widget, bool focusable);
TERMIN_GUI_NATIVE_API bool tc_widget_is_focusable(const tc_widget* widget);
TERMIN_GUI_NATIVE_API void tc_widget_set_visible(tc_widget* widget, bool visible);
TERMIN_GUI_NATIVE_API bool tc_widget_is_visible(const tc_widget* widget);
TERMIN_GUI_NATIVE_API void tc_widget_set_enabled(tc_widget* widget, bool enabled);
TERMIN_GUI_NATIVE_API bool tc_widget_is_enabled(const tc_widget* widget);
TERMIN_GUI_NATIVE_API void tc_widget_set_mouse_transparent(tc_widget* widget, bool mouse_transparent);
TERMIN_GUI_NATIVE_API bool tc_widget_is_mouse_transparent(const tc_widget* widget);
TERMIN_GUI_NATIVE_API tc_ui_rect tc_widget_bounds(const tc_widget* widget);
TERMIN_GUI_NATIVE_API void tc_widget_set_bounds(tc_widget* widget, tc_ui_rect bounds);
TERMIN_GUI_NATIVE_API tc_ui_size tc_widget_min_size(const tc_widget* widget);
TERMIN_GUI_NATIVE_API void tc_widget_set_min_size(tc_widget* widget, tc_ui_size size);
TERMIN_GUI_NATIVE_API tc_ui_size tc_widget_preferred_size(const tc_widget* widget);
TERMIN_GUI_NATIVE_API void tc_widget_set_preferred_size(tc_widget* widget, tc_ui_size size);
TERMIN_GUI_NATIVE_API tc_ui_size tc_widget_max_size(const tc_widget* widget);
TERMIN_GUI_NATIVE_API void tc_widget_set_max_size(tc_widget* widget, tc_ui_size size);
TERMIN_GUI_NATIVE_API tc_widget* tc_widget_parent(tc_widget* widget);
TERMIN_GUI_NATIVE_API const tc_widget* tc_widget_parent_const(const tc_widget* widget);
TERMIN_GUI_NATIVE_API size_t tc_widget_child_count(const tc_widget* widget);
TERMIN_GUI_NATIVE_API tc_widget* tc_widget_child_at(tc_widget* widget, size_t index);
TERMIN_GUI_NATIVE_API const tc_widget* tc_widget_child_at_const(const tc_widget* widget, size_t index);
TERMIN_GUI_NATIVE_API bool tc_widget_append_child(tc_widget* parent, tc_widget* child);
TERMIN_GUI_NATIVE_API bool tc_widget_insert_child(tc_widget* parent, size_t index, tc_widget* child);
TERMIN_GUI_NATIVE_API bool tc_widget_remove_child(tc_widget* parent, tc_widget* child);
TERMIN_GUI_NATIVE_API bool tc_widget_detach(tc_widget* widget);
TERMIN_GUI_NATIVE_API const char* tc_widget_stable_id(const tc_widget* widget);
TERMIN_GUI_NATIVE_API const char* tc_widget_name(const tc_widget* widget);
TERMIN_GUI_NATIVE_API const char* tc_widget_debug_name(const tc_widget* widget);
TERMIN_GUI_NATIVE_API void tc_widget_mark_dirty(tc_widget* widget, uint32_t dirty_flags);
TERMIN_GUI_NATIVE_API void tc_widget_clear_dirty(tc_widget* widget, uint32_t dirty_flags);
TERMIN_GUI_NATIVE_API uint32_t tc_widget_dirty_flags(const tc_widget* widget);
TERMIN_GUI_NATIVE_API bool tc_widget_has_dirty_flags(const tc_widget* widget, uint32_t dirty_flags);

TERMIN_GUI_NATIVE_API tc_ui_document* tc_ui_document_create(void);
TERMIN_GUI_NATIVE_API void tc_ui_document_destroy(tc_ui_document* document);

TERMIN_GUI_NATIVE_API tc_widget_handle tc_ui_document_adopt_widget(
    tc_ui_document* document,
    tc_widget* widget
);

TERMIN_GUI_NATIVE_API bool tc_ui_document_is_alive(
    const tc_ui_document* document,
    tc_widget_handle handle
);

TERMIN_GUI_NATIVE_API tc_widget* tc_ui_document_resolve_widget(
    tc_ui_document* document,
    tc_widget_handle handle
);

TERMIN_GUI_NATIVE_API const tc_widget* tc_ui_document_resolve_widget_const(
    const tc_ui_document* document,
    tc_widget_handle handle
);

TERMIN_GUI_NATIVE_API bool tc_ui_document_destroy_widget(
    tc_ui_document* document,
    tc_widget_handle handle
);

TERMIN_GUI_NATIVE_API bool tc_ui_document_destroy_widget_recursive(
    tc_ui_document* document,
    tc_widget_handle handle
);

TERMIN_GUI_NATIVE_API size_t tc_ui_document_live_widget_count(
    const tc_ui_document* document
);

TERMIN_GUI_NATIVE_API void tc_ui_document_set_text_measurer(
    tc_ui_document* document,
    tc_ui_text_measure_fn measure,
    void* user_data
);

TERMIN_GUI_NATIVE_API bool tc_ui_document_measure_text(
    tc_ui_document* document,
    const char* text_utf8,
    size_t text_byte_length,
    float font_size,
    tc_ui_text_metrics* out_metrics
);

TERMIN_GUI_NATIVE_API bool tc_ui_document_add_root(
    tc_ui_document* document,
    tc_widget_handle handle
);

TERMIN_GUI_NATIVE_API bool tc_ui_document_remove_root(
    tc_ui_document* document,
    tc_widget_handle handle
);

TERMIN_GUI_NATIVE_API size_t tc_ui_document_root_count(
    const tc_ui_document* document
);

TERMIN_GUI_NATIVE_API tc_widget_handle tc_ui_document_root_at(
    const tc_ui_document* document,
    size_t index
);

TERMIN_GUI_NATIVE_API void tc_ui_document_paint_roots(
    tc_ui_document* document,
    tc_ui_paint_context* context
);

TERMIN_GUI_NATIVE_API void tc_ui_document_layout_roots(
    tc_ui_document* document,
    tc_ui_rect rect
);

TERMIN_GUI_NATIVE_API tc_ui_event_result tc_ui_document_dispatch_pointer_event(
    tc_ui_document* document,
    const tc_ui_pointer_event* event
);

TERMIN_GUI_NATIVE_API tc_widget_handle tc_ui_document_hit_test(
    tc_ui_document* document,
    float x,
    float y
);

TERMIN_GUI_NATIVE_API tc_widget_handle tc_ui_document_hovered_widget(
    const tc_ui_document* document
);

TERMIN_GUI_NATIVE_API tc_widget_handle tc_ui_document_pointer_capture(
    const tc_ui_document* document
);

TERMIN_GUI_NATIVE_API bool tc_ui_document_set_pointer_capture(
    tc_ui_document* document,
    tc_widget_handle handle
);

TERMIN_GUI_NATIVE_API bool tc_ui_document_release_pointer_capture(
    tc_ui_document* document,
    tc_widget_handle handle
);

TERMIN_GUI_NATIVE_API tc_widget_handle tc_ui_document_focused_widget(
    const tc_ui_document* document
);

TERMIN_GUI_NATIVE_API bool tc_ui_document_set_focus(
    tc_ui_document* document,
    tc_widget_handle handle
);

TERMIN_GUI_NATIVE_API bool tc_ui_document_clear_focus(
    tc_ui_document* document,
    tc_widget_handle handle
);

TERMIN_GUI_NATIVE_API tc_ui_event_result tc_ui_document_dispatch_key_event(
    tc_ui_document* document,
    const tc_ui_key_event* event
);

TERMIN_GUI_NATIVE_API tc_ui_event_result tc_ui_document_dispatch_text_event(
    tc_ui_document* document,
    const tc_ui_text_event* event
);

TERMIN_GUI_NATIVE_API tc_ui_draw_list* tc_ui_draw_list_create(void);
TERMIN_GUI_NATIVE_API void tc_ui_draw_list_destroy(tc_ui_draw_list* draw_list);
TERMIN_GUI_NATIVE_API void tc_ui_draw_list_clear(tc_ui_draw_list* draw_list);
TERMIN_GUI_NATIVE_API size_t tc_ui_draw_list_command_count(const tc_ui_draw_list* draw_list);
TERMIN_GUI_NATIVE_API const tc_ui_draw_command* tc_ui_draw_list_command_at(
    const tc_ui_draw_list* draw_list,
    size_t index
);

TERMIN_GUI_NATIVE_API tc_ui_paint_context* tc_ui_paint_context_create(
    tc_ui_draw_list* draw_list
);
TERMIN_GUI_NATIVE_API void tc_ui_paint_context_destroy(tc_ui_paint_context* context);
TERMIN_GUI_NATIVE_API tc_ui_draw_list* tc_ui_paint_context_draw_list(
    tc_ui_paint_context* context
);

TERMIN_GUI_NATIVE_API void tc_ui_painter_fill_rect(
    tc_ui_paint_context* context,
    tc_ui_rect rect,
    tc_ui_color color
);
TERMIN_GUI_NATIVE_API void tc_ui_painter_stroke_rect(
    tc_ui_paint_context* context,
    tc_ui_rect rect,
    tc_ui_color color,
    float thickness
);
TERMIN_GUI_NATIVE_API void tc_ui_painter_draw_line(
    tc_ui_paint_context* context,
    tc_ui_point p0,
    tc_ui_point p1,
    tc_ui_color color,
    float thickness
);
TERMIN_GUI_NATIVE_API void tc_ui_painter_draw_text(
    tc_ui_paint_context* context,
    const char* text,
    tc_ui_point position,
    float font_size,
    tc_ui_color color
);
TERMIN_GUI_NATIVE_API void tc_ui_painter_push_clip(
    tc_ui_paint_context* context,
    tc_ui_rect rect
);
TERMIN_GUI_NATIVE_API void tc_ui_painter_pop_clip(tc_ui_paint_context* context);

#ifdef __cplusplus
}
#endif

#endif // TERMIN_GUI_NATIVE_TC_UI_DOCUMENT_H
