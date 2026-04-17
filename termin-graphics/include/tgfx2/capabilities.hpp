#pragma once

#include <cstdint>
#include "tgfx2/enums.hpp"

namespace tgfx {

struct BackendCapabilities {
    BackendType backend = BackendType::Null;
    bool supports_compute = false;
    bool supports_geometry_shaders = false;
    bool supports_timestamp_queries = false;
    bool supports_multisample_resolve = true;
    uint32_t max_color_attachments = 4;
    uint32_t max_texture_dimension_2d = 8192;
    uint32_t max_texture_units = 16;
};

} // namespace tgfx
