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

typedef struct tc_ui_pointer_event {
    tc_ui_pointer_event_type type;
    float x;
    float y;
    int32_t button;
    int32_t modifiers;
    float wheel_x;
    float wheel_y;
} tc_ui_pointer_event;

typedef void (*tc_widget_deleter)(tc_widget* widget);
typedef void (*tc_widget_visit_fn)(void* user_data, tc_widget_handle handle);

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

    // Explicit recursive-destroy policy hook. This is not a generic child list:
    // it is consulted only by tc_ui_document_destroy_widget_recursive().
    void (*visit_recursive_destroy_targets)(
        tc_widget* widget,
        tc_ui_document* document,
        void* user_data,
        tc_widget_visit_fn visit
    );

    void (*on_destroy)(tc_widget* widget, tc_ui_document* document);
} tc_widget_vtable;

struct tc_widget {
    const tc_widget_vtable* vtable;
    tc_widget_deleter deleter;
    tc_widget_handle handle;
    tc_language native_language;
    void* body;
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
