#include "widgets_internal.hpp"

namespace termin::gui_native {
using namespace detail;
IconButton::IconButton(std::string icon)
    : NativeWidget("IconButton"), icon_(std::move(icon)) {
    set_style_role(TC_UI_STYLE_BUTTON);
    set_preferred_size(tc_ui_size {28.0f, 28.0f});
}

void IconButton::set_icon(std::string icon) {
    if (!valid_utf8(icon)) {
        tc_log_error("[termin-gui-native] IconButton rejected invalid UTF-8 icon");
        return;
    }
    icon_ = std::move(icon);
    mark_dirty(TC_WIDGET_DIRTY_PAINT);
}

void IconButton::set_texture(uint32_t texture_id) {
    texture_id_ = texture_id;
    mark_dirty(TC_WIDGET_DIRTY_PAINT);
}

void IconButton::set_active(bool active) {
    active_ = active;
    mark_dirty(TC_WIDGET_DIRTY_STATE | TC_WIDGET_DIRTY_PAINT);
}

void IconButton::paint(tc_ui_document* document, tc_ui_paint_context* context) {
    const uint32_t extra = (active_ ? TC_UI_STYLE_STATE_CHECKED : 0) |
        (pressed_ ? TC_UI_STYLE_STATE_PRESSED : 0);
    const tc_ui_style style = computed_style(document, extra);
    tc_ui_painter_fill_rounded_rect(context, bounds(), 4.0f, style.background);
    if (texture_id_ != 0) {
        const float inset = 5.0f;
        tc_ui_painter_draw_texture(context, texture_id_, tc_ui_rect {bounds().x + inset, bounds().y + inset, bounds().width - inset * 2.0f, bounds().height - inset * 2.0f}, style.foreground, false);
    } else if (!icon_.empty()) {
        tc_ui_text_metrics metrics {};
        measure_text(document, icon_, style.font_size, metrics);
        tc_ui_painter_draw_text(context, icon_.c_str(), tc_ui_point {bounds().x + (bounds().width - metrics.width) * 0.5f, bounds().y + bounds().height * 0.68f}, style.font_size, style.foreground);
    }
}

tc_ui_event_result IconButton::pointer_event(tc_ui_document*, const tc_ui_pointer_event* event) {
    if (!event) return TC_UI_EVENT_IGNORED;
    if (event->type == TC_UI_POINTER_DOWN && rect_contains(bounds(), event->x, event->y)) {
        pressed_ = true;
        mark_dirty(TC_WIDGET_DIRTY_STATE | TC_WIDGET_DIRTY_PAINT);
        return TC_UI_EVENT_HANDLED;
    }
    if (event->type == TC_UI_POINTER_UP && pressed_) {
        pressed_ = false;
        mark_dirty(TC_WIDGET_DIRTY_STATE | TC_WIDGET_DIRTY_PAINT);
        if (rect_contains(bounds(), event->x, event->y)) clicked_.emit(*this);
        return TC_UI_EVENT_HANDLED;
    }
    return TC_UI_EVENT_IGNORED;
}

ImageWidget::ImageWidget()
    : NativeWidget("ImageWidget") {
    set_preferred_size(intrinsic_size_);
}

void ImageWidget::set_texture(uint32_t texture_id, tc_ui_size intrinsic_size) {
    texture_id_ = texture_id;
    if (intrinsic_size.width > 0.0f && intrinsic_size.height > 0.0f) {
        intrinsic_size_ = intrinsic_size;
        set_preferred_size(intrinsic_size_);
    }
    mark_dirty(TC_WIDGET_DIRTY_LAYOUT | TC_WIDGET_DIRTY_PAINT);
}

void ImageWidget::set_tint(Color tint) {
    tint_ = tint;
    mark_dirty(TC_WIDGET_DIRTY_PAINT);
}

tc_ui_size ImageWidget::measure(tc_ui_document*, tc_ui_constraints constraints) {
    return clamp_size(intrinsic_size_, constraints);
}

void ImageWidget::paint(tc_ui_document*, tc_ui_paint_context* context) {
    if (texture_id_ == 0) return;
    tc_ui_rect destination = bounds();
    if (preserve_aspect_ && intrinsic_size_.width > 0.0f && intrinsic_size_.height > 0.0f) {
        const float scale = std::min(bounds().width / intrinsic_size_.width, bounds().height / intrinsic_size_.height);
        destination.width = intrinsic_size_.width * scale;
        destination.height = intrinsic_size_.height * scale;
        destination.x += (bounds().width - destination.width) * 0.5f;
        destination.y += (bounds().height - destination.height) * 0.5f;
    }
    tc_ui_painter_draw_texture(context, texture_id_, destination, tint_.c_color(), false);
}

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

Swatch::Swatch(Color color)
    : NativeWidget("Swatch"), color_(color) {
    set_preferred_size(tc_ui_size {36.0f, 36.0f});
}

void Swatch::paint(tc_ui_document*, tc_ui_paint_context* context) {
    tc_ui_painter_fill_rect(context, bounds(), color_.c_color());
    tc_ui_painter_stroke_rect(context, bounds(), tc_ui_color {0.88f, 0.90f, 0.94f, 1.0f}, 1.0f);
}

Spacer::Spacer(tc_ui_size size)
    : NativeWidget("Spacer") {
    set_preferred_size(size);
}

} // namespace termin::gui_native

