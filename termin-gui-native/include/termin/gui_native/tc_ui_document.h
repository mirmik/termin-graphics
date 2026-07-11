#ifndef TERMIN_GUI_NATIVE_TC_UI_DOCUMENT_H
#define TERMIN_GUI_NATIVE_TC_UI_DOCUMENT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <inspect/tc_runtime_type_registry.h>

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

typedef enum tc_ui_style_role {
    TC_UI_STYLE_GENERIC = 0,
    TC_UI_STYLE_PANEL = 1,
    TC_UI_STYLE_LABEL = 2,
    TC_UI_STYLE_BUTTON = 3,
    TC_UI_STYLE_TEXT_INPUT = 4,
    TC_UI_STYLE_GROUP_BOX = 5,
    TC_UI_STYLE_TAB = 6,
    TC_UI_STYLE_CHECKBOX = 7,
    TC_UI_STYLE_PROGRESS = 8,
    TC_UI_STYLE_SLIDER = 9,
    TC_UI_STYLE_SEPARATOR = 10,
    TC_UI_STYLE_ROLE_COUNT = 11
} tc_ui_style_role;

typedef enum tc_ui_font_role {
    TC_UI_FONT_BODY = 0,
    TC_UI_FONT_SMALL = 1,
    TC_UI_FONT_TITLE = 2,
    TC_UI_FONT_MONOSPACE = 3
} tc_ui_font_role;

typedef enum tc_ui_style_state_flag {
    TC_UI_STYLE_STATE_HOVERED = 1u << 0,
    TC_UI_STYLE_STATE_PRESSED = 1u << 1,
    TC_UI_STYLE_STATE_FOCUSED = 1u << 2,
    TC_UI_STYLE_STATE_DISABLED = 1u << 3,
    TC_UI_STYLE_STATE_CHECKED = 1u << 4
} tc_ui_style_state_flag;

typedef uint64_t tc_ui_style_field_mask;

enum {
    TC_UI_STYLE_BACKGROUND = 1ull << 0,
    TC_UI_STYLE_FOREGROUND = 1ull << 1,
    TC_UI_STYLE_BORDER = 1ull << 2,
    TC_UI_STYLE_ACCENT = 1ull << 3,
    TC_UI_STYLE_PADDING_LEFT = 1ull << 4,
    TC_UI_STYLE_PADDING_TOP = 1ull << 5,
    TC_UI_STYLE_PADDING_RIGHT = 1ull << 6,
    TC_UI_STYLE_PADDING_BOTTOM = 1ull << 7,
    TC_UI_STYLE_SPACING = 1ull << 8,
    TC_UI_STYLE_BORDER_WIDTH = 1ull << 9,
    TC_UI_STYLE_FONT_SIZE = 1ull << 10,
    TC_UI_STYLE_MIN_WIDTH = 1ull << 11,
    TC_UI_STYLE_MIN_HEIGHT = 1ull << 12,
    TC_UI_STYLE_FONT_ROLE = 1ull << 13,
    TC_UI_STYLE_CORNER_RADIUS = 1ull << 14,
    TC_UI_STYLE_ALL_FIELDS = (1ull << 15) - 1ull
};

typedef enum tc_ui_style_override_flag {
    TC_UI_STYLE_OVERRIDE_INHERIT = 1u << 0
} tc_ui_style_override_flag;

typedef struct tc_ui_style {
    tc_ui_color background;
    tc_ui_color foreground;
    tc_ui_color border;
    tc_ui_color accent;
    float padding_left;
    float padding_top;
    float padding_right;
    float padding_bottom;
    float spacing;
    float border_width;
    float font_size;
    float min_width;
    float min_height;
    float corner_radius;
    tc_ui_font_role font_role;
} tc_ui_style;

typedef struct tc_ui_style_override {
    tc_ui_style value;
    tc_ui_style_field_mask fields;
    uint32_t flags;
} tc_ui_style_override;

typedef struct tc_ui_role_style {
    tc_ui_style base;
    tc_ui_style_override hovered;
    tc_ui_style_override pressed;
    tc_ui_style_override focused;
    tc_ui_style_override disabled;
    tc_ui_style_override checked;
} tc_ui_role_style;

typedef struct tc_ui_theme {
    tc_ui_role_style roles[TC_UI_STYLE_ROLE_COUNT];
} tc_ui_theme;

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

typedef const char* (*tc_ui_clipboard_get_text_fn)(void* user_data);
typedef bool (*tc_ui_clipboard_set_text_fn)(
    void* user_data,
    const char* text_utf8,
    size_t text_byte_length
);

