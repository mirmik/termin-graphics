// tc_gpu.h - legacy material glUniform dispatch. Removed in Stage 8.2.
//
// The per-uniform tc_material_phase_apply_uniforms /
// tc_material_phase_apply_textures / tc_material_phase_apply_gpu /
// tc_material_phase_apply_with_mvp functions used to loop over a
// phase's uniforms and call glUniform* through the gpu_ops vtable.
// That path is dead — materials now pack uniforms into a std140 UBO
// (material_ubo_apply.{hpp,cpp}) and bind it via
// ctx2->bind_uniform_buffer inside tgfx2-migrated passes.
//
// Header left in place so #include <tgfx/tc_gpu.h> keeps compiling
// in call sites that still transitively pulled in
// tgfx_resource_gpu / tc_gpu_context / tc_material through this
// header. Those will be swapped to direct includes in a later
// cleanup pass.
#pragma once

#include "tc_types.h"
#include <tgfx/tgfx_resource_gpu.h>
#include <tgfx/tc_gpu_context.h>
#include <tgfx/resources/tc_material.h>
