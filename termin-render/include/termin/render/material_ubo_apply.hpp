// material_ubo_apply.hpp - Upload + bind a std140 material UBO for a draw.
//
// Stage 3 of the tgfx2 migration. Consumes a MaterialUboLayout (produced by
// shader_parser at shader load time) and a list of property values to:
//   1. Pack values into a std140-laid out staging buffer.
//   2. Bind the payload through RenderContext2's dynamic UBO path.
//   3. Bind any accompanying sampled textures at their slots via RenderContext2.
#pragma once

#include <cstdint>
#include <vector>

#include "tgfx2/handles.hpp"

extern "C" {
#include "tgfx/resources/tc_material.h"
#include "tgfx/resources/tc_shader.h"
}

namespace tgfx {
class IRenderDevice;
class RenderContext2;
}

namespace termin {

// Fallback UBO binding slot for the per-material std140 block.
// Convention:
//   slot 1  — per-material (this)
//   slot 2  — per-frame view/proj data
//   slot 14 — per-object push constants (TGFX2_PUSH_CONSTANTS_BINDING)
//   slot 24 — per-draw data (TC_SHADER_RESOURCE_BINDING_DRAW_DATA)
constexpr uint32_t MATERIAL_UBO_BINDING = 1;

struct MaterialProperty;
struct MaterialUboLayout;

// A sampled texture that accompanies the material UBO at draw time.
struct MaterialTextureBinding {
    uint32_t slot = 0;
    tgfx::TextureHandle texture;
    tgfx::SamplerHandle sampler;  // may be {} for default sampling
};

// Resolve the per-material UBO binding from shader resource metadata. The
// fallback keeps legacy shaders working until every load path populates
// ShaderResourceLayout from compiled artifacts/reflection.
uint32_t material_ubo_binding_for_shader(
    const tc_shader* shader,
    uint32_t fallback_binding = MATERIAL_UBO_BINDING);

// Pack, upload, and bind one material UBO + its textures.
//
// Requirements:
//   - `ctx` must be inside begin_pass (so command-list state is live).
//
// Values not present in `values` are written as zero because the staging
// buffer is zero-initialised for every bind.
void bind_material_ubo(
    const MaterialUboLayout& layout,
    const std::vector<MaterialProperty>& values,
    const std::vector<MaterialTextureBinding>& textures,
    uint32_t ubo_slot,
    uint32_t set,
    tgfx::RenderContext2& ctx);

// Dispatch entry point: if `shader` declares a material UBO layout,
// packs + binds the phase's uniforms at `ubo_slot`. Textures from
// phase->textures[] are bound in declared order to material sampler slots.
// The `tex_slot_start` argument is retained for ABI compatibility; slot
// assignment skips the shadow-map binding at 8. Returns true when the UBO
// path ran; false when the shader has no layout and the caller should fall
// back to legacy per-uniform dispatch.
bool apply_material_phase_ubo(
    tc_material_phase* phase,
    const tc_shader* shader,
    uint32_t ubo_slot,
    uint32_t tex_slot_start,
    tgfx::IRenderDevice& device,
    tgfx::RenderContext2& ctx);

} // namespace termin
