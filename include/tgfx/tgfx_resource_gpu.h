// tgfx_resource_gpu.h - GPU operations for resources (texture, shader, mesh)
// High-level wrappers that use tgfx_gpu_ops vtable + tc_gpu_context.
#ifndef TGFX_RESOURCE_GPU_H
#define TGFX_RESOURCE_GPU_H

#include "tgfx_api.h"
#include "resources/tc_texture.h"
#include "resources/tc_shader.h"
#include "resources/tc_mesh.h"
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
// Shader GPU operations
// ============================================================================

// Compile shader if not already compiled
// Returns GPU program ID (0 on failure)
TGFX_API uint32_t tc_shader_compile_gpu(tc_shader* shader);

// Use shader program
TGFX_API void tc_shader_use_gpu(tc_shader* shader);

// Delete shader from GPU
TGFX_API void tc_shader_delete_gpu(tc_shader* shader);

// Uniform setters (shader must be in use)
TGFX_API void tc_shader_set_int(tc_shader* shader, const char* name, int value);
TGFX_API void tc_shader_set_float(tc_shader* shader, const char* name, float value);
TGFX_API void tc_shader_set_vec2(tc_shader* shader, const char* name, float x, float y);
TGFX_API void tc_shader_set_vec3(tc_shader* shader, const char* name, float x, float y, float z);
TGFX_API void tc_shader_set_vec4(tc_shader* shader, const char* name, float x, float y, float z, float w);
TGFX_API void tc_shader_set_mat4(tc_shader* shader, const char* name, const float* data, bool transpose);
TGFX_API void tc_shader_set_mat4_array(tc_shader* shader, const char* name, const float* data, int count, bool transpose);
TGFX_API void tc_shader_set_block_binding(tc_shader* shader, const char* block_name, int binding_point);

// ============================================================================
// Mesh GPU operations
// ============================================================================

// Upload mesh to GPU if not already uploaded
// Returns GPU VAO ID (0 on failure)
TGFX_API uint32_t tc_mesh_upload_gpu(tc_mesh* mesh);

// Draw mesh (must be uploaded first)
TGFX_API void tc_mesh_draw_gpu(tc_mesh* mesh);

// Delete mesh from GPU
TGFX_API void tc_mesh_delete_gpu(tc_mesh* mesh);

#ifdef __cplusplus
}
#endif

#endif // TGFX_RESOURCE_GPU_H