typedef enum tc_ui_draw_command_type {
    TC_UI_DRAW_FILL_RECT = 0,
    TC_UI_DRAW_STROKE_RECT = 1,
    TC_UI_DRAW_LINE = 2,
    TC_UI_DRAW_PUSH_CLIP = 3,
    TC_UI_DRAW_POP_CLIP = 4,
    TC_UI_DRAW_TEXT = 5,
    TC_UI_DRAW_FILL_ROUNDED_RECT = 6,
    TC_UI_DRAW_STROKE_ROUNDED_RECT = 7,
    TC_UI_DRAW_FILL_CIRCLE = 8,
    TC_UI_DRAW_STROKE_CIRCLE = 9,
    TC_UI_DRAW_ARC = 10,
    TC_UI_DRAW_POLYLINE = 11,
    TC_UI_DRAW_TEXTURE = 12
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
    float radius;
    float start_radians;
    float end_radians;
    int32_t segments;
    const tc_ui_point* points;
    size_t point_count;
    uint32_t texture_id;
    bool flip_v;
} tc_ui_draw_command;

typedef enum tc_ui_event_result {
    TC_UI_EVENT_IGNORED = 0,
    TC_UI_EVENT_HANDLED = 1
} tc_ui_event_result;

typedef enum tc_ui_pointer_event_type {
    TC_UI_POINTER_MOVE = 0,
    TC_UI_POINTER_DOWN = 1,
    TC_UI_POINTER_UP = 2,
    TC_UI_POINTER_WHEEL = 3,
    TC_UI_POINTER_ENTER = 4,
    TC_UI_POINTER_LEAVE = 5
} tc_ui_pointer_event_type;

typedef enum tc_ui_modifier_flag {
    TC_UI_MOD_SHIFT = 1u << 0,
    TC_UI_MOD_CTRL = 1u << 1,
    TC_UI_MOD_ALT = 1u << 2,
    TC_UI_MOD_SUPER = 1u << 3
} tc_ui_modifier_flag;

typedef enum tc_ui_overlay_flag {
    TC_UI_OVERLAY_MODAL = 1u << 0,
    TC_UI_OVERLAY_DISMISS_ON_OUTSIDE = 1u << 1,
    TC_UI_OVERLAY_POINTER_TRANSPARENT = 1u << 2,
    TC_UI_OVERLAY_TOOLTIP = 1u << 3,
    TC_UI_OVERLAY_ALLOW_ROOT_HIT = 1u << 4
} tc_ui_overlay_flag;

typedef enum tc_ui_overlay_dismiss_reason {
    TC_UI_OVERLAY_DISMISS_PROGRAMMATIC = 0,
    TC_UI_OVERLAY_DISMISS_OUTSIDE = 1,
    TC_UI_OVERLAY_DISMISS_ESCAPE = 2
} tc_ui_overlay_dismiss_reason;

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
    uint32_t click_count;
    int32_t modifiers;
    float wheel_x;
    float wheel_y;
} tc_ui_pointer_event;

typedef enum tc_ui_key_event_type {
    TC_UI_KEY_DOWN = 0,
    TC_UI_KEY_UP = 1
} tc_ui_key_event_type;

typedef enum tc_ui_key_code {
    TC_UI_KEY_UNKNOWN = -1,
    TC_UI_KEY_TAB = 9,
    TC_UI_KEY_ENTER = 13,
    TC_UI_KEY_SPACE = 32,
    TC_UI_KEY_0 = 48,
    TC_UI_KEY_1 = 49,
    TC_UI_KEY_2 = 50,
    TC_UI_KEY_3 = 51,
    TC_UI_KEY_4 = 52,
    TC_UI_KEY_5 = 53,
    TC_UI_KEY_6 = 54,
    TC_UI_KEY_7 = 55,
    TC_UI_KEY_8 = 56,
    TC_UI_KEY_9 = 57,
    TC_UI_KEY_A = 65,
    TC_UI_KEY_B = 66,
    TC_UI_KEY_C = 67,
    TC_UI_KEY_D = 68,
    TC_UI_KEY_E = 69,
    TC_UI_KEY_F = 70,
    TC_UI_KEY_G = 71,
    TC_UI_KEY_H = 72,
    TC_UI_KEY_I = 73,
    TC_UI_KEY_J = 74,
    TC_UI_KEY_K = 75,
    TC_UI_KEY_L = 76,
    TC_UI_KEY_M = 77,
    TC_UI_KEY_N = 78,
    TC_UI_KEY_O = 79,
    TC_UI_KEY_P = 80,
    TC_UI_KEY_Q = 81,
    TC_UI_KEY_R = 82,
    TC_UI_KEY_S = 83,
    TC_UI_KEY_T = 84,
    TC_UI_KEY_U = 85,
    TC_UI_KEY_V = 86,
    TC_UI_KEY_W = 87,
    TC_UI_KEY_X = 88,
    TC_UI_KEY_Y = 89,
    TC_UI_KEY_Z = 90,
    TC_UI_KEY_ESCAPE = 256,
    TC_UI_KEY_BACKSPACE = 259,
    TC_UI_KEY_DELETE = 261,
    TC_UI_KEY_RIGHT = 262,
    TC_UI_KEY_LEFT = 263,
    TC_UI_KEY_DOWN_ARROW = 264,
    TC_UI_KEY_UP_ARROW = 265,
    TC_UI_KEY_HOME = 268,
    TC_UI_KEY_END = 269,
    TC_UI_KEY_F1 = 290,
    TC_UI_KEY_F2 = 291,
    TC_UI_KEY_F3 = 292,
    TC_UI_KEY_F4 = 293,
    TC_UI_KEY_F5 = 294,
    TC_UI_KEY_F6 = 295,
    TC_UI_KEY_F7 = 296,
    TC_UI_KEY_F8 = 297,
    TC_UI_KEY_F9 = 298,
    TC_UI_KEY_F10 = 299,
    TC_UI_KEY_F11 = 300,
    TC_UI_KEY_F12 = 301
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

typedef enum tc_widget_ownership_policy {
    TC_WIDGET_OWNED = 0,
    TC_WIDGET_BORROWED = 1
} tc_widget_ownership_policy;

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
    void (*focus_event)(
        tc_widget* widget,
        tc_ui_document* document,
        bool focused
    );
    void (*overlay_dismissed)(
        tc_widget* widget,
        tc_ui_document* document,
        tc_ui_overlay_dismiss_reason reason
    );

    void (*on_destroy)(tc_widget* widget, tc_ui_document* document);
} tc_widget_vtable;

