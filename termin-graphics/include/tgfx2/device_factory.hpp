#pragma once

#include <memory>
#include "tgfx2/tgfx2_api.h"
#include "tgfx2/enums.hpp"
#include "tgfx2/i_render_device.hpp"

namespace tgfx {

TGFX2_API std::unique_ptr<IRenderDevice> create_device(BackendType type);

// Return the backend selected by the TERMIN_BACKEND env-var.
// Accepts case-insensitive "opengl"/"gl", "vulkan"/"vk". Anything else
// (or unset) falls back to OpenGL. This is the single point where
// env-driven backend selection is resolved; hosting code should call
// `create_device(default_backend_from_env())` instead of hard-coding
// the backend.
TGFX2_API BackendType default_backend_from_env();

} // namespace tgfx
