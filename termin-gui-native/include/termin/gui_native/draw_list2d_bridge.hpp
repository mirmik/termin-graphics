#pragma once

#include <termin/gui_native/export.h>
#include <termin/gui_native/tc_ui_document.h>
#include <tgfx2/draw_list2d.hpp>

namespace termin::gui_native {

// Owns a frozen canonical draw list in the surrounding tc_ui_draw_list so it
// can be rendered in-order with ordinary widget commands.
TERMIN_GUI_NATIVE_API bool append_draw_list2d(
    tc_ui_paint_context* context,
    tgfx::DrawList2D draw_list);

} // namespace termin::gui_native
