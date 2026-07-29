#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include "tgfx2/enums.hpp"
#include "tgfx2/tgfx2_api.h"

extern "C" {
#include "tgfx/resources/tc_texture.h"
}

namespace tgfx {

// Backend-neutral upload payload for a tc_texture. RGB source formats are
// normalized to RGBA because not every backend has a portable RGB upload
// layout. Each entry contains one complete, tightly packed mip level.
struct TcTextureUpload {
    PixelFormat format = PixelFormat::Undefined;
    std::vector<std::vector<uint8_t>> levels;
};

TGFX2_API uint32_t texture_mip_level_count(uint32_t width, uint32_t height);

// Builds a complete mip chain from an already normalized base level.
// sRGB color channels are decoded before filtering and encoded afterwards;
// alpha and all linear/data formats are averaged numerically.
TGFX2_API bool build_texture_mip_chain(
    PixelFormat format,
    uint32_t width,
    uint32_t height,
    std::span<const uint8_t> base_level,
    std::vector<std::vector<uint8_t>>& out_levels);

// Normalizes tc_texture storage and applies its mipmap policy. Failures are
// logged and never degrade silently to a level-0-only upload.
TGFX2_API bool prepare_tc_texture_upload(
    const tc_texture* texture,
    TcTextureUpload& out_upload);

} // namespace tgfx
