#pragma once

#include "render/tc_color_output.h"
#include "termin/render/render_export.hpp"
#include <tgfx/texture_encoding.h>
#include <tgfx2/handles.hpp>
#include <string>
#include <type_traits>

namespace termin {

    enum class ColorContent : uint8_t {
        SceneLinear = TC_COLOR_CONTENT_SCENE_LINEAR,
        DisplayLinear = TC_COLOR_CONTENT_DISPLAY_LINEAR,
        DisplaySRGB = TC_COLOR_CONTENT_DISPLAY_SRGB,
    };

    // A physical color destination is identified by its texture. Format,
    // transfer encoding, extent and sample count are properties of the live
    // texture descriptor and must not be copied into this contract.
    struct ColorTarget {
        tgfx::TextureHandle texture{};
    };

    // A pipeline export describes data owned by the pipeline. It deliberately
    // carries no physical format: the destination contract is supplied later
    // by the caller that binds this export to a ColorTarget.
    struct PipelineColorExport {
        std::string resource;
        std::string viewport_name;
        ColorContent content = ColorContent::DisplayLinear;
    };

    static_assert(std::is_standard_layout_v<ColorTarget>);

} // namespace termin
