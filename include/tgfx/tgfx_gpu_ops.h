// tgfx_gpu_ops.h - GPU operations vtable
// Allows C code to perform GPU operations via callbacks from rendering backend
#ifndef TGFX_GPU_OPS_H
#define TGFX_GPU_OPS_H

#include "tgfx_api.h"
#include "tgfx_types.h"
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct tgfx_gpu_ops {
    // Texture operations
    // Upload texture to GPU, returns GPU texture ID (0 on failure)
    uint32_t (*texture_upload)(
        const uint8_t* data,
        int width,
        int height,
        int channels,
        bool mipmap,
        bool clamp
    );

    // Upload depth texture to GPU (for shadow maps), returns GPU texture ID (0 on failure)
    // If compare_mode is true, enables GL_TEXTURE_COMPARE_MODE for sampler2DShadow
    uint32_t (*depth_texture_upload)(
        const float* data,
        int width,
        int height,
        bool compare_mode
    );

    // Bind texture to unit
    void (*texture_bind)(uint32_t gpu_id, int unit);

    // Bind depth texture to unit
    void (*depth_texture_bind)(uint32_t gpu_id, int unit);

    // Delete GPU texture
    void (*texture_delete)(uint32_t gpu_id);

    // Shader operations
    // Preprocess shader source (resolve #include), returns new string (caller frees) or NULL
    char* (*shader_preprocess)(const char* source, const char* source_name);

    // Compile shader, returns GPU program ID (0 on failure)
    uint32_t (*shader_compile)(
        const char* vertex_source,
        const char* fragment_source,
        const char* geometry_source  // may be NULL
    );

    // Use shader program
    void (*shader_use)(uint32_t gpu_id);

    // Delete shader program
    void (*shader_delete)(uint32_t gpu_id);

    // Uniform setters (gpu_id must be active program)
    void (*shader_set_int)(uint32_t gpu_id, const char* name, int value);
    void (*shader_set_float)(uint32_t gpu_id, const char* name, float value);
    void (*shader_set_vec2)(uint32_t gpu_id, const char* name, float x, float y);
    void (*shader_set_vec3)(uint32_t gpu_id, const char* name, float x, float y, float z);
    void (*shader_set_vec4)(uint32_t gpu_id, const char* name, float x, float y, float z, float w);
    void (*shader_set_mat4)(uint32_t gpu_id, const char* name, const float* data, bool transpose);
    void (*shader_set_mat4_array)(uint32_t gpu_id, const char* name, const float* data, int count, bool transpose);
    void (*shader_set_block_binding)(uint32_t gpu_id, const char* block_name, int binding_point);

    // Mesh operations
    // Upload mesh to GPU (creates VBO+EBO+VAO), returns GPU VAO ID (0 on failure)
    // Outputs VBO/EBO IDs through out_vbo/out_ebo pointers.
    uint32_t (*mesh_upload)(
        const void* vertex_data,
        size_t vertex_count,
        const uint32_t* indices,
        size_t index_count,
        const tgfx_vertex_layout* layout,
        uint32_t* out_vbo,
        uint32_t* out_ebo
    );

    // Draw mesh (binds given VAO and calls glDrawElements)
    void (*mesh_draw)(uint32_t vao, size_t index_count, tgfx_draw_mode mode);

    // Delete GPU mesh VAO
    void (*mesh_delete)(uint32_t gpu_id);

    // Create VAO from existing shared VBO/EBO (for additional GL contexts).
    // Returns new VAO ID (0 on failure).
    uint32_t (*mesh_create_vao)(const tgfx_vertex_layout* layout, uint32_t vbo, uint32_t ebo);

    // Delete a GL buffer object (VBO/EBO/UBO)
    void (*buffer_delete)(uint32_t buffer_id);

    // User data (passed to callbacks if needed)
    void* user_data;
} tgfx_gpu_ops;

// Set the GPU operations vtable (called by rendering backend during init)
TGFX_API void tgfx_gpu_set_ops(const tgfx_gpu_ops* ops);

// Get current GPU operations vtable (returns NULL if not set)
TGFX_API const tgfx_gpu_ops* tgfx_gpu_get_ops(void);

// Set shader preprocessor callback
typedef char* (*tgfx_shader_preprocess_fn)(const char* source, const char* source_name);
TGFX_API void tgfx_gpu_set_shader_preprocess(tgfx_shader_preprocess_fn fn);

// Check if GPU ops are available
TGFX_API bool tgfx_gpu_available(void);

#ifdef __cplusplus
}
#endif

#endif // TGFX_GPU_OPS_H
