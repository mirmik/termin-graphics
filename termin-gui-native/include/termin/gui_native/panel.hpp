#pragma once

#include <termin/gui_native/native_widget.hpp>
#include <termin/gui_native/widget_types.hpp>

namespace termin::gui_native {
class Panel : public NativeWidget {
public:
    explicit Panel(const char* debug_name = nullptr);
    Panel& set_fill(Color color);
    Panel& set_border(Color color, float thickness = 1.0f);
    void paint(tc_ui_document* document, tc_ui_paint_context* context) override;
};
} // namespace termin::gui_native
