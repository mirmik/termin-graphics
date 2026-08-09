#pragma once

#include "render/tc_color_output.h"
#include "termin/render/render_export.hpp"
#include <tgfx/texture_encoding.h>
#include <tgfx2/handles.hpp>
#include <tgfx2/descriptors.hpp>
#include <tgfx2/output_transform.hpp>
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

    enum class ColorOutputBindingOp : uint8_t {
        Direct,
        CopyOrResolve,
        Transform,
        RejectSceneLinear,
    };

    struct ColorOutputBindingPlan {
        ColorOutputBindingOp operation = ColorOutputBindingOp::CopyOrResolve;
        bool valid = true;
    };

    RENDER_CORE_API ColorOutputBindingPlan plan_color_output_binding(const tgfx::TextureDesc& source,
                                                                     ColorContent content,
                                                                     const tgfx::TextureDesc& target);

    RENDER_CORE_API tgfx::OutputTransformParams
    make_output_transform_params(const tgfx::TextureDesc& source,
                                 ColorContent content,
                                 const tgfx::TextureDesc& target);

    static_assert(std::is_standard_layout_v<ColorTarget>);

} // namespace termin
