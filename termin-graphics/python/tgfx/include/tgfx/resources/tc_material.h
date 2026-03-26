// tc_material.h - Material data structures
#pragma once

#include "tgfx/tgfx_api.h"
#include "tgfx/tc_handle.h"
#include "tgfx/resources/tc_resource.h"
#include "tgfx/resources/tc_shader.h"
#include "tgfx/resources/tc_texture.h"
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// Material handle - safe reference to material in pool
// ============================================================================

TC_DEFINE_HANDLE(tc_material_handle)

// ============================================================================
// Render state enums
// ============================================================================

typedef enum tc_polygon_mode {
    TC_POLYGON_FILL = 0,
    TC_POLYGON_LINE = 1
} tc_polygon_mode;

typedef enum tc_blend_factor {
    TC_BLEND_ZERO = 0,
    TC_BLEND_ONE = 1,
    TC_BLEND_SRC_ALPHA = 2,
    TC_BLEND_ONE_MINUS_SRC_ALPHA = 3
} tc_blend_factor;

typedef enum tc_depth_func {
    TC_DEPTH_LESS = 0,
    TC_DEPTH_LEQUAL = 1,
    TC_DEPTH_EQUAL = 2,
    TC_DEPTH_GREATER = 3,
    TC_DEPTH_GEQUAL = 4,
    TC_DEPTH_NOTEQUAL = 5,
    TC_DEPTH_ALWAYS = 6,
    TC_DEPTH_NEVER = 7
} tc_depth_func;

// ============================================================================
// Render state
// ============================================================================

typedef struct tc_render_state {
    uint8_t polygon_mode;    // tc_polygon_mode
    uint8_t cull;            // enable backface culling
    uint8_t depth_test;      // enable depth testing
    uint8_t depth_write;     // write to depth buffer
    uint8_t blend;           // enable alpha blending
    uint8_t blend_src;       // tc_blend_factor for source
    uint8_t blend_dst;       // tc_blend_factor for destination
    uint8_t depth_func;      // tc_depth_func
} tc_render_state;

// Default render states
static inline tc_render_state tc_render_state_opaque(void) {
    tc_render_state s = {0};
    s.polygon_mode = TC_POLYGON_FILL;
    s.cull = 1;
    s.depth_test = 1;
    s.depth_write = 1;
    s.blend = 0;
    s.blend_src = TC_BLEND_SRC_ALPHA;
    s.blend_dst = TC_BLEND_ONE_MINUS_SRC_ALPHA;
    s.depth_func = TC_DEPTH_LESS;
    return s;
}

static inline tc_render_state tc_render_state_transparent(void) {
    tc_render_state s = tc_render_state_opaque();
    s.blend = 1;
    s.depth_write = 0;
    return s;
}

static inline tc_render_state tc_render_state_wireframe(void) {
    tc_render_state s = tc_render_state_opaque();
    s.polygon_mode = TC_POLYGON_LINE;
    s.cull = 0;
    return s;
}

// ============================================================================
// Uniform value types
// ============================================================================

typedef enum tc_uniform_type {
    TC_UNIFORM_NONE = 0,
    TC_UNIFORM_BOOL = 1,
    TC_UNIFORM_INT = 2,
    TC_UNIFORM_FLOAT = 3,
    TC_UNIFORM_VEC2 = 4,
    TC_UNIFORM_VEC3 = 5,
    TC_UNIFORM_VEC4 = 6,
    TC_UNIFORM_MAT4 = 7,
    TC_UNIFORM_FLOAT_ARRAY = 8
} tc_uniform_type;

#define TC_UNIFORM_NAME_MAX 64
#define TC_UNIFORM_ARRAY_MAX 64

typedef struct tc_uniform_value {
    char name[TC_UNIFORM_NAME_MAX];
    uint8_t type;           // tc_uniform_type
    uint8_t array_size;     // for TC_UNIFORM_FLOAT_ARRAY
    uint8_t _pad[2];
    union {
        int32_t i;
        float f;
        float v2[2];
        float v3[3];
        float v4[4];
        float m4[16];
        float arr[TC_UNIFORM_ARRAY_MAX];
    } data;
} tc_uniform_value;

// ============================================================================
// Material texture binding
// ============================================================================

typedef struct tc_material_texture {
    char name[TC_UNIFORM_NAME_MAX];  // uniform name (e.g., "u_albedo")
    tc_texture_handle texture;
} tc_material_texture;

// ============================================================================
// Material phase
// ============================================================================

#define TC_PHASE_MARK_MAX 32
#define TC_MATERIAL_MAX_TEXTURES 16
#define TC_MATERIAL_MAX_UNIFORMS 32
#define TC_MATERIAL_MAX_MARKS 8

