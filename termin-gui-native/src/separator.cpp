#include "widgets_internal.hpp"

namespace termin::gui_native {
using namespace detail;

Separator::Separator(Orientation orientation)
    : NativeWidget("Separator"), orientation_(orientation) {
    set_style_role(TC_UI_STYLE_SEPARATOR);
    set_preferred_size(orientation_ == Orientation::Horizontal
        ? tc_ui_size {24.0f, 1.0f}
        : tc_ui_size {1.0f, 24.0f});
}

Separator& Separator::set_color(Color color) {
    set_style_color(*this, TC_UI_STYLE_BACKGROUND, color.c_color());
    return *this;
}

Separator& Separator::set_thickness(float thickness) {
    const float next = std::max(1.0f, thickness);
    set_style_metric(*this, TC_UI_STYLE_BORDER_WIDTH, next);
    set_preferred_size(orientation_ == Orientation::Horizontal
        ? tc_ui_size {preferred_size().width, next}
        : tc_ui_size {next, preferred_size().height});
    return *this;
}

void Separator::paint(tc_ui_document_handle document, tc_ui_paint_context* context) {
    const tc_ui_style style = computed_style(document);
    const float thickness = std::max(1.0f, style.border_width);
    tc_ui_rect rect = bounds();
    if (orientation_ == Orientation::Horizontal) {
        rect.y += std::max(0.0f, (rect.height - thickness) * 0.5f);
        rect.height = std::min(rect.height, thickness);
    } else {
        rect.x += std::max(0.0f, (rect.width - thickness) * 0.5f);
        rect.width = std::min(rect.width, thickness);
    }
    tc_ui_painter_fill_rect(context, rect, style.background);
}



} // namespace termin::gui_native
