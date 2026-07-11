#pragma once

#include <termin/gui_native/native_widget.hpp>

namespace termin::gui_native {
class ProgressBar : public NativeWidget {
private:
    float value_ = 0.0f;

public:
    explicit ProgressBar(float value = 0.0f);
    void set_value(float value);
    float value() const { return value_; }
    void paint(tc_ui_document* document, tc_ui_paint_context* context) override;
};
} // namespace termin::gui_native
