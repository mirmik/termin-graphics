#pragma once

#include <cstdint>
#include <functional>

#include <termin/gui_native/native_widget.hpp>
#include <termin/gui_native/signal.hpp>

namespace termin::gui_native {
class Canvas : public NativeWidget {
public:
    using PaintCallback = std::function<void(Canvas&, tc_ui_paint_context*)>;
private:
    uint32_t texture_id_ = 0;
    uint32_t overlay_texture_id_ = 0;
    tc_ui_texture_sampling texture_sampling_ = TC_UI_TEXTURE_SAMPLING_LINEAR;
    tc_ui_size image_size_ {};
    float zoom_ = 1.0f;
    float min_zoom_ = 0.01f;
    float max_zoom_ = 100.0f;
    tc_ui_point offset_ {};
    bool fit_mode_ = true;
    bool panning_ = false;
    tc_ui_point pan_start_ {};
    tc_ui_point pan_start_offset_ {};
    PaintCallback paint_callback_;
    Signal<Canvas&, float> zoom_changed_;
    Signal<Canvas&, tc_ui_point, const tc_ui_pointer_event&> pointer_input_;
public:
    Canvas();
    void set_texture(uint32_t texture_id, tc_ui_size image_size = {});
    void clear_texture();
    void set_overlay_texture(uint32_t texture_id);
    uint32_t texture_id() const { return texture_id_; }
    uint32_t overlay_texture_id() const { return overlay_texture_id_; }
    tc_ui_size image_size() const { return image_size_; }
    tc_ui_texture_sampling texture_sampling() const { return texture_sampling_; }
    void set_texture_sampling(tc_ui_texture_sampling sampling);
    void set_paint_callback(PaintCallback callback);
    float zoom() const { return zoom_; }
    bool fit_mode() const { return fit_mode_; }
    void set_zoom(float zoom, tc_ui_point anchor);
    void fit_in_view();
    tc_ui_point widget_to_image(tc_ui_point point) const;
    tc_ui_point image_to_widget(tc_ui_point point) const;
    Signal<Canvas&, float>& zoom_changed() { return zoom_changed_; }
    Signal<Canvas&, tc_ui_point, const tc_ui_pointer_event&>& pointer_input() { return pointer_input_; }
    void layout(tc_ui_document_handle document, tc_ui_rect rect) override;
    void paint(tc_ui_document_handle document, tc_ui_paint_context* context) override;
    tc_ui_event_result pointer_event(tc_ui_document_handle document, const tc_ui_pointer_event* event) override;
};
} // namespace termin::gui_native
