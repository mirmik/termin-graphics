// tgfx_gpu_ops.h - GPU operations vtable
// Allows C code to perform GPU operations via callbacks from rendering backend
#ifndef TGFX_GPU_OPS_H
#define TGFX_GPU_OPS_H

#include "tgfx_api.h"
#include <tgfx/tgfx_types.h>
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

    // Allocate a GPU-only (no CPU pixel data) texture with the given
    // tc_texture_format / tc_texture_usage_flags bitset. Used by render
    // targets and similar attachment-owning textures: the GPU image is
    // created blank and is later written into by render passes. Returns
    // GPU texture ID (0 on failure).
    uint32_t (*texture_create_gpu_only)(
        int width,
        int height,
        int format,         // tc_texture_format
        uint32_t usage      // tc_texture_usage_flags bitset
    );

    // Bind texture to unit
    void (*texture_bind)(uint32_t gpu_id, int unit);

    // Bind depth texture to unit
    void (*depth_texture_bind)(uint32_t gpu_id, int unit);

    // Delete GPU texture
    void (*texture_delete)(uint32_t gpu_id);

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

// Check if GPU ops are available
TGFX_API bool tgfx_gpu_available(void);

#ifdef __cplusplus
}
#endif

#endif // TGFX_GPU_OPS_H
