#pragma once

#include <termin/gui_native/native_widget.hpp>

namespace termin::gui_native {
class Spacer : public NativeWidget {
public:
    explicit Spacer(tc_ui_size size);
};
} // namespace termin::gui_native
