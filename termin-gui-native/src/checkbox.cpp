#include "widgets_internal.hpp"

namespace termin::gui_native {
using namespace detail;

Checkbox::Checkbox(bool checked)
    : NativeWidget("Checkbox"), checked_(checked) {
    set_style_role(TC_UI_STYLE_CHECKBOX);
    set_cursor_intent(TC_UI_CURSOR_HAND);
    set_preferred_size(tc_ui_size {18.0f, 18.0f});
}

void Checkbox::set_checked(bool checked) {
    if (checked_ == checked) {
        return;
    }
    checked_ = checked;
    mark_dirty(TC_WIDGET_DIRTY_STATE | TC_WIDGET_DIRTY_PAINT);
    changed_.emit(*this, checked_);
}

void Checkbox::paint(tc_ui_document_handle document, tc_ui_paint_context* context) {
    const tc_ui_style style = computed_style(
        document,
        checked_ ? TC_UI_STYLE_STATE_CHECKED : 0
    );
    const float side = std::min(bounds().width, bounds().height);
    const tc_ui_rect box {
        bounds().x + (bounds().width - side) * 0.5f,
        bounds().y + (bounds().height - side) * 0.5f,
        side,
        side
    };
    tc_ui_painter_fill_rounded_rect(context, box, style.corner_radius, style.background);
    if (style.border_width > 0.0f && color_visible(style.border)) {
        tc_ui_painter_stroke_rounded_rect(
            context, box, style.corner_radius, style.border, style.border_width);
    }
    if (checked_) {
        tc_ui_painter_draw_line(
            context,
            tc_ui_point {box.x + side * 0.22f, box.y + side * 0.55f},
            tc_ui_point {box.x + side * 0.43f, box.y + side * 0.74f},
            style.accent,
            2.0f
        );
        tc_ui_painter_draw_line(
            context,
            tc_ui_point {box.x + side * 0.43f, box.y + side * 0.74f},
            tc_ui_point {box.x + side * 0.78f, box.y + side * 0.26f},
            style.accent,
            2.0f
        );
    }
}

tc_ui_event_result Checkbox::pointer_event(tc_ui_document_handle document, const tc_ui_pointer_event* event) {
    if (!event) {
        return TC_UI_EVENT_IGNORED;
    }
    const bool captured = tc_widget_handle_eq(tc_ui_document_pointer_capture(document), handle());
    if (event->type == TC_UI_POINTER_DOWN && rect_contains(bounds(), event->x, event->y)) {
        pressed_ = true;
        tc_ui_document_set_pointer_capture(document, handle());
        mark_dirty(TC_WIDGET_DIRTY_STATE | TC_WIDGET_DIRTY_PAINT);
        return TC_UI_EVENT_HANDLED;
    }
    if (event->type == TC_UI_POINTER_UP && (pressed_ || captured)) {
        const bool activate = pressed_ && rect_contains(bounds(), event->x, event->y);
        pressed_ = false;
        if (captured) {
            tc_ui_document_release_pointer_capture(document, handle());
        }
        mark_dirty(TC_WIDGET_DIRTY_STATE | TC_WIDGET_DIRTY_PAINT);
        if (activate) {
            set_checked(!checked_);
        }
        return TC_UI_EVENT_HANDLED;
    }
    if (event->type == TC_UI_POINTER_MOVE && captured) {
        return TC_UI_EVENT_HANDLED;
    }
    return TC_UI_EVENT_IGNORED;
}


} // namespace termin::gui_native
