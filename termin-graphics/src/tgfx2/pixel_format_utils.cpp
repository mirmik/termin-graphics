#include "tgfx2/pixel_format_utils.hpp"

namespace tgfx {

bool is_depth_format(PixelFormat format) {
    return format == PixelFormat::D24_UNorm ||
           format == PixelFormat::D24_UNorm_S8_UInt ||
           format == PixelFormat::D32F;
}

std::string_view pixel_format_name(PixelFormat format) {
    switch (format) {
        case PixelFormat::R8_UNorm:          return "r8";
        case PixelFormat::RG8_UNorm:         return "rg8";
        case PixelFormat::RGB8_UNorm:        return "rgb8";
        case PixelFormat::RGBA8_UNorm:       return "rgba8";
        case PixelFormat::BGRA8_UNorm:       return "bgra8";
        case PixelFormat::R16F:              return "r16f";
        case PixelFormat::RG16F:             return "rg16f";
        case PixelFormat::RGBA16F:           return "rgba16f";
        case PixelFormat::R32F:              return "r32f";
        case PixelFormat::RG32F:             return "rg32f";
        case PixelFormat::RGBA32F:           return "rgba32f";
        case PixelFormat::D24_UNorm:         return "depth24";
        case PixelFormat::D24_UNorm_S8_UInt: return "depth24_stencil8";
        case PixelFormat::D32F:              return "depth32f";
        case PixelFormat::Undefined:         return "undefined";
    }
    return "unknown";
}

PixelFormat pixel_format_from_name(std::string_view name, PixelFormat fallback) {
    if (name.empty() || name == "rgba8") return PixelFormat::RGBA8_UNorm;
    if (name == "r8") return PixelFormat::R8_UNorm;
    if (name == "rg8") return PixelFormat::RG8_UNorm;
    if (name == "rgb8") return PixelFormat::RGB8_UNorm;
    if (name == "bgra8") return PixelFormat::BGRA8_UNorm;
    if (name == "r16f") return PixelFormat::R16F;
    if (name == "rg16f") return PixelFormat::RG16F;
    if (name == "rgba16f") return PixelFormat::RGBA16F;
    if (name == "r32f") return PixelFormat::R32F;
    if (name == "rg32f") return PixelFormat::RG32F;
    if (name == "rgba32f") return PixelFormat::RGBA32F;
    if (name == "depth24") return PixelFormat::D24_UNorm;
    if (name == "depth24_stencil8") return PixelFormat::D24_UNorm_S8_UInt;
    if (name == "depth32f") return PixelFormat::D32F;
    if (name == "undefined") return PixelFormat::Undefined;
    return fallback;
}

} // namespace tgfx
