#pragma once

#include <termin/gui_native/box_layout.hpp>

namespace termin::gui_native {
class VStack : public BoxLayout {
public:
    explicit VStack(const char* debug_name = nullptr) : BoxLayout(Orientation::Vertical, debug_name) {}
};
} // namespace termin::gui_native
