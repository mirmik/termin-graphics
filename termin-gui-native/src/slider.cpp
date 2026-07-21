#include "widgets_internal.hpp"

namespace termin::gui_native {
using namespace detail;

Slider::Slider(float value)
    : NativeWidget("Slider") {
    set_style_role(TC_UI_STYLE_SLIDER);
    set_preferred_size(tc_ui_size {140.0f, 28.0f});
    set_value(value);
}

void Slider::set_value(float value) {
    float next = clamp_float(value, min_value_, max_value_);
    if (step_ > 0.0f) {
        next = min_value_ + std::round((next - min_value_) / step_) * step_;
        next = clamp_float(next, min_value_, max_value_);
    }
    if (std::fabs(next - value_) <= 0.0001f) {
        return;
    }
    value_ = next;
    mark_dirty(TC_WIDGET_DIRTY_STATE | TC_WIDGET_DIRTY_PAINT);
    changed_.emit(*this, value_);
}

void Slider::set_range(float min_value, float max_value) {
    if (!std::isfinite(min_value) || !std::isfinite(max_value) || max_value < min_value) {
        tc_log_error("[termin-gui-native] Slider rejected invalid range");
        return;
    }
    min_value_ = min_value;
    max_value_ = max_value;
    set_value(value_);
    mark_dirty(TC_WIDGET_DIRTY_STATE | TC_WIDGET_DIRTY_PAINT);
}

void Slider::set_step(float step) {
    if (!std::isfinite(step) || step < 0.0f) {
        tc_log_error("[termin-gui-native] Slider rejected invalid step");
        return;
    }
    step_ = step;
    set_value(value_);
}

void Slider::paint(tc_ui_document_handle document, tc_ui_paint_context* context) {
    const tc_ui_style style = computed_style(document);
    const float center_y = bounds().y + bounds().height * 0.5f;
    const float left = bounds().x + 10.0f;
    const float right = bounds().x + bounds().width - 10.0f;
    const float range = max_value_ - min_value_;
    const float ratio = range > 0.0f ? (value_ - min_value_) / range : 0.0f;
    const float knob_x = left + (right - left) * ratio;
    tc_ui_painter_draw_line(
        context,
        tc_ui_point {left, center_y},
        tc_ui_point {right, center_y},
        style.border,
        4.0f
    );
    tc_ui_painter_draw_line(
        context,
        tc_ui_point {left, center_y},
        tc_ui_point {knob_x, center_y},
        style.accent,
        4.0f
    );
    tc_ui_painter_fill_rect(
        context,
        tc_ui_rect {knob_x - 5.0f, center_y - 10.0f, 10.0f, 20.0f},
        style.foreground
    );
}

tc_ui_event_result Slider::pointer_event(tc_ui_document_handle document, const tc_ui_pointer_event* event) {
    if (!event) {
        return TC_UI_EVENT_IGNORED;
    }
    const bool captured = tc_widget_handle_eq(tc_ui_document_pointer_capture(document), handle());
    if (event->type == TC_UI_POINTER_DOWN && rect_contains(bounds(), event->x, event->y)) {
        dragging_ = true;
        tc_ui_document_set_pointer_capture(document, handle());
        mark_dirty(TC_WIDGET_DIRTY_STATE | TC_WIDGET_DIRTY_PAINT);
    } else if (event->type == TC_UI_POINTER_DOWN) {
        return TC_UI_EVENT_IGNORED;
    } else if (event->type == TC_UI_POINTER_MOVE && !(dragging_ || captured)) {
        return TC_UI_EVENT_IGNORED;
    } else if (event->type == TC_UI_POINTER_UP && (dragging_ || captured)) {
        dragging_ = false;
        if (captured) {
            tc_ui_document_release_pointer_capture(document, handle());
        }
        mark_dirty(TC_WIDGET_DIRTY_STATE | TC_WIDGET_DIRTY_PAINT);
        return TC_UI_EVENT_HANDLED;
    } else if (event->type != TC_UI_POINTER_DOWN && event->type != TC_UI_POINTER_MOVE) {
        return TC_UI_EVENT_IGNORED;
    }
    const float left = bounds().x + 10.0f;
    const float right = bounds().x + bounds().width - 10.0f;
    if (right <= left) {
        return TC_UI_EVENT_HANDLED;
    }
    const float ratio = clamp_float((event->x - left) / (right - left), 0.0f, 1.0f);
    set_value(min_value_ + ratio * (max_value_ - min_value_));
    return TC_UI_EVENT_HANDLED;
}


} // namespace termin::gui_native
