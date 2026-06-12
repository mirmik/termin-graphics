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
    TC_SHADER_VARIANT_FOLIAGE = 4,
    TC_SHADER_VARIANT_FOLIAGE_SHADOW = 5,
    TC_SHADER_VARIANT_LINE_MATERIAL_FRAGMENT = 6,
} tc_shader_variant_op;

// ============================================================================
// Shader features (bitflags)
// ============================================================================

typedef enum tc_shader_feature {
    TC_SHADER_FEATURE_NONE = 0,
    TC_SHADER_FEATURE_LIGHTING_UBO = 1 << 0,  // Uses UBO for lighting data
} tc_shader_feature;

// ============================================================================
// Shader source language and artifact policy
// ============================================================================

typedef enum tc_shader_language {
    TC_SHADER_LANGUAGE_GLSL = 0,
    TC_SHADER_LANGUAGE_SLANG = 1,
    TC_SHADER_LANGUAGE_HLSL = 2,
} tc_shader_language;

typedef enum tc_shader_artifact_policy {
    TC_SHADER_ARTIFACT_OPTIONAL = 0,
    TC_SHADER_ARTIFACT_REQUIRED = 1,
} tc_shader_artifact_policy;

// ============================================================================
// Shader data
// ============================================================================

#define TC_SHADER_HASH_LEN 17  // 16 hex chars + null terminator
#define TC_MATERIAL_UBO_NAME_MAX 64
#define TC_MATERIAL_UBO_TYPE_MAX 16
#define TC_SHADER_RESOURCE_NAME_MAX 64

typedef enum tc_shader_resource_kind {
    TC_SHADER_RESOURCE_NONE = 0,
    TC_SHADER_RESOURCE_CONSTANT_BUFFER = 1,
    TC_SHADER_RESOURCE_TEXTURE = 2,
    TC_SHADER_RESOURCE_SAMPLER = 3,
    TC_SHADER_RESOURCE_STORAGE_BUFFER = 4,
    TC_SHADER_RESOURCE_STORAGE_TEXTURE = 5,
} tc_shader_resource_kind;

typedef enum tc_shader_resource_scope {
    TC_SHADER_RESOURCE_SCOPE_UNKNOWN = 0,
    TC_SHADER_RESOURCE_SCOPE_FRAME = 1,
    TC_SHADER_RESOURCE_SCOPE_PASS = 2,
    TC_SHADER_RESOURCE_SCOPE_MATERIAL = 3,
    TC_SHADER_RESOURCE_SCOPE_DRAW = 4,
    TC_SHADER_RESOURCE_SCOPE_TRANSIENT = 5,
} tc_shader_resource_scope;

typedef enum tc_shader_stage_mask {
    TC_SHADER_STAGE_NONE = 0,
    TC_SHADER_STAGE_VERTEX = 1 << 0,
    TC_SHADER_STAGE_FRAGMENT = 1 << 1,
    TC_SHADER_STAGE_GEOMETRY = 1 << 2,
    TC_SHADER_STAGE_COMPUTE = 1 << 3,
    TC_SHADER_STAGE_ALL_GRAPHICS =
        TC_SHADER_STAGE_VERTEX | TC_SHADER_STAGE_FRAGMENT | TC_SHADER_STAGE_GEOMETRY,
    TC_SHADER_STAGE_ALL = 0xffffffffu,
} tc_shader_stage_mask;

// Engine-reserved resource names. These names are part of the Termin shader
// interface; backend set/binding numbers are runtime metadata and should not
// be authored into Slang sources long-term.
#define TC_SHADER_RESOURCE_MATERIAL "material"
#define TC_SHADER_RESOURCE_PER_FRAME "per_frame"
#define TC_SHADER_RESOURCE_DRAW "draw"

#define TC_SHADER_RESOURCE_SET_DEFAULT 0u
#define TC_SHADER_RESOURCE_BINDING_MATERIAL 1u
#define TC_SHADER_RESOURCE_BINDING_PER_FRAME 2u
#define TC_SHADER_RESOURCE_BINDING_LIGHTING 0u
#define TC_SHADER_RESOURCE_BINDING_SHADOW_BLOCK 3u
#define TC_SHADER_RESOURCE_BINDING_SHADOW_MAPS 8u
#define TC_SHADER_RESOURCE_BINDING_DRAW_DATA 24u
#define TC_SHADER_RESOURCE_BINDING_DRAW_STORAGE 25u

