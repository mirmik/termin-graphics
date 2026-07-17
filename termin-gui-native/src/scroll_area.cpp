#include "widgets_internal.hpp"

namespace termin::gui_native {
using namespace detail;

ScrollArea::ScrollArea(const char* debug_name)
    : NativeWidget(debug_name ? debug_name : "ScrollArea") {
    set_preferred_size(tc_ui_size {240.0f, 180.0f});
}

void ScrollArea::set_content(tc_widget_handle handle) {
    if (tc_widget_handle_is_invalid(handle)) {
        tc_log_error("[termin-gui-native] cannot set invalid ScrollArea content handle");
        return;
    }
    const tc_widget_handle previous = this->content();
    if (tc_widget_handle_eq(previous, handle)) {
        return;
    }
    if (!attach_child(c_widget(), handle, 0, "ScrollArea::set_content")) {
        return;
    }
    detach_if_child(c_widget(), previous);
    mark_dirty(TC_WIDGET_DIRTY_LAYOUT | TC_WIDGET_DIRTY_PAINT);
}

void ScrollArea::set_scroll_axes(bool horizontal, bool vertical) {
    horizontal_scroll_enabled_ = horizontal;
    vertical_scroll_enabled_ = vertical;
    if (!horizontal_scroll_enabled_) {
        scroll_x_ = 0.0f;
    }
    if (!vertical_scroll_enabled_) {
        scroll_y_ = 0.0f;
    }
    clamp_scroll();
    mark_dirty(TC_WIDGET_DIRTY_LAYOUT | TC_WIDGET_DIRTY_PAINT | TC_WIDGET_DIRTY_STATE);
}

void ScrollArea::set_scroll(float x, float y) {
    scroll_x_ = horizontal_scroll_enabled_ ? std::max(0.0f, x) : 0.0f;
    scroll_y_ = vertical_scroll_enabled_ ? std::max(0.0f, y) : 0.0f;
    clamp_scroll();
    mark_dirty(TC_WIDGET_DIRTY_STATE | TC_WIDGET_DIRTY_PAINT);
}

tc_ui_size ScrollArea::measure(tc_ui_document* document, tc_ui_constraints constraints) {
    tc_ui_size measured = preferred_size();
    if (!tc_widget_handle_is_invalid(this->content())) {
        if (tc_widget* content = resolve_child(document, c_widget(), this->content(), "ScrollArea::measure")) {
            tc_ui_size content_size = measure_widget(content, document, unconstrained());
            measured.width = std::max(measured.width, std::min(content_size.width, preferred_size().width));
            measured.height = std::max(measured.height, std::min(content_size.height, preferred_size().height));
        }
    }
    measured.width = std::max(measured.width, min_size().width);
    measured.height = std::max(measured.height, min_size().height);
    return clamp_size(measured, constraints);
}

void ScrollArea::layout(tc_ui_document* document, tc_ui_rect rect) {
    NativeWidget::layout(document, rect);
    content_size_ = tc_ui_size {0.0f, 0.0f};
    if (tc_widget_handle_is_invalid(this->content())) {
        return;
    }
    tc_widget* content = resolve_child(document, c_widget(), this->content(), "ScrollArea::layout");
    if (!content) {
        return;
    }

    tc_ui_size measured = measure_widget(content, document, unconstrained());
    content_size_ = tc_ui_size {
        horizontal_scroll_enabled_ ? std::max(measured.width, rect.width) : rect.width,
        vertical_scroll_enabled_ ? std::max(measured.height, rect.height) : rect.height
    };
    clamp_scroll();
    layout_widget(
        content,
        document,
        tc_ui_rect {rect.x - scroll_x_, rect.y - scroll_y_, content_size_.width, content_size_.height}
    );
}

void ScrollArea::paint(tc_ui_document* document, tc_ui_paint_context* context) {
    tc_ui_painter_push_clip(context, bounds());
    if (!tc_widget_handle_is_invalid(this->content())) {
        tc_widget* content = resolve_child(document, c_widget(), this->content(), "ScrollArea::paint");
        paint_widget(content, document, context);
    }
    tc_ui_painter_pop_clip(context);
}

tc_ui_event_result ScrollArea::pointer_event(tc_ui_document* document, const tc_ui_pointer_event* event) {
    if (!event || !rect_contains(bounds(), event->x, event->y)) {
        return TC_UI_EVENT_IGNORED;
    }
    if (event->type == TC_UI_POINTER_WHEEL) {
        const float delta_y = vertical_scroll_enabled_ && event->wheel_y != 0.0f
            ? -event->wheel_y * wheel_step_ : 0.0f;
        const float delta_x = horizontal_scroll_enabled_ && event->wheel_x != 0.0f
            ? -event->wheel_x * wheel_step_ : 0.0f;
        set_scroll(scroll_x_ + delta_x, scroll_y_ + delta_y);
        if (!tc_widget_handle_is_invalid(this->content())) {
            if (tc_widget* content = resolve_child(document, c_widget(), this->content(), "ScrollArea::wheel")) {
                layout_widget(
                    content,
                    document,
                    tc_ui_rect {
                        bounds().x - scroll_x_,
                        bounds().y - scroll_y_,
                        content_size_.width,
                        content_size_.height
                    }
                );
            }
        }
        return TC_UI_EVENT_HANDLED;
    }
    return TC_UI_EVENT_IGNORED;
}

tc_widget_handle ScrollArea::hit_test(tc_ui_document* document, float x, float y) {
    if (!rect_contains(bounds(), x, y)) {
        return tc_widget_handle_invalid();
    }
    if (!tc_widget_handle_is_invalid(this->content())) {
        tc_widget* content = resolve_child(document, c_widget(), this->content(), "ScrollArea::hit_test");
        if (content && content->vtable && content->vtable->hit_test) {
            tc_widget_handle hit = content->vtable->hit_test(content, document, x, y);
            if (!tc_widget_handle_is_invalid(hit)) {
                return hit;
            }
        }
    }
    return mouse_transparent() ? tc_widget_handle_invalid() : handle();
}

void ScrollArea::clamp_scroll() {
    scroll_x_ = horizontal_scroll_enabled_
        ? clamp_float(scroll_x_, 0.0f, std::max(0.0f, content_size_.width - bounds().width))
        : 0.0f;
    scroll_y_ = vertical_scroll_enabled_
        ? clamp_float(scroll_y_, 0.0f, std::max(0.0f, content_size_.height - bounds().height))
        : 0.0f;
}


} // namespace termin::gui_native
