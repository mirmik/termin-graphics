// tc_shader.h - Shader data structures with variant support
#pragma once

#include "tgfx/tgfx_api.h"
#include "tgfx/tc_handle.h"
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// Shader handle - safe reference to shader in pool
// ============================================================================

TC_DEFINE_HANDLE(tc_shader_handle)

// ============================================================================
// Shader variant operations
// ============================================================================

typedef enum tc_shader_variant_op {
    TC_SHADER_VARIANT_NONE = 0,
    TC_SHADER_VARIANT_SKINNING = 1,
    TC_SHADER_VARIANT_INSTANCING = 2,
    TC_SHADER_VARIANT_MORPHING = 3,
} tc_shader_variant_op;

// ============================================================================
// Shader features (bitflags)
// ============================================================================

typedef enum tc_shader_feature {
    TC_SHADER_FEATURE_NONE = 0,
    TC_SHADER_FEATURE_LIGHTING_UBO = 1 << 0,  // Uses UBO for lighting data
} tc_shader_feature;

// ============================================================================
// Shader data
// ============================================================================

#define TC_SHADER_HASH_LEN 17  // 16 hex chars + null terminator

typedef struct tc_shader {
    char* vertex_source;         // vertex shader source (owned)
    char* fragment_source;       // fragment shader source (owned)
    char* geometry_source;       // geometry shader source (owned, may be NULL)
    char source_hash[TC_SHADER_HASH_LEN];  // hash of sources for variant lookup
    uint32_t version;            // incremented on source change
    uint32_t ref_count;          // reference count for ownership
    char uuid[40];               // unique identifier
    const char* name;            // human-readable name (interned string)
    const char* source_path;     // optional source file path (interned string)
    uint8_t is_variant;          // true if this is a derived variant
    uint8_t variant_op;          // tc_shader_variant_op if is_variant
    uint8_t _pad[2];
    tc_shader_handle original_handle;  // handle to original shader (if is_variant)
    uint32_t original_version;   // version of original when variant was created
    uint32_t features;           // tc_shader_feature bitflags
    uint32_t pool_index;         // index in shader pool (for GPUContext lookup)
} tc_shader;

// ============================================================================
// Helper functions
// ============================================================================

// Calculate total source size in bytes
static inline size_t tc_shader_source_size(const tc_shader* shader) {
    size_t size = 0;
    if (shader->vertex_source) size += strlen(shader->vertex_source) + 1;
    if (shader->fragment_source) size += strlen(shader->fragment_source) + 1;
    if (shader->geometry_source) size += strlen(shader->geometry_source) + 1;
    return size;
}

// Check if shader has geometry stage
static inline bool tc_shader_has_geometry(const tc_shader* shader) {
    return shader->geometry_source != NULL && shader->geometry_source[0] != '\0';
}

// Check if shader has a specific feature
static inline bool tc_shader_has_feature(const tc_shader* shader, tc_shader_feature feature) {
    return (shader->features & feature) != 0;
}

// Set shader feature
static inline void tc_shader_set_feature(tc_shader* shader, tc_shader_feature feature) {
    shader->features |= feature;
}

// Clear shader feature
static inline void tc_shader_clear_feature(tc_shader* shader, tc_shader_feature feature) {
    shader->features &= ~feature;
}

// ============================================================================
// Reference counting
// ============================================================================

// Increment reference count
TGFX_API void tc_shader_add_ref(tc_shader* shader);

// Decrement reference count. Returns true if shader was destroyed
TGFX_API bool tc_shader_release(tc_shader* shader);

// ============================================================================
// Hash computation
// ============================================================================

// Compute hash from shader sources (SHA256 truncated to 16 hex chars)
// Result is written to hash_out (must be at least TC_SHADER_HASH_LEN bytes)
TGFX_API void tc_shader_compute_hash(
    const char* vertex_source,
    const char* fragment_source,
    const char* geometry_source,  // may be NULL
    char* hash_out
);

// Recompute and update shader's source_hash field
TGFX_API void tc_shader_update_hash(tc_shader* shader);

#ifdef __cplusplus
}
#endif
