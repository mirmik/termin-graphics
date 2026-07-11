#pragma once

#include <string>

#include <termin/gui_native/native_widget.hpp>
#include <termin/gui_native/signal.hpp>

namespace termin::gui_native {
class SliderEdit : public NativeWidget {
private:
    float value_ = 0.0f;
    float min_value_ = 0.0f;
    float max_value_ = 1.0f;
    float step_ = 0.0f;
    int decimals_ = 2;
    float spacing_ = 4.0f;
    float spin_box_width_ = 80.0f;
    std::string label_;
    tc_widget_handle slider_handle_ = tc_widget_handle_invalid();
    tc_widget_handle spin_box_handle_ = tc_widget_handle_invalid();
    size_t slider_connection_ = 0;
    size_t spin_box_connection_ = 0;
    bool syncing_ = false;
    Signal<SliderEdit&, float> changed_;

public:
    explicit SliderEdit(float value = 0.0f);
    float value() const { return value_; }
    void set_value(float value);
    void set_range(float min_value, float max_value);
    void set_step(float step);
    void set_decimals(int decimals);
    void set_label(std::string label);
    const std::string& label() const { return label_; }
    tc_widget_handle slider_handle() const { return slider_handle_; }
    tc_widget_handle spin_box_handle() const { return spin_box_handle_; }
    Signal<SliderEdit&, float>& changed() { return changed_; }
    const Signal<SliderEdit&, float>& changed() const { return changed_; }
    tc_ui_size measure(tc_ui_document* document, tc_ui_constraints constraints) override;
    void layout(tc_ui_document* document, tc_ui_rect rect) override;
    void paint(tc_ui_document* document, tc_ui_paint_context* context) override;
    void on_destroy(tc_ui_document* document) override;
private:
    bool ensure_children(tc_ui_document* document);
    void sync_children(tc_ui_document* document);
};
} // namespace termin::gui_native
