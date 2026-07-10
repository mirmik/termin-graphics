#pragma once

#include <termin/gui_native/box_layout.hpp>

namespace termin::gui_native {
class HStack : public BoxLayout {
public:
    explicit HStack(const char* debug_name = nullptr) : BoxLayout(Orientation::Horizontal, debug_name) {}
};
} // namespace termin::gui_native
