#pragma once

#include <algorithm>
#include <cstdint>

#include <termin/geom/bounds2.hpp>

namespace tgfx {

inline termin::Bounds2i aspect_fit_rect(
    uint32_t source_width, uint32_t source_height,
    uint32_t destination_width, uint32_t destination_height)
{
    if (source_width == 0 || source_height == 0 ||
        destination_width == 0 || destination_height == 0) {
        return {};
    }
    uint32_t width = destination_width;
    uint32_t height = destination_height;
    if (static_cast<uint64_t>(source_width) * destination_height >
        static_cast<uint64_t>(destination_width) * source_height) {
        height = std::max(1u, static_cast<uint32_t>(
            static_cast<uint64_t>(destination_width) * source_height / source_width));
    } else {
        width = std::max(1u, static_cast<uint32_t>(
            static_cast<uint64_t>(destination_height) * source_width / source_height));
    }
    const int x = static_cast<int>((destination_width - width) / 2u);
    const int y = static_cast<int>((destination_height - height) / 2u);
    return {x, y, x + static_cast<int>(width), y + static_cast<int>(height)};
}

} // namespace tgfx
