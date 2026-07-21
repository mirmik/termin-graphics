#include "widgets_internal.hpp"

namespace termin::gui_native {
using namespace detail;

Swatch::Swatch(Color color)
    : NativeWidget("Swatch"), color_(color) {
    set_preferred_size(tc_ui_size {36.0f, 36.0f});
}

void Swatch::paint(tc_ui_document_handle, tc_ui_paint_context* context) {
    tc_ui_painter_fill_rect(context, bounds(), color_.c_color());
    tc_ui_painter_stroke_rect(context, bounds(), tc_ui_color {0.88f, 0.90f, 0.94f, 1.0f}, 1.0f);
}


} // namespace termin::gui_native
