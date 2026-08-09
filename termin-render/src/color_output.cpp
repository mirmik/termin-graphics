#include "termin/render/color_output.hpp"

#include <tgfx2/pixel_format_utils.hpp>

namespace termin {

    namespace {

        bool is_float_color_format(tgfx::PixelFormat format) {
            return format == tgfx::PixelFormat::R16F || format == tgfx::PixelFormat::RG16F ||
                   format == tgfx::PixelFormat::RGBA16F || format == tgfx::PixelFormat::R32F ||
                   format == tgfx::PixelFormat::RG32F || format == tgfx::PixelFormat::RGBA32F;
        }

        bool programmable_transform_supported(const tgfx::TextureDesc& source,
                                               const tgfx::TextureDesc& target) {
            return source.sample_count == 1 && target.sample_count == 1 &&
                   source.array_layers == target.array_layers;
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

        const tgfx::TextureEncoding source_encoding = tgfx::texture_encoding_for_format(source.format);
        const bool transfer_mismatch =
            (content == ColorContent::DisplayLinear && target_encoding == tgfx::TextureEncoding::SRGB) ||
            (content == ColorContent::DisplaySRGB && target_encoding == tgfx::TextureEncoding::Linear);
        const bool low_precision_target = tgfx::is_rgba8_family(target.format);
        const bool source_is_already_quantized = tgfx::is_rgba8_family(source.format) &&
                                                 source_encoding == target_encoding;

        if ((transfer_mismatch || (low_precision_target && !source_is_already_quantized)) &&
            programmable_transform_supported(source, target)) {
            return {ColorOutputBindingOp::Transform, true};
        }

        return {exact_physical_match(source, target) ? ColorOutputBindingOp::Direct
                                                     : ColorOutputBindingOp::CopyOrResolve,
                true};
    }

    tgfx::OutputTransformParams make_output_transform_params(const tgfx::TextureDesc& source,
                                                              ColorContent content,
                                                              const tgfx::TextureDesc& target) {
        const tgfx::TextureEncoding source_storage_encoding = tgfx::texture_encoding_for_format(source.format);
        const tgfx::TextureEncoding target_encoding = tgfx::texture_encoding_for_format(target.format);

        // Sampling an sRGB texture already applies the EOTF. DisplaySRGB data
        // stored in a linear texture remains encoded and needs an explicit
        // decode in the output shader.
        const bool sampled_values_are_srgb =
            content == ColorContent::DisplaySRGB && source_storage_encoding == tgfx::TextureEncoding::Linear;
        const bool low_precision_target = tgfx::is_rgba8_family(target.format);

        return tgfx::OutputTransformParams{
            .sampled_input_encoding = sampled_values_are_srgb ? tgfx::TextureEncoding::SRGB
                                                               : tgfx::TextureEncoding::Linear,
            .target_encoding = target_encoding,
            .dither = low_precision_target ? tgfx::OutputDitherMode::StableSpatial
                                           : tgfx::OutputDitherMode::Disabled,
            .target_rgb_bits = static_cast<uint8_t>(low_precision_target ? 8 : 0),
        };
    }

} // namespace termin
