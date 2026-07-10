#include "widgets_internal.hpp"

namespace termin::gui_native {
using namespace detail;

Canvas::Canvas()
    : NativeWidget("Canvas") {
    set_style_role(TC_UI_STYLE_PANEL);
    set_focusable(true);
    set_preferred_size(tc_ui_size {320.0f, 240.0f});
}

void Canvas::set_texture(uint32_t texture_id, tc_ui_size image_size) {
    texture_id_ = texture_id;
    image_size_ = image_size;
    mark_dirty(TC_WIDGET_DIRTY_STATE | TC_WIDGET_DIRTY_PAINT);
}

void Canvas::set_overlay_texture(uint32_t texture_id) {
    overlay_texture_id_ = texture_id;
    mark_dirty(TC_WIDGET_DIRTY_PAINT);
}

void Canvas::set_paint_callback(PaintCallback callback) {
    paint_callback_ = std::move(callback);
    mark_dirty(TC_WIDGET_DIRTY_PAINT);
}

void Canvas::set_zoom(float zoom, tc_ui_point anchor) {
    const tc_ui_point image_anchor = widget_to_image(anchor);
    const float next = clamp_float(zoom, min_zoom_, max_zoom_);
    if (std::fabs(next - zoom_) <= 0.000001f) return;
    zoom_ = next;
    offset_.x = anchor.x - bounds().x - image_anchor.x * zoom_;
    offset_.y = anchor.y - bounds().y - image_anchor.y * zoom_;
    mark_dirty(TC_WIDGET_DIRTY_STATE | TC_WIDGET_DIRTY_PAINT);
    zoom_changed_.emit(*this, zoom_);
}

void Canvas::fit_in_view() {
    if (image_size_.width <= 0.0f || image_size_.height <= 0.0f || bounds().width <= 0.0f || bounds().height <= 0.0f) return;
    zoom_ = std::min(bounds().width / image_size_.width, bounds().height / image_size_.height) * 0.95f;
    zoom_ = clamp_float(zoom_, min_zoom_, max_zoom_);
    offset_.x = (bounds().width - image_size_.width * zoom_) * 0.5f;
    offset_.y = (bounds().height - image_size_.height * zoom_) * 0.5f;
    mark_dirty(TC_WIDGET_DIRTY_STATE | TC_WIDGET_DIRTY_PAINT);
    zoom_changed_.emit(*this, zoom_);
}

tc_ui_point Canvas::widget_to_image(tc_ui_point point) const {
    return tc_ui_point {
        (point.x - bounds().x - offset_.x) / zoom_,
        (point.y - bounds().y - offset_.y) / zoom_
    };
}

tc_ui_point Canvas::image_to_widget(tc_ui_point point) const {
    return tc_ui_point {
        bounds().x + offset_.x + point.x * zoom_,
        bounds().y + offset_.y + point.y * zoom_
    };
}

void Canvas::layout(tc_ui_document* document, tc_ui_rect rect) {
    const bool first_layout = bounds().width <= 0.0f || bounds().height <= 0.0f;
    NativeWidget::layout(document, rect);
    if (first_layout && image_size_.width > 0.0f && image_size_.height > 0.0f) fit_in_view();
}

void Canvas::paint(tc_ui_document* document, tc_ui_paint_context* context) {
    const tc_ui_style style = computed_style(document);
    tc_ui_painter_fill_rect(context, bounds(), style.background);
    tc_ui_painter_push_clip(context, bounds());
    if (texture_id_ != 0 && image_size_.width > 0.0f && image_size_.height > 0.0f) {
        const tc_ui_rect destination {
            bounds().x + offset_.x,
            bounds().y + offset_.y,
            image_size_.width * zoom_,
            image_size_.height * zoom_
        };
        tc_ui_painter_draw_texture(context, texture_id_, destination, tc_ui_color {1, 1, 1, 1}, false);
        if (overlay_texture_id_ != 0) {
            tc_ui_painter_draw_texture(context, overlay_texture_id_, destination, tc_ui_color {1, 1, 1, 1}, false);
        }
    }
    if (paint_callback_) paint_callback_(*this, context);
    tc_ui_painter_pop_clip(context);
}

tc_ui_event_result Canvas::pointer_event(tc_ui_document* document, const tc_ui_pointer_event* event) {
    if (!event) return TC_UI_EVENT_IGNORED;
    const bool captured = tc_widget_handle_eq(tc_ui_document_pointer_capture(document), handle());
    if (event->type == TC_UI_POINTER_WHEEL && rect_contains(bounds(), event->x, event->y) && image_size_.width > 0.0f) {
        const float factor = event->wheel_y > 0.0f ? 1.15f : 1.0f / 1.15f;
        set_zoom(zoom_ * factor, tc_ui_point {event->x, event->y});
        return TC_UI_EVENT_HANDLED;
    }
    if (event->type == TC_UI_POINTER_DOWN && event->button == 2 && rect_contains(bounds(), event->x, event->y)) {
        panning_ = true;
        pan_start_ = tc_ui_point {event->x, event->y};
        pan_start_offset_ = offset_;
        tc_ui_document_set_pointer_capture(document, handle());
        return TC_UI_EVENT_HANDLED;
    }
    if (event->type == TC_UI_POINTER_MOVE && (panning_ || captured)) {
        offset_.x = pan_start_offset_.x + event->x - pan_start_.x;
        offset_.y = pan_start_offset_.y + event->y - pan_start_.y;
        mark_dirty(TC_WIDGET_DIRTY_STATE | TC_WIDGET_DIRTY_PAINT);
        return TC_UI_EVENT_HANDLED;
    }
    if (event->type == TC_UI_POINTER_UP && (panning_ || captured)) {
        panning_ = false;
        tc_ui_document_release_pointer_capture(document, handle());
        return TC_UI_EVENT_HANDLED;
    }
    if (rect_contains(bounds(), event->x, event->y)) {
        const bool has_listeners = pointer_input_.size() > 0;
        pointer_input_.emit(*this, widget_to_image(tc_ui_point {event->x, event->y}), *event);
        return has_listeners ? TC_UI_EVENT_HANDLED : TC_UI_EVENT_IGNORED;
    }
    return TC_UI_EVENT_IGNORED;
}


} // namespace termin::gui_native
