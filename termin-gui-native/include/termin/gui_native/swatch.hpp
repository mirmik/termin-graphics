#pragma once

#include <termin/gui_native/native_widget.hpp>
#include <termin/gui_native/widget_types.hpp>

namespace termin::gui_native {
class Swatch : public NativeWidget {
private:
    Color color_;

public:
    explicit Swatch(Color color);
    void paint(tc_ui_document_handle document, tc_ui_paint_context* context) override;
};
} // namespace termin::gui_native
