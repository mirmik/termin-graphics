#include "tgfx2/pixel_format_utils.hpp"

namespace tgfx {

    bool is_depth_format(PixelFormat format) {
        return format == PixelFormat::D24_UNorm || format == PixelFormat::D24_UNorm_S8_UInt ||
               format == PixelFormat::D32F;
    }

    bool is_srgb_format(PixelFormat format) {
        return format == PixelFormat::RGBA8_sRGB || format == PixelFormat::BGRA8_sRGB;
    }

    bool is_rgba8_family(PixelFormat format) {
        return format == PixelFormat::RGBA8_UNorm || format == PixelFormat::BGRA8_UNorm ||
               format == PixelFormat::RGBA8_sRGB || format == PixelFormat::BGRA8_sRGB;
    }

    uint32_t pixel_format_byte_size(PixelFormat format) {
        switch (format) {
        case PixelFormat::R8_UNorm:
            return 1;
        case PixelFormat::RG8_UNorm:
            return 2;
        case PixelFormat::RGB8_UNorm:
            return 3;
        case PixelFormat::RGBA8_UNorm:
        case PixelFormat::BGRA8_UNorm:
        case PixelFormat::RGBA8_sRGB:
        case PixelFormat::BGRA8_sRGB:
        case PixelFormat::R32F:
        case PixelFormat::D24_UNorm:
        case PixelFormat::D24_UNorm_S8_UInt:
        case PixelFormat::D32F:
            return 4;
        case PixelFormat::R16F:
            return 2;
        case PixelFormat::RG16F:
            return 4;
        case PixelFormat::RGBA16F:
            return 8;
        case PixelFormat::RG32F:
            return 8;
        case PixelFormat::RGBA32F:
            return 16;
        case PixelFormat::Undefined:
            return 0;
        }
        return 0;
    }

    PixelFormat pixel_format_for_encoding(PixelFormat storage_format, TextureEncoding encoding) {
        switch (storage_format) {
        case PixelFormat::RGBA8_UNorm:
        case PixelFormat::RGBA8_sRGB:
            return encoding == TextureEncoding::SRGB ? PixelFormat::RGBA8_sRGB : PixelFormat::RGBA8_UNorm;
        case PixelFormat::BGRA8_UNorm:
        case PixelFormat::BGRA8_sRGB:
            return encoding == TextureEncoding::SRGB ? PixelFormat::BGRA8_sRGB : PixelFormat::BGRA8_UNorm;
        default:
            // The first contract intentionally supports sRGB only for the
            // portable four-channel 8-bit formats. Importers normalize RGB8
            // before requesting sRGB; numeric, float, and depth formats must
            // remain Linear.
            return encoding == TextureEncoding::Linear ? storage_format : PixelFormat::Undefined;
        }
    }

    PixelFormat pixel_format_for_tc_texture(tc_texture_format storage_format, tc_texture_encoding encoding) {
        TextureEncoding cpp_encoding;
        switch (encoding) {
        case TC_TEXTURE_ENCODING_LINEAR:
            cpp_encoding = TextureEncoding::Linear;
            break;
        case TC_TEXTURE_ENCODING_SRGB:
            cpp_encoding = TextureEncoding::SRGB;
            break;
        default:
            return PixelFormat::Undefined;
        }

        PixelFormat base_format = PixelFormat::Undefined;
        switch (storage_format) {
        case TC_TEXTURE_RGBA8:
            base_format = PixelFormat::RGBA8_UNorm;
            break;
        case TC_TEXTURE_RGB8:
            base_format = PixelFormat::RGB8_UNorm;
            break;
        case TC_TEXTURE_RG8:
            base_format = PixelFormat::RG8_UNorm;
            break;
        case TC_TEXTURE_R8:
            base_format = PixelFormat::R8_UNorm;
            break;
        case TC_TEXTURE_RGBA16F:
            base_format = PixelFormat::RGBA16F;
            break;
        case TC_TEXTURE_RGB16F:
            base_format = PixelFormat::RGBA16F;
            break;
        case TC_TEXTURE_DEPTH24:
            base_format = PixelFormat::D24_UNorm_S8_UInt;
            break;
        case TC_TEXTURE_DEPTH32F:
            base_format = PixelFormat::D32F;
            break;
        case TC_TEXTURE_R16F:
            base_format = PixelFormat::R16F;
            break;
        case TC_TEXTURE_R32F:
            base_format = PixelFormat::R32F;
            break;
        }
        return pixel_format_for_encoding(base_format, cpp_encoding);
    }

    std::string_view pixel_format_name(PixelFormat format) {
        switch (format) {
        case PixelFormat::R8_UNorm:
            return "r8";
        case PixelFormat::RG8_UNorm:
            return "rg8";
        case PixelFormat::RGB8_UNorm:
            return "rgb8";
        case PixelFormat::RGBA8_UNorm:
            return "rgba8";
        case PixelFormat::BGRA8_UNorm:
            return "bgra8";
        case PixelFormat::R16F:
            return "r16f";
        case PixelFormat::RG16F:
            return "rg16f";
        case PixelFormat::RGBA16F:
            return "rgba16f";
        case PixelFormat::R32F:
            return "r32f";
        case PixelFormat::RG32F:
            return "rg32f";
        case PixelFormat::RGBA32F:
            return "rgba32f";
        case PixelFormat::D24_UNorm:
            return "depth24";
        case PixelFormat::D24_UNorm_S8_UInt:
            return "depth24_stencil8";
        case PixelFormat::D32F:
            return "depth32f";
        case PixelFormat::RGBA8_sRGB:
            return "rgba8_srgb";
        case PixelFormat::BGRA8_sRGB:
            return "bgra8_srgb";
        case PixelFormat::Undefined:
            return "undefined";
        }
        return "unknown";
    }

    PixelFormat pixel_format_from_name(std::string_view name, PixelFormat fallback) {
        if (name.empty() || name == "rgba8")
            return PixelFormat::RGBA8_UNorm;
        if (name == "r8")
            return PixelFormat::R8_UNorm;
        if (name == "rg8")
            return PixelFormat::RG8_UNorm;
        if (name == "rgb8")
            return PixelFormat::RGB8_UNorm;
        if (name == "bgra8")
            return PixelFormat::BGRA8_UNorm;
        if (name == "r16f")
            return PixelFormat::R16F;
        if (name == "rg16f")
            return PixelFormat::RG16F;
        if (name == "rgba16f")
            return PixelFormat::RGBA16F;
        if (name == "r32f")
            return PixelFormat::R32F;
        if (name == "rg32f")
            return PixelFormat::RG32F;
        if (name == "rgba32f")
            return PixelFormat::RGBA32F;
        if (name == "depth24")
            return PixelFormat::D24_UNorm;
        if (name == "depth24_stencil8")
            return PixelFormat::D24_UNorm_S8_UInt;
        if (name == "depth32f")
            return PixelFormat::D32F;
        if (name == "rgba8_srgb")
            return PixelFormat::RGBA8_sRGB;
        if (name == "bgra8_srgb")
            return PixelFormat::BGRA8_sRGB;
        if (name == "undefined")
            return PixelFormat::Undefined;
        return fallback;
    }

} // namespace tgfx
