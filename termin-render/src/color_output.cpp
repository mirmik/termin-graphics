#include "termin/render/color_output.hpp"

#include <tgfx2/pixel_format_utils.hpp>

namespace termin {

    namespace {

        bool is_float_color_format(tgfx::PixelFormat format) {
            return format == tgfx::PixelFormat::R16F || format == tgfx::PixelFormat::RG16F ||
                   format == tgfx::PixelFormat::RGBA16F || format == tgfx::PixelFormat::R32F ||
                   format == tgfx::PixelFormat::RG32F || format == tgfx::PixelFormat::RGBA32F;
        }

        bool exact_physical_match(const tgfx::TextureDesc& source, const tgfx::TextureDesc& target) {
            return source.width == target.width && source.height == target.height &&
                   source.array_layers == target.array_layers && source.sample_count == target.sample_count &&
                   source.format == target.format;
        }

    } // namespace

    ColorOutputBindingPlan plan_color_output_binding(const tgfx::TextureDesc& source,
                                                     ColorContent content,
                                                     const tgfx::TextureDesc& target) {
        const tgfx::TextureEncoding target_encoding = tgfx::texture_encoding_for_format(target.format);

        if (content == ColorContent::SceneLinear) {
            if (target_encoding != tgfx::TextureEncoding::Linear || !is_float_color_format(target.format)) {
                return {ColorOutputBindingOp::RejectSceneLinear, false};
            }
            return {exact_physical_match(source, target) ? ColorOutputBindingOp::Direct
                                                         : ColorOutputBindingOp::CopyOrResolve,
                    true};
        }

        if (content == ColorContent::DisplayLinear && target_encoding == tgfx::TextureEncoding::SRGB) {
            return {ColorOutputBindingOp::EncodeSRGB, true};
        }
        if (content == ColorContent::DisplaySRGB && target_encoding == tgfx::TextureEncoding::Linear) {
            return {ColorOutputBindingOp::DecodeSRGB, true};
        }

        return {exact_physical_match(source, target) ? ColorOutputBindingOp::Direct
                                                     : ColorOutputBindingOp::CopyOrResolve,
                true};
    }

} // namespace termin