typedef struct tc_material_phase {
    tc_shader_handle shader;
    tc_render_state state;
    char phase_mark[TC_PHASE_MARK_MAX];
    int32_t priority;

    // Textures bound to this phase
    tc_material_texture textures[TC_MATERIAL_MAX_TEXTURES];
    size_t texture_count;

    // Uniform values for this phase
    tc_uniform_value uniforms[TC_MATERIAL_MAX_UNIFORMS];
    size_t uniform_count;

    // Available phase marks (for user selection in inspector)
    char available_marks[TC_MATERIAL_MAX_MARKS][TC_PHASE_MARK_MAX];
    size_t available_mark_count;

    // Per-mark render states (optional overrides)
    tc_render_state mark_states[TC_MATERIAL_MAX_MARKS];
    uint8_t mark_state_valid[TC_MATERIAL_MAX_MARKS];  // which mark_states are set
} tc_material_phase;

// ============================================================================
// Material
// ============================================================================

#define TC_MATERIAL_MAX_PHASES 8
#define TC_MATERIAL_NAME_MAX 128

typedef struct tc_material {
    tc_resource_header header;

    // Phases (different render passes: opaque, shadow, transparent, etc.)
    tc_material_phase phases[TC_MATERIAL_MAX_PHASES];
    size_t phase_count;

    // Metadata
    char shader_name[TC_MATERIAL_NAME_MAX];
    char active_phase_mark[TC_PHASE_MARK_MAX];  // force specific phase mark
    const char* source_path;  // interned path to .material file (or NULL)

    // Texture handles for inspector (asset references, separate from phase textures)
    tc_material_texture texture_handles[TC_MATERIAL_MAX_TEXTURES];
    size_t texture_handle_count;
} tc_material;

// ============================================================================
// Phase helpers
// ============================================================================

// Find uniform by name in phase, returns NULL if not found
static inline tc_uniform_value* tc_material_phase_find_uniform(
    tc_material_phase* phase, const char* name
) {
    for (size_t i = 0; i < phase->uniform_count; i++) {
        if (strcmp(phase->uniforms[i].name, name) == 0) {
            return &phase->uniforms[i];
        }
    }
    return NULL;
}

// Find texture by name in phase, returns NULL if not found
static inline tc_material_texture* tc_material_phase_find_texture(
    tc_material_phase* phase, const char* name
) {
    for (size_t i = 0; i < phase->texture_count; i++) {
        if (strcmp(phase->textures[i].name, name) == 0) {
            return &phase->textures[i];
        }
    }
    return NULL;
}

// Add or update uniform in phase
TGFX_API bool tc_material_phase_set_uniform(
    tc_material_phase* phase,
    const char* name,
    tc_uniform_type type,
    const void* value
);

// Add or update texture in phase
TGFX_API bool tc_material_phase_set_texture(
    tc_material_phase* phase,
    const char* name,
    tc_texture_handle texture
);

// Get color (u_color uniform) from phase
TGFX_API bool tc_material_phase_get_color(
    const tc_material_phase* phase,
    float* r, float* g, float* b, float* a
);

// Set color (u_color uniform) on phase
TGFX_API void tc_material_phase_set_color(
    tc_material_phase* phase,
    float r, float g, float b, float a
);

// Set transparent render state (blend=ON, depth_write=OFF) on phase
TGFX_API void tc_material_phase_make_transparent(tc_material_phase* phase);

// ============================================================================
// Material helpers
// ============================================================================

// Get default (first) phase
static inline tc_material_phase* tc_material_default_phase(tc_material* mat) {
    return mat->phase_count > 0 ? &mat->phases[0] : NULL;
}

// Find phase by mark
TGFX_API tc_material_phase* tc_material_find_phase(tc_material* mat, const char* mark);

// Set uniform on all phases
TGFX_API void tc_material_set_uniform(
    tc_material* mat,
    const char* name,
    tc_uniform_type type,
    const void* value
);

// Set texture on all phases and store handle for inspector
TGFX_API void tc_material_set_texture(
    tc_material* mat,
    const char* name,
    tc_texture_handle texture
);

// Get color from default phase
TGFX_API bool tc_material_get_color(
    const tc_material* mat,
    float* r, float* g, float* b, float* a
);

// Set color on all phases
TGFX_API void tc_material_set_color(
    tc_material* mat,
    float r, float g, float b, float a
);

// ============================================================================
// Reference counting
// ============================================================================

TGFX_API void tc_material_add_ref(tc_material* mat);
TGFX_API bool tc_material_release(tc_material* mat);

#ifdef __cplusplus
}
#endif
