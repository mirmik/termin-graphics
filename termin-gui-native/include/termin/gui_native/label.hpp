#pragma once

#include <string>

#include <termin/gui_native/native_widget.hpp>
#include <termin/gui_native/widget_types.hpp>

namespace termin::gui_native {
class Label : public NativeWidget {
private:
    std::string text_;

public:
    explicit Label(std::string text);
    Label(std::string text, float font_size);
    Label(std::string text, float font_size, Color color);
    const std::string& text() const { return text_; }
    Label& set_text(std::string text);
    Label& set_color(Color color);
    Label& set_font_size(float font_size);
    tc_ui_size measure(tc_ui_document* document, tc_ui_constraints constraints) override;
    void paint(tc_ui_document* document, tc_ui_paint_context* context) override;
private:
    void update_unmeasured_size();
};
} // namespace termin::gui_native
