#include "widgets_internal.hpp"

namespace termin::gui_native {
using namespace detail;

ProgressBar::ProgressBar(float value)
    : NativeWidget("ProgressBar") {
    set_style_role(TC_UI_STYLE_PROGRESS);
    set_preferred_size(tc_ui_size {120.0f, 20.0f});
    set_value(value);
}

void ProgressBar::set_value(float value) {
    const float next = clamp_float(value, 0.0f, 1.0f);
    if (std::fabs(next - value_) <= 0.0001f) {
        return;
    }
    value_ = next;
    mark_dirty(TC_WIDGET_DIRTY_STATE | TC_WIDGET_DIRTY_PAINT);
}

void ProgressBar::paint(tc_ui_document* document, tc_ui_paint_context* context) {
    const tc_ui_style style = computed_style(document);
    tc_ui_painter_fill_rect(context, bounds(), style.background);
    tc_ui_rect fill = bounds();
    fill.width *= value_;
    tc_ui_painter_fill_rect(context, fill, style.accent);
    tc_ui_painter_stroke_rect(context, bounds(), style.border, style.border_width);
}


} // namespace termin::gui_native
