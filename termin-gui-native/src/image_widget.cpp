#include "widgets_internal.hpp"

namespace termin::gui_native {
using namespace detail;

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

void ImageWidget::clear_texture() {
    if (texture_id_ == 0) {
        return;
    }
    texture_id_ = 0;
    // Do not reset intrinsic_size_: an image preview must keep its allocated
    // slot while the producer is waiting for the next frame.
    mark_dirty(TC_WIDGET_DIRTY_PAINT);
}

void ImageWidget::set_tint(Color tint) {
    tint_ = tint;
    mark_dirty(TC_WIDGET_DIRTY_PAINT);
}

void ImageWidget::set_fit(ImageFit fit) {
    if (fit_ == fit) return;
    fit_ = fit;
    mark_dirty(TC_WIDGET_DIRTY_PAINT);
}

tc_ui_size ImageWidget::measure(tc_ui_document*, tc_ui_constraints constraints) {
    return clamp_size(intrinsic_size_, constraints);
}

void ImageWidget::paint(tc_ui_document*, tc_ui_paint_context* context) {
    if (texture_id_ == 0) return;
    tc_ui_rect destination = bounds();
    if (fit_ != ImageFit::Stretch && intrinsic_size_.width > 0.0f && intrinsic_size_.height > 0.0f) {
        const float width_scale = bounds().width / intrinsic_size_.width;
        const float height_scale = bounds().height / intrinsic_size_.height;
        const float scale = fit_ == ImageFit::Cover
            ? std::max(width_scale, height_scale)
            : std::min(width_scale, height_scale);
        destination.width = intrinsic_size_.width * scale;
        destination.height = intrinsic_size_.height * scale;
        destination.x += (bounds().width - destination.width) * 0.5f;
        destination.y += (bounds().height - destination.height) * 0.5f;
    }
    tc_ui_painter_push_clip(context, bounds());
    tc_ui_painter_draw_texture(context, texture_id_, destination, tint_.c_color(),
                               TC_UI_TEXTURE_SAMPLING_LINEAR, false);
    tc_ui_painter_pop_clip(context);
}


} // namespace termin::gui_native
