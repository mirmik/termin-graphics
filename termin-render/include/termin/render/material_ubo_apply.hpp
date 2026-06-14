// material_ubo_apply.hpp - Apply material phase resources by reflected names.
#pragma once

extern "C" {
#include "tgfx/resources/tc_material.h"
#include "tgfx/resources/tc_shader.h"
}

namespace tgfx {
class IRenderDevice;
class RenderContext2;
}

namespace termin {

// Dispatch entry point: if `shader` declares a material UBO layout,
// packs + binds the phase's uniforms. Textures from phase->textures[] are
// bound by reflected resource name. Missing reflected resources are logged.
// Returns true when any material resource was bound.
bool apply_material_phase_ubo(
    tc_material_phase* phase,
    const tc_shader* shader,
    tgfx::IRenderDevice& device,
    tgfx::RenderContext2& ctx);

} // namespace termin
