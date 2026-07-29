// pixel_format_utils.hpp - Common PixelFormat naming/parsing helpers.
#pragma once

#include <string_view>

#include "tgfx2/enums.hpp"
#include "tgfx2/tgfx2_api.h"

extern "C" {
#include "tgfx/resources/tc_texture.h"
}

namespace tgfx {

TGFX2_API bool is_depth_format(PixelFormat format);
TGFX2_API bool is_srgb_format(PixelFormat format);
TGFX2_API bool is_rgba8_family(PixelFormat format);
TGFX2_API uint32_t pixel_format_byte_size(PixelFormat format);
TGFX2_API PixelFormat pixel_format_for_encoding(
    PixelFormat storage_format,
    TextureEncoding encoding);
TGFX2_API PixelFormat pixel_format_for_tc_texture(
    tc_texture_format storage_format,
    tc_texture_encoding encoding);
TGFX2_API std::string_view pixel_format_name(PixelFormat format);
TGFX2_API PixelFormat pixel_format_from_name(
    std::string_view name,
    PixelFormat fallback = PixelFormat::RGBA8_UNorm);

} // namespace tgfx