// One field inside a constant-buffer resource layout. Offsets and sizes
// are in bytes. Populated from Slang reflection / shader layout sidecar.
typedef struct tc_shader_resource_field {
    char name[TC_SHADER_RESOURCE_NAME_MAX];
    char type[TC_MATERIAL_UBO_TYPE_MAX];
    uint32_t offset;
    uint32_t size;
} tc_shader_resource_field;

typedef struct tc_shader_resource_binding {
    char name[TC_SHADER_RESOURCE_NAME_MAX];
    uint32_t kind;        // tc_shader_resource_kind
    uint32_t scope;       // tc_shader_resource_scope
    uint32_t set;
    uint32_t binding;
    uint32_t stage_mask;  // tc_shader_stage_mask
    uint32_t size;        // bytes for buffers, 0 when unknown/not applicable
    tc_shader_resource_field* fields;
    uint32_t field_count;
} tc_shader_resource_binding;

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
    char* vertex_entry;          // vertex shader entry point (owned, defaults to "main")
    char* fragment_entry;        // fragment shader entry point (owned, defaults to "main")
    char* geometry_entry;        // geometry shader entry point (owned, defaults to "main")
    char source_hash[TC_SHADER_HASH_LEN];  // source + metadata identity hash
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
    uint8_t has_resource_layout; // true once artifact/catalog layout was loaded,
                                 // even if the reflected resource list is empty
    tc_shader_handle original_handle;  // handle to original shader (if is_variant)
    uint32_t original_version;   // version of original when variant was created
    uint32_t features;           // tc_shader_feature bitflags
    uint32_t language;           // tc_shader_language
    uint32_t artifact_policy;    // tc_shader_artifact_policy
    uint32_t pool_index;         // index in shader pool (for GPUContext lookup)

    // Optional std140 material UBO layout, populated by the shader parser
    // when the `.shader` program declares `@features material_ubo`. Consumed
    // by migrated passes via tc_shader_get_material_ubo_* accessors. Owned
    // by the shader; freed on destroy.
    tc_material_ubo_entry* material_ubo_entries;
    uint32_t material_ubo_entry_count;
    uint32_t material_ubo_block_size;

    // Runtime resource layout for this shader. This is the bridge toward
    // backend-neutral Slang sources: shader source names resources, compiled
    // artifact/reflection fills set/binding metadata, runtime binds by name.
    tc_shader_resource_binding* resource_bindings;
    uint32_t resource_binding_count;
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

static inline bool tc_shader_requires_artifacts(const tc_shader* shader) {
    return shader && shader->artifact_policy == TC_SHADER_ARTIFACT_REQUIRED;
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

// Compute legacy source-only FNV-1a hash, truncated to 16 hex chars.
// Registry identity hashing also includes shader language and artifact policy.
// Result is written to hash_out (must be at least TC_SHADER_HASH_LEN bytes)
TGFX_API void tc_shader_compute_hash(
    const char* vertex_source,
    const char* fragment_source,
    const char* geometry_source,  // may be NULL
    char* hash_out
);

// Recompute and update shader's source_hash field
TGFX_API void tc_shader_update_hash(tc_shader* shader);

TGFX_API bool tc_shader_set_language(tc_shader* shader, tc_shader_language language);
TGFX_API tc_shader_language tc_shader_get_language(const tc_shader* shader);
TGFX_API bool tc_shader_set_artifact_policy(tc_shader* shader, tc_shader_artifact_policy policy);
TGFX_API tc_shader_artifact_policy tc_shader_get_artifact_policy(const tc_shader* shader);

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

// ============================================================================
// Shader resource layout
// ============================================================================

// Replace the whole resource layout. A copy of `bindings` is made; caller
// retains ownership. Pass count=0 to clear. Version is not bumped for the same
// reason as material UBO layout: resource layout belongs to the compiled source
// identity and is populated immediately after source/artifact load.
TGFX_API void tc_shader_set_resource_layout(
    tc_shader* shader,
    const tc_shader_resource_binding* bindings,
    uint32_t count
);

TGFX_API uint32_t tc_shader_resource_binding_count(const tc_shader* shader);
TGFX_API const tc_shader_resource_binding* tc_shader_resource_bindings(const tc_shader* shader);
TGFX_API const tc_shader_resource_binding* tc_shader_find_resource_binding(
    const tc_shader* shader,
    const char* name
);
TGFX_API bool tc_shader_has_resource_layout(const tc_shader* shader);
TGFX_API void tc_shader_mark_resource_layout_known(tc_shader* shader);

#ifdef __cplusplus
}
#endif
