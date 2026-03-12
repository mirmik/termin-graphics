// tc_gpu.h - GPU operations for termin
// Resource GPU ops (texture, shader, mesh) are now in tgfx_resource_gpu.h.
// This file provides termin-specific material operations.
#pragma once

#include "tc_types.h"
#include <tgfx/tgfx_resource_gpu.h>
#include <tgfx/tc_gpu_context.h>
#include <tgfx/resources/tc_material.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// Material GPU operations (termin-specific, not in tgfx)
// ============================================================================

// Apply material phase for rendering:
// 1. Compile and use shader
// 2. Bind textures
// 3. Apply uniform values
// Returns true if successful
TC_API bool tc_material_phase_apply_gpu(tc_material_phase* phase);

// Apply material uniforms only (shader must already be in use)
TC_API void tc_material_phase_apply_uniforms(tc_material_phase* phase, tc_shader* shader);

// Apply material textures only
TC_API void tc_material_phase_apply_textures(tc_material_phase* phase);

// Apply material phase with MVP matrices (shader must already be in use)
// Sets u_model, u_view, u_projection, binds textures, applies uniforms
TC_API void tc_material_phase_apply_with_mvp(
    tc_material_phase* phase,
    tc_shader* shader,
    const float* model,      // 16 floats
    const float* view,       // 16 floats
    const float* projection  // 16 floats
);

#ifdef __cplusplus
}
#endif
