#include "widgets_internal.hpp"

namespace termin::gui_native {
using namespace detail;

Spacer::Spacer(tc_ui_size size)
    : NativeWidget("Spacer") {
    set_preferred_size(size);
}

} // namespace termin::gui_native
