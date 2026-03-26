#pragma once

#include <memory>
#include "tgfx2/tgfx2_api.h"
#include "tgfx2/enums.hpp"
#include "tgfx2/i_render_device.hpp"

namespace tgfx2 {

TGFX2_API std::unique_ptr<IRenderDevice> create_device(BackendType type);

} // namespace tgfx2
