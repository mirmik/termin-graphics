// tc_shader.h - Shader data structures with variant support
#pragma once

#include "tgfx/tgfx_api.h"
#include "tgfx/tc_handle.h"
#include <tcbase/tc_binding_types.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// Shader handle - safe reference to shader in pool
// ============================================================================

// tc_shader_handle is provided by tcbase/tc_binding_types.h
#ifndef TC_SHADER_HANDLE_DEFINED
TC_DEFINE_HANDLE(tc_shader_handle)
#endif

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
#define TC_MATERIAL_UBO_NAME_MAX 64
#define TC_MATERIAL_UBO_TYPE_MAX 16

// One field inside a shader's generated std140 material UBO block.
// Populated by the shader parser (see termin-app/cpp/termin/render/shader_parser.cpp)
// and pushed onto the shader via tc_shader_set_material_ubo_layout() so that
// migrated passes can pack material values into the UBO at draw time.
typedef struct tc_material_ubo_entry {
    char name[TC_MATERIAL_UBO_NAME_MAX];
    char property_type[TC_MATERIAL_UBO_TYPE_MAX];
    uint32_t offset;
    uint32_t size;
} tc_material_ubo_entry;

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
    uint8_t is_static;           // true if registered via tc_shader_register_static
                                 // (engine shader, never destroyed — see
                                 // tc_shader_register_static in tc_shader_registry.h)
    uint8_t _pad[1];
    tc_shader_handle original_handle;  // handle to original shader (if is_variant)
    uint32_t original_version;   // version of original when variant was created
    uint32_t features;           // tc_shader_feature bitflags
    uint32_t pool_index;         // index in shader pool (for GPUContext lookup)

    // Optional std140 material UBO layout, populated by the shader parser
    // when the `.shader` program declares `@features material_ubo`. Consumed
    // by migrated passes via tc_shader_get_material_ubo_* accessors. Owned
    // by the shader; freed on destroy.
    tc_material_ubo_entry* material_ubo_entries;
    uint32_t material_ubo_entry_count;
    uint32_t material_ubo_block_size;
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

// ============================================================================
// Material UBO layout (populated by the shader parser after set_sources)
// ============================================================================

// Replace the shader's material UBO layout. A copy of `entries` is made;
// caller retains ownership of its buffer. Pass count=0 to clear the layout.
// Version is NOT bumped — layout changes travel together with source changes,
// which already bump version via tc_shader_set_sources.
TGFX_API void tc_shader_set_material_ubo_layout(
    tc_shader* shader,
    const tc_material_ubo_entry* entries,
    uint32_t count,
    uint32_t block_size
);

// Read access for consumers (e.g., material UBO packer in migrated passes).
TGFX_API uint32_t tc_shader_material_ubo_entry_count(const tc_shader* shader);
TGFX_API const tc_material_ubo_entry* tc_shader_material_ubo_entries(const tc_shader* shader);
TGFX_API uint32_t tc_shader_material_ubo_block_size(const tc_shader* shader);

#ifdef __cplusplus
}
#endif
