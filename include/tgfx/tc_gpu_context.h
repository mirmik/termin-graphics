// tc_gpu_context.h - Per-context GPU resource state
// Holds per-context VAOs and references shared resources via share_group.
#pragma once

#include "tgfx/tgfx_api.h"
#include "tgfx/tc_gpu_share_group.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// Per-context VAO slot
// ============================================================================

// Per-context VAO with bound VBO/EBO tracking for stale detection.
// When shared VBO/EBO changes, bound_vbo != shared->vbo → recreate VAO.
typedef struct tc_gpu_vao_slot {
    uint32_t vao;
    uint32_t bound_vbo;   // VBO GL id this VAO was created with
    uint32_t bound_ebo;   // EBO GL id this VAO was created with
} tc_gpu_vao_slot;

// ============================================================================
// GPU Context
// ============================================================================

// Per-context GPU state. References shared resources via share_group.
typedef struct tc_gpu_context {
    // Shared resources (textures, shaders, VBO/EBO). Refcounted.
    tc_gpu_share_group* share_group;

    // Per-context mesh VAOs indexed by mesh pool_index
    tc_gpu_vao_slot* mesh_vaos;
    uint32_t mesh_vao_capacity;

    // Backend-specific per-context VAOs
    uint32_t backend_ui_vao;
    uint32_t backend_immediate_vao;

    // Context identity key
    uintptr_t key;
} tc_gpu_context;

// ============================================================================
// Lifecycle
// ============================================================================

// Create new GPU context with given key and share group.
// If group is NULL, creates a standalone share group with the given key.
TGFX_API tc_gpu_context* tc_gpu_context_new(uintptr_t key, tc_gpu_share_group* group);

// Destroy GPU context: delete per-context VAOs, unref share group.
// Must be called with the corresponding GL context active.
TGFX_API void tc_gpu_context_free(tc_gpu_context* ctx);

// ============================================================================
// Thread-local current context
// ============================================================================

// Set current GPU context for this thread (call after glMakeCurrent).
TGFX_API void tc_gpu_set_context(tc_gpu_context* ctx);

// Get current GPU context (NULL if not set).
TGFX_API tc_gpu_context* tc_gpu_get_context(void);

// Ensure a default GPU context exists for the current thread.
// Creates one if none set. Used by standalone paths (launcher, examples)
// where display/render manager doesn't set context explicitly.
TGFX_API void tc_ensure_default_gpu_context(void);

// ============================================================================
// Slot access (delegates to share_group for shared resources)
// ============================================================================

// Get texture slot (delegates to share_group). Grows array if needed.
TGFX_API tc_gpu_slot* tc_gpu_context_texture_slot(tc_gpu_context* ctx, uint32_t index);

// Get shader slot (delegates to share_group). Grows array if needed.
TGFX_API tc_gpu_slot* tc_gpu_context_shader_slot(tc_gpu_context* ctx, uint32_t index);

// Get per-context VAO slot for given mesh pool index. Grows array if needed.
TGFX_API tc_gpu_vao_slot* tc_gpu_context_vao_slot(tc_gpu_context* ctx, uint32_t index);

#ifdef __cplusplus
}
#endif