struct tc_widget {
    const tc_widget_vtable* vtable;
    tc_widget_deleter deleter;
    tc_ui_document* document;
    tc_widget_handle handle;
    tc_language native_language;
    tc_widget_ownership_policy ownership_policy;
    void* body;
    tc_runtime_type_instance_link runtime_type_link;

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
    char* owned_stable_id;
    char* owned_name;
    char* owned_debug_name;
    uint32_t flags;
    tc_ui_style_role style_role;
    tc_ui_style_override style_override;
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
TERMIN_GUI_NATIVE_API bool tc_widget_set_stable_id(tc_widget* widget, const char* stable_id);
TERMIN_GUI_NATIVE_API bool tc_widget_set_name(tc_widget* widget, const char* name);
TERMIN_GUI_NATIVE_API bool tc_widget_set_debug_name(tc_widget* widget, const char* debug_name);
TERMIN_GUI_NATIVE_API const char* tc_widget_type_name(const tc_widget* widget);
TERMIN_GUI_NATIVE_API tc_widget_ownership_policy tc_widget_ownership(
    const tc_widget* widget
);
TERMIN_GUI_NATIVE_API void tc_widget_mark_dirty(tc_widget* widget, uint32_t dirty_flags);
TERMIN_GUI_NATIVE_API void tc_widget_clear_dirty(tc_widget* widget, uint32_t dirty_flags);
TERMIN_GUI_NATIVE_API uint32_t tc_widget_dirty_flags(const tc_widget* widget);
TERMIN_GUI_NATIVE_API bool tc_widget_has_dirty_flags(const tc_widget* widget, uint32_t dirty_flags);
TERMIN_GUI_NATIVE_API void tc_widget_set_style_role(tc_widget* widget, tc_ui_style_role role);
TERMIN_GUI_NATIVE_API tc_ui_style_role tc_widget_style_role(const tc_widget* widget);
TERMIN_GUI_NATIVE_API bool tc_widget_set_style_override(
    tc_widget* widget,
    const tc_ui_style_override* style_override
);
TERMIN_GUI_NATIVE_API void tc_widget_clear_style_override(tc_widget* widget);
TERMIN_GUI_NATIVE_API tc_ui_style_override tc_widget_style_override(const tc_widget* widget);

TERMIN_GUI_NATIVE_API tc_ui_document* tc_ui_document_create(void);
TERMIN_GUI_NATIVE_API void tc_ui_document_destroy(tc_ui_document* document);
TERMIN_GUI_NATIVE_API void tc_ui_theme_init_default(tc_ui_theme* theme);
TERMIN_GUI_NATIVE_API const tc_ui_theme* tc_ui_document_theme(const tc_ui_document* document);
TERMIN_GUI_NATIVE_API bool tc_ui_document_set_theme(
    tc_ui_document* document,
    const tc_ui_theme* theme
);
TERMIN_GUI_NATIVE_API uint64_t tc_ui_document_theme_revision(const tc_ui_document* document);
TERMIN_GUI_NATIVE_API uint32_t tc_ui_document_widget_style_state(
    const tc_ui_document* document,
    const tc_widget* widget
);
TERMIN_GUI_NATIVE_API bool tc_ui_document_resolve_style(
    const tc_ui_document* document,
    const tc_widget* widget,
    uint32_t extra_state_flags,
    tc_ui_style* out_style
);

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

TERMIN_GUI_NATIVE_API void tc_ui_document_set_clipboard(
    tc_ui_document* document,
    tc_ui_clipboard_get_text_fn get_text,
    tc_ui_clipboard_set_text_fn set_text,
    void* user_data
);

TERMIN_GUI_NATIVE_API const char* tc_ui_document_clipboard_text(tc_ui_document* document);
TERMIN_GUI_NATIVE_API bool tc_ui_document_set_clipboard_text(
    tc_ui_document* document,
    const char* text_utf8,
    size_t text_byte_length
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

TERMIN_GUI_NATIVE_API void tc_ui_document_paint(
    tc_ui_document* document,
    tc_ui_paint_context* context
);

TERMIN_GUI_NATIVE_API void tc_ui_document_layout_roots(
    tc_ui_document* document,
    tc_ui_rect rect
);

TERMIN_GUI_NATIVE_API bool tc_ui_document_show_overlay(
    tc_ui_document* document,
    tc_widget_handle handle,
    uint32_t flags
);

TERMIN_GUI_NATIVE_API bool tc_ui_document_dismiss_overlay(
    tc_ui_document* document,
    tc_widget_handle handle,
    tc_ui_overlay_dismiss_reason reason
);

TERMIN_GUI_NATIVE_API size_t tc_ui_document_overlay_count(const tc_ui_document* document);
TERMIN_GUI_NATIVE_API tc_widget_handle tc_ui_document_overlay_at(
    const tc_ui_document* document,
    size_t index
);
TERMIN_GUI_NATIVE_API uint32_t tc_ui_document_overlay_flags_at(
    const tc_ui_document* document,
    size_t index
);

TERMIN_GUI_NATIVE_API tc_ui_rect tc_ui_tooltip_rect(
    tc_ui_rect viewport,
    tc_ui_point anchor,
    tc_ui_size preferred_size,
    tc_ui_point offset,
    float margin
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

TERMIN_GUI_NATIVE_API tc_widget_handle tc_ui_document_pressed_widget(
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

TERMIN_GUI_NATIVE_API bool tc_ui_document_focus_next(tc_ui_document* document);
TERMIN_GUI_NATIVE_API bool tc_ui_document_focus_previous(tc_ui_document* document);

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
TERMIN_GUI_NATIVE_API void tc_ui_painter_fill_rounded_rect(
    tc_ui_paint_context* context,
    tc_ui_rect rect,
    float radius,
    tc_ui_color color
);
TERMIN_GUI_NATIVE_API void tc_ui_painter_stroke_rect(
    tc_ui_paint_context* context,
    tc_ui_rect rect,
    tc_ui_color color,
    float thickness
);
TERMIN_GUI_NATIVE_API void tc_ui_painter_stroke_rounded_rect(
    tc_ui_paint_context* context,
    tc_ui_rect rect,
    float radius,
    tc_ui_color color,
    float thickness
);
TERMIN_GUI_NATIVE_API void tc_ui_painter_fill_circle(
    tc_ui_paint_context* context,
    tc_ui_point center,
    float radius,
    tc_ui_color color,
    int32_t segments
);
TERMIN_GUI_NATIVE_API void tc_ui_painter_stroke_circle(
    tc_ui_paint_context* context,
    tc_ui_point center,
    float radius,
    tc_ui_color color,
    float thickness,
    int32_t segments
);
TERMIN_GUI_NATIVE_API void tc_ui_painter_draw_arc(
    tc_ui_paint_context* context,
    tc_ui_point center,
    float radius,
    float start_radians,
    float end_radians,
    tc_ui_color color,
    float thickness,
    int32_t segments
);
TERMIN_GUI_NATIVE_API void tc_ui_painter_draw_line(
    tc_ui_paint_context* context,
    tc_ui_point p0,
    tc_ui_point p1,
    tc_ui_color color,
    float thickness
);
TERMIN_GUI_NATIVE_API void tc_ui_painter_draw_polyline(
    tc_ui_paint_context* context,
    const tc_ui_point* points,
    size_t point_count,
    tc_ui_color color,
    float thickness
);
TERMIN_GUI_NATIVE_API void tc_ui_painter_draw_texture(
    tc_ui_paint_context* context,
    uint32_t texture_id,
    tc_ui_rect rect,
    tc_ui_color tint,
    bool flip_v
);
TERMIN_GUI_NATIVE_API void tc_ui_painter_draw_text(
    tc_ui_paint_context* context,
    const char* text,
    // The position is the left end of the text baseline, not its top-left corner.
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
