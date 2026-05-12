// pixel_format_utils.hpp - Common PixelFormat naming/parsing helpers.
#pragma once

#include <string_view>

#include "tgfx2/enums.hpp"
#include "tgfx2/tgfx2_api.h"

namespace tgfx {

TGFX2_API bool is_depth_format(PixelFormat format);
TGFX2_API std::string_view pixel_format_name(PixelFormat format);
TGFX2_API PixelFormat pixel_format_from_name(
    std::string_view name,
    PixelFormat fallback = PixelFormat::RGBA8_UNorm);

} // namespace tgfx
