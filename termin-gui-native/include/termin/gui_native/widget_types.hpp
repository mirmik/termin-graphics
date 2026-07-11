#pragma once

#include <cstddef>
#include <cstdint>

#include <termin/gui_native/tc_ui_document.h>

namespace termin::gui_native {

enum class Orientation { Horizontal, Vertical };
enum class PointerButton : int32_t { Left = 0, Right = 1, Middle = 2 };
constexpr int32_t pointer_button_value(PointerButton button) {
    return static_cast<int32_t>(button);
}
struct EdgeInsets { float left = 0.0f; float top = 0.0f; float right = 0.0f; float bottom = 0.0f; };
enum class LayoutPolicy { Fixed, Preferred, Flex, Stretch };
struct LayoutItem { tc_widget_handle handle {}; LayoutPolicy policy = LayoutPolicy::Stretch; float fixed_extent = 0.0f; float flex = 1.0f; float grow = 1.0f; float shrink = 1.0f; float min_extent = 0.0f; float max_extent = 0.0f; };
struct GridTrack { LayoutPolicy policy = LayoutPolicy::Stretch; float value = 0.0f; float grow = 1.0f; float shrink = 1.0f; float min_extent = 0.0f; float max_extent = 0.0f; };
struct GridItem { tc_widget_handle handle {}; size_t row = 0; size_t column = 0; size_t row_span = 1; size_t column_span = 1; };
struct Color { float r = 0.0f; float g = 0.0f; float b = 0.0f; float a = 1.0f; tc_ui_color c_color() const { return tc_ui_color {r, g, b, a}; } };

} // namespace termin::gui_native
