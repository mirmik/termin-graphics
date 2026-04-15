// tgfx_resource_gpu.h - GPU operations for resources (texture, shader, mesh)
// High-level wrappers that use tgfx_gpu_ops vtable + tc_gpu_context.
#ifndef TGFX_RESOURCE_GPU_H
#define TGFX_RESOURCE_GPU_H

#include "tgfx_api.h"
#include "resources/tc_texture.h"
#include "resources/tc_shader.h"
#include <tgfx/resources/tc_mesh.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// Shader preprocessor callback
// ============================================================================

// Preprocess shader source (e.g. resolve #include directives).
// Returns malloc'd string (caller frees) or NULL if no preprocessing needed.
typedef char* (*tgfx_shader_preprocess_fn)(const char* source, const char* source_name);

// Set shader preprocessor callback
TGFX_API void tgfx_gpu_set_shader_preprocess(tgfx_shader_preprocess_fn fn);

// Read the currently-set shader preprocessor callback (NULL if none).
// Exposed so the tgfx2 OpenGL backend can reuse the same preprocessor
// that the legacy path uses — that's where #include "lighting.glsl"
// and friends get resolved. Without this, tgfx2-compiled shaders
// would fail on any .shader that relies on includes.
TGFX_API tgfx_shader_preprocess_fn tgfx_gpu_get_shader_preprocess(void);

// ============================================================================
// Texture GPU operations
// ============================================================================

// Bind texture to unit, uploading if needed
TGFX_API bool tc_texture_bind_gpu(tc_texture* tex, int unit);

// Force re-upload texture to GPU
TGFX_API bool tc_texture_upload_gpu(tc_texture* tex);

// Delete texture from GPU (keeps CPU data)
TGFX_API void tc_texture_delete_gpu(tc_texture* tex);

// Check if texture needs GPU upload (version mismatch)
TGFX_API bool tc_texture_needs_upload(const tc_texture* tex);

// ============================================================================
// Mesh GPU operations (OpenGL implementation)
// These are the actual GPU backends; registered via tc_mesh_set_gpu_ops().
// ============================================================================

TGFX_API uint32_t tgfx_mesh_upload_gpu(tc_mesh* mesh);
TGFX_API void     tgfx_mesh_draw_gpu(tc_mesh* mesh);
TGFX_API void     tgfx_mesh_delete_gpu(tc_mesh* mesh);

#ifdef __cplusplus
}
#endif

#endif // TGFX_RESOURCE_GPU_H
