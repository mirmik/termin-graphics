#include "widgets_internal.hpp"

namespace termin::gui_native {
using namespace detail;

Panel::Panel(const char* debug_name)
    : NativeWidget(debug_name) {
    set_style_role(TC_UI_STYLE_PANEL);
    set_preferred_size(tc_ui_size {96.0f, 64.0f});
}

Panel& Panel::set_fill(Color color) {
    set_style_color(*this, TC_UI_STYLE_BACKGROUND, color.c_color());
    return *this;
}

Panel& Panel::set_border(Color color, float thickness) {
    set_style_color(*this, TC_UI_STYLE_BORDER, color.c_color());
    set_style_metric(*this, TC_UI_STYLE_BORDER_WIDTH, std::max(0.0f, thickness));
    return *this;
}

void Panel::paint(tc_ui_document* document, tc_ui_paint_context* context) {
    const tc_ui_style style = computed_style(document);
    tc_ui_painter_fill_rect(context, bounds(), style.background);
    if (style.border_width > 0.0f && color_visible(style.border)) {
        tc_ui_painter_stroke_rect(context, bounds(), style.border, style.border_width);
    }
}



} // namespace termin::gui_native
