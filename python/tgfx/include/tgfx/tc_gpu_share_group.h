// tc_gpu_share_group.h - Shared GL resources for one OpenGL share group
// Textures, shaders, VBO/EBO are shared across all contexts in the group.
// Refcounted — freed when the last context in the group is destroyed.
#pragma once

#include "tgfx/tgfx_api.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// GPU slot (shared resource: texture or shader)
// ============================================================================

typedef struct tc_gpu_slot {
    uint32_t gl_id;
    int32_t  version;    // -1 = never uploaded
} tc_gpu_slot;

// ============================================================================
// Shared mesh data (VBO + EBO + version, without per-context VAO)
// ============================================================================

typedef struct tc_gpu_mesh_data_slot {
    uint32_t vbo;
    uint32_t ebo;
    int32_t  version;    // -1 = never uploaded
} tc_gpu_mesh_data_slot;

// ============================================================================
// Share Group
// ============================================================================

typedef struct tc_gpu_share_group {
    // Texture GL IDs indexed by texture pool_index
    tc_gpu_slot* textures;
    uint32_t texture_capacity;

    // Shader program IDs indexed by shader pool_index
    tc_gpu_slot* shaders;
    uint32_t shader_capacity;

    // Mesh shared data (VBO/EBO/version) indexed by mesh pool_index
    tc_gpu_mesh_data_slot* mesh_data;
    uint32_t mesh_data_capacity;

    // Backend-specific shared resources (VBOs)
    uint32_t backend_ui_vbo;
    uint32_t backend_immediate_vbo;

    // Share group identity key
    uintptr_t key;

    // Reference count (number of tc_gpu_context* using this group)
    int32_t refcount;
} tc_gpu_share_group;

// ============================================================================
// Lifecycle
// ============================================================================

// Get existing share group by key, or create a new one.
// Increments refcount on returned group.
TGFX_API tc_gpu_share_group* tc_gpu_share_group_get_or_create(uintptr_t key);

// Increment refcount.
TGFX_API tc_gpu_share_group* tc_gpu_share_group_ref(tc_gpu_share_group* group);

// Decrement refcount. When it reaches 0, delete all GL resources and free.
// Must be called with a valid GL context active.
TGFX_API void tc_gpu_share_group_unref(tc_gpu_share_group* group);

// ============================================================================
// Slot access (auto-grow arrays if needed)
// ============================================================================

TGFX_API tc_gpu_slot* tc_gpu_share_group_texture_slot(tc_gpu_share_group* g, uint32_t index);
TGFX_API tc_gpu_slot* tc_gpu_share_group_shader_slot(tc_gpu_share_group* g, uint32_t index);
TGFX_API tc_gpu_mesh_data_slot* tc_gpu_share_group_mesh_data_slot(tc_gpu_share_group* g, uint32_t index);

#ifdef __cplusplus
}
#endif
