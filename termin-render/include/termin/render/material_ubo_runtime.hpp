#pragma once

#include <cstdint>

#include "termin/render/render_export.hpp"

extern "C" {
#include "tgfx/resources/tc_material.h"
#include "tgfx/resources/tc_shader.h"
}

namespace tgfx {
class IRenderDevice;
class RenderContext2;
}

namespace termin {

// Backend-neutral runtime dispatcher for per-material std140 UBOs.
//
// Unlike termin-app's shader-parser helpers, this consumes only the C-side
// layout already baked into tc_shader plus current values from tc_material.
// That keeps low-level passes usable from termin-components-render without a
// dependency on termin-app.
RENDER_API bool apply_material_phase_ubo_runtime(
    tc_material_phase* phase,
    const tc_shader* shader,
    uint32_t ubo_slot,
    uint32_t tex_slot_start,
    tgfx::IRenderDevice& device,
    tgfx::RenderContext2& ctx
);

} // namespace termin
