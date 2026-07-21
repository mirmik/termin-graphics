#pragma once

#include <termin/gui_native/native_widget.hpp>
#include <termin/gui_native/widget_types.hpp>

namespace termin::gui_native {
class Separator : public NativeWidget {
private:
    Orientation orientation_ = Orientation::Horizontal;

public:
    explicit Separator(Orientation orientation = Orientation::Horizontal);
    Separator& set_color(Color color);
    Separator& set_thickness(float thickness);
    void paint(tc_ui_document_handle document, tc_ui_paint_context* context) override;
};
} // namespace termin::gui_native
