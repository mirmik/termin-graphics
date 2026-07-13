// material_ubo_apply.hpp - Apply material phase resources by reflected names.
#pragma once

#include <cstdint>

#include <termin/render/render_export.hpp>

extern "C" {
#include "tgfx/resources/tc_material.h"
#include "tgfx/resources/tc_shader.h"
}

namespace tgfx {
class IRenderDevice;
class RenderContext2;
}

namespace termin {

// Pack one material uniform value into a std140 field. Scalar numeric
// compatibility is intentionally narrow: Float<->Int follow existing material
// coercion, and Bool fields accept either TC_UNIFORM_BOOL or TC_UNIFORM_INT,
// normalized to int32 0/1. Returns false without touching dst on mismatch and
// logs the incompatible uniform/reflected field types.
RENDER_API bool pack_material_uniform_value_to_std140_field(
    const tc_uniform_value& uniform,
    const char* field_type,
    uint8_t* dst);

// Dispatch entry point: if `shader` declares a material UBO layout,
// packs + binds the phase's uniforms. Textures from phase->textures[] are
// bound by reflected resource name. Missing reflected resources are logged.
// Returns true when any material resource was bound.
RENDER_API bool apply_material_phase_ubo(
    tc_material_phase* phase,
    const tc_shader* shader,
    tgfx::IRenderDevice& device,
    tgfx::RenderContext2& ctx);

} // namespace termin
