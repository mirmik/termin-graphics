#pragma once

#include <cstddef>
#include <cstdint>

#include <tcbase/input_enums.hpp>
#include <termin/geom/color.hpp>
#include <termin/gui_native/tc_ui_document.h>

namespace termin::gui_native {

    enum class Orientation {
        Horizontal,
        Vertical
    };
    enum class TextWrapMode {
        None,
        Word,
        Character
    };
    enum class TextOverflow {
        Clip,
        Ellipsis
    };
    enum class ImageFit {
        Stretch,
        Contain,
        Cover
    };
    struct EdgeInsets {
        float left = 0.0f;
        float top = 0.0f;
        float right = 0.0f;
        float bottom = 0.0f;
    };
    enum class LayoutPolicy {
        Fixed,
        Preferred,
        Flex,
        Stretch
    };
    enum class CrossAxisAlignment {
        Auto,
        Stretch,
        Start,
        Center,
        End
    };
    struct LayoutItem {
        tc_widget_handle handle{};
        LayoutPolicy policy = LayoutPolicy::Stretch;
        float fixed_extent = 0.0f;
        float flex = 1.0f;
        float grow = 1.0f;
        float shrink = 1.0f;
        float min_extent = 0.0f;
        float max_extent = 0.0f;
        CrossAxisAlignment align_self = CrossAxisAlignment::Auto;
    };
    struct GridTrack {
        LayoutPolicy policy = LayoutPolicy::Stretch;
        float value = 0.0f;
        float grow = 1.0f;
        float shrink = 1.0f;
        float min_extent = 0.0f;
        float max_extent = 0.0f;
    };
    struct GridItem {
        tc_widget_handle handle{};
        size_t row = 0;
        size_t column = 0;
        size_t row_span = 1;
        size_t column_span = 1;
    };
    inline tc_ui_srgb_color to_tc_ui_srgb(termin::SrgbColor color) {
        return tc_ui_srgb_color{color.r, color.g, color.b, color.a};
    }

} // namespace termin::gui_native
