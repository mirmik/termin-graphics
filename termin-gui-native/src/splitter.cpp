#include "widgets_internal.hpp"

namespace termin::gui_native {
using namespace detail;

Splitter::Splitter(Orientation orientation, const char* debug_name)
    : NativeWidget(debug_name ? debug_name : "Splitter"),
      orientation_(orientation) {
    set_style_role(TC_UI_STYLE_SEPARATOR);
    set_preferred_size(orientation_ == Orientation::Horizontal
        ? tc_ui_size {320.0f, 180.0f}
        : tc_ui_size {240.0f, 260.0f});
}

void Splitter::set_first(tc_widget_handle handle) {
    if (tc_widget_handle_is_invalid(handle)) {
        tc_log_error("[termin-gui-native] cannot set invalid Splitter first handle");
        return;
    }
    const tc_widget_handle previous = this->first();
    if (tc_widget_handle_eq(previous, handle)) {
        return;
    }
    if (tc_widget_handle_eq(this->second(), handle)) {
        tc_log_error("[termin-gui-native] Splitter first and second widgets must be distinct");
        return;
    }
    if (!attach_child(c_widget(), handle, 0, "Splitter::set_first")) {
        return;
    }
    detach_if_child(c_widget(), previous);
    mark_dirty(TC_WIDGET_DIRTY_LAYOUT | TC_WIDGET_DIRTY_PAINT);
}

void Splitter::set_second(tc_widget_handle handle) {
    if (tc_widget_handle_is_invalid(handle)) {
        tc_log_error("[termin-gui-native] cannot set invalid Splitter second handle");
        return;
    }
    const tc_widget_handle previous = this->second();
    if (tc_widget_handle_eq(previous, handle)) {
        return;
    }
    if (tc_widget_handle_eq(this->first(), handle)) {
        tc_log_error("[termin-gui-native] Splitter first and second widgets must be distinct");
        return;
    }
    if (!attach_child(c_widget(), handle, 1, "Splitter::set_second")) {
        return;
    }
    detach_if_child(c_widget(), previous);
    mark_dirty(TC_WIDGET_DIRTY_LAYOUT | TC_WIDGET_DIRTY_PAINT);
}

Splitter& Splitter::set_split_fraction(float fraction) {
    split_fraction_ = clamp_float(fraction, 0.05f, 0.95f);
    mark_dirty(TC_WIDGET_DIRTY_LAYOUT | TC_WIDGET_DIRTY_PAINT | TC_WIDGET_DIRTY_STATE);
    return *this;
}

Splitter& Splitter::set_min_extents(float first_min, float second_min) {
    first_min_extent_ = std::max(0.0f, first_min);
    second_min_extent_ = std::max(0.0f, second_min);
    mark_dirty(TC_WIDGET_DIRTY_LAYOUT | TC_WIDGET_DIRTY_PAINT);
    return *this;
}

Splitter& Splitter::set_divider_thickness(float thickness) {
    divider_thickness_ = std::max(1.0f, thickness);
    mark_dirty(TC_WIDGET_DIRTY_LAYOUT | TC_WIDGET_DIRTY_PAINT);
    return *this;
}

tc_ui_size Splitter::measure(tc_ui_document* document, tc_ui_constraints constraints) {
    tc_ui_size first_size {0.0f, 0.0f};
    tc_ui_size second_size {0.0f, 0.0f};
    if (!tc_widget_handle_is_invalid(this->first())) {
        if (tc_widget* first = resolve_child(document, c_widget(), this->first(), "Splitter::measure(first)")) {
            first_size = measure_widget(first, document, unconstrained());
        }
    }
    if (!tc_widget_handle_is_invalid(this->second())) {
        if (tc_widget* second = resolve_child(document, c_widget(), this->second(), "Splitter::measure(second)")) {
            second_size = measure_widget(second, document, unconstrained());
        }
    }

    tc_ui_size measured {};
    if (orientation_ == Orientation::Horizontal) {
        measured.width = first_size.width + second_size.width + divider_thickness_;
        measured.height = std::max(first_size.height, second_size.height);
    } else {
        measured.width = std::max(first_size.width, second_size.width);
        measured.height = first_size.height + second_size.height + divider_thickness_;
    }
    measured.width = std::max({measured.width, preferred_size().width, min_size().width});
    measured.height = std::max({measured.height, preferred_size().height, min_size().height});
    return clamp_size(measured, constraints);
}

void Splitter::layout(tc_ui_document* document, tc_ui_rect rect) {
    NativeWidget::layout(document, rect);
    layout_children(document);
}

void Splitter::paint(tc_ui_document* document, tc_ui_paint_context* context) {
    tc_ui_painter_push_clip(context, bounds());
    if (!tc_widget_handle_is_invalid(this->first())) {
        tc_widget* first = resolve_child(document, c_widget(), this->first(), "Splitter::paint(first)");
        paint_widget(first, document, context);
    }
    if (!tc_widget_handle_is_invalid(this->second())) {
        tc_widget* second = resolve_child(document, c_widget(), this->second(), "Splitter::paint(second)");
        paint_widget(second, document, context);
    }
    tc_ui_painter_pop_clip(context);
    const bool active = tc_widget_handle_eq(
        tc_ui_document_pointer_capture(document), handle());
    const tc_ui_style style = computed_style(
        document, active ? TC_UI_STYLE_STATE_HOVERED : 0);
    tc_ui_painter_fill_rect(
        context,
        divider_line_rect(style.border_width),
        style.background);
}

tc_ui_event_result Splitter::pointer_event(tc_ui_document* document, const tc_ui_pointer_event* event) {
    if (!event) {
        return TC_UI_EVENT_IGNORED;
    }
    const bool captured = tc_widget_handle_eq(tc_ui_document_pointer_capture(document), handle());
    if ((event->type == TC_UI_POINTER_ENTER || event->type == TC_UI_POINTER_LEAVE) && !captured) {
        mark_dirty(TC_WIDGET_DIRTY_PAINT | TC_WIDGET_DIRTY_STATE);
        return TC_UI_EVENT_HANDLED;
    }
    if (!captured && !rect_contains(bounds(), event->x, event->y)) {
        return TC_UI_EVENT_IGNORED;
    }
    if (event->type == TC_UI_POINTER_DOWN && rect_contains(divider_hit_rect(), event->x, event->y)) {
        tc_ui_document_set_pointer_capture(document, handle());
        mark_dirty(TC_WIDGET_DIRTY_PAINT | TC_WIDGET_DIRTY_STATE);
        return TC_UI_EVENT_HANDLED;
    }
    if (event->type == TC_UI_POINTER_MOVE && captured) {
        const float axis = split_axis_extent();
        if (axis > 0.0f) {
            const float position = orientation_ == Orientation::Horizontal
                ? event->x - bounds().x
                : event->y - bounds().y;
            set_split_fraction(position / axis);
            layout_children(document);
        }
        return TC_UI_EVENT_HANDLED;
    }
    if (event->type == TC_UI_POINTER_UP && captured) {
        tc_ui_document_release_pointer_capture(document, handle());
        mark_dirty(TC_WIDGET_DIRTY_PAINT | TC_WIDGET_DIRTY_STATE);
        return TC_UI_EVENT_HANDLED;
    }

    return TC_UI_EVENT_IGNORED;
}

tc_widget_handle Splitter::hit_test(tc_ui_document* document, float x, float y) {
    const bool over_divider = rect_contains(divider_hit_rect(), x, y);
    const bool captured = tc_widget_handle_eq(
        tc_ui_document_pointer_capture(document), handle());
    c_widget()->cursor_intent = over_divider || captured
        ? (orientation_ == Orientation::Horizontal
               ? TC_UI_CURSOR_RESIZE_HORIZONTAL
               : TC_UI_CURSOR_RESIZE_VERTICAL)
        : TC_UI_CURSOR_INHERIT;
    if (!rect_contains(bounds(), x, y)) {
        return tc_widget_handle_invalid();
    }
    if (over_divider) {
        return mouse_transparent() ? tc_widget_handle_invalid() : handle();
    }
    auto hit_child = [this, document, x, y](tc_widget_handle handle) {
        tc_widget* child = resolve_child(document, c_widget(), handle, "Splitter::hit_test");
        if (!child || !child->vtable || !child->vtable->hit_test) {
            return tc_widget_handle_invalid();
        }
        return child->vtable->hit_test(child, document, x, y);
    };
    tc_widget_handle second_hit = hit_child(this->second());
    if (!tc_widget_handle_is_invalid(second_hit)) {
        return second_hit;
    }
    tc_widget_handle first_hit = hit_child(this->first());
    if (!tc_widget_handle_is_invalid(first_hit)) {
        return first_hit;
    }
    return mouse_transparent() ? tc_widget_handle_invalid() : handle();
}

tc_ui_rect Splitter::divider_rect() const {
    const float axis = split_axis_extent();
    if (orientation_ == Orientation::Horizontal) {
        const float first_extent = clamp_float(
            axis * split_fraction_,
            first_min_extent_,
            std::max(first_min_extent_, axis - second_min_extent_)
        );
        return tc_ui_rect {bounds().x + first_extent, bounds().y, divider_thickness_, bounds().height};
    }
    const float first_extent = clamp_float(
        axis * split_fraction_,
        first_min_extent_,
        std::max(first_min_extent_, axis - second_min_extent_)
    );
    return tc_ui_rect {bounds().x, bounds().y + first_extent, bounds().width, divider_thickness_};
}

tc_ui_rect Splitter::divider_hit_rect() const {
    const tc_ui_rect divider = divider_rect();
    const float expansion = std::max(
        0.0f, (divider_hit_thickness_ - divider_thickness_) * 0.5f);
    if (orientation_ == Orientation::Horizontal) {
        const float x = std::max(bounds().x, divider.x - expansion);
        const float right = std::min(
            bounds().x + bounds().width,
            divider.x + divider.width + expansion);
        return tc_ui_rect {x, divider.y, std::max(0.0f, right - x), divider.height};
    }
    const float y = std::max(bounds().y, divider.y - expansion);
    const float bottom = std::min(
        bounds().y + bounds().height,
        divider.y + divider.height + expansion);
    return tc_ui_rect {divider.x, y, divider.width, std::max(0.0f, bottom - y)};
}

tc_ui_rect Splitter::divider_line_rect(float line_thickness) const {
    const tc_ui_rect divider = divider_rect();
    const float thickness = clamp_float(line_thickness, 1.0f, divider_thickness_);
    if (orientation_ == Orientation::Horizontal) {
        return tc_ui_rect {
            divider.x + (divider.width - thickness) * 0.5f,
            divider.y,
            thickness,
            divider.height};
    }
    return tc_ui_rect {
        divider.x,
        divider.y + (divider.height - thickness) * 0.5f,
        divider.width,
        thickness};
}

void Splitter::layout_children(tc_ui_document* document) {
    const float axis = split_axis_extent();
    const float first_extent = clamp_float(
        axis * split_fraction_,
        first_min_extent_,
        std::max(first_min_extent_, axis - second_min_extent_)
    );
    const float second_extent = std::max(0.0f, axis - first_extent);
    if (!tc_widget_handle_is_invalid(this->first())) {
        tc_widget* first = resolve_child(document, c_widget(), this->first(), "Splitter::layout(first)");
        tc_ui_rect first_rect = orientation_ == Orientation::Horizontal
            ? tc_ui_rect {bounds().x, bounds().y, first_extent, bounds().height}
            : tc_ui_rect {bounds().x, bounds().y, bounds().width, first_extent};
        layout_widget(first, document, first_rect);
    }
    if (!tc_widget_handle_is_invalid(this->second())) {
        tc_widget* second = resolve_child(document, c_widget(), this->second(), "Splitter::layout(second)");
        tc_ui_rect second_rect = orientation_ == Orientation::Horizontal
            ? tc_ui_rect {
                bounds().x + first_extent + divider_thickness_,
                bounds().y,
                second_extent,
                bounds().height
            }
            : tc_ui_rect {
                bounds().x,
                bounds().y + first_extent + divider_thickness_,
                bounds().width,
                second_extent
            };
        layout_widget(second, document, second_rect);
    }
}

float Splitter::split_axis_extent() const {
    const float axis = orientation_ == Orientation::Horizontal ? bounds().width : bounds().height;
    return std::max(0.0f, axis - divider_thickness_);
}


} // namespace termin::gui_native
