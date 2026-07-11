#pragma once

#include <termin/gui_native/native_widget.hpp>
#include <termin/gui_native/signal.hpp>

namespace termin::gui_native {
class Slider : public NativeWidget {
private:
    float value_ = 0.0f;
    float min_value_ = 0.0f;
    float max_value_ = 1.0f;
    float step_ = 0.0f;
    bool dragging_ = false;
    Signal<Slider&, float> changed_;

public:
    explicit Slider(float value = 0.0f);
    void set_value(float value);
    float value() const { return value_; }
    float min_value() const { return min_value_; }
    float max_value() const { return max_value_; }
    float step() const { return step_; }
    void set_range(float min_value, float max_value);
    void set_step(float step);
    Signal<Slider&, float>& changed() { return changed_; }
    const Signal<Slider&, float>& changed() const { return changed_; }
    void paint(tc_ui_document* document, tc_ui_paint_context* context) override;
    tc_ui_event_result pointer_event(tc_ui_document* document, const tc_ui_pointer_event* event) override;
private:
};
} // namespace termin::gui_native
