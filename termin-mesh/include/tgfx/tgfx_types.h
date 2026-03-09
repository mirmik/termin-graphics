// tgfx_types.h - Vertex layout types for termin-graphics
#ifndef TGFX_TYPES_H
#define TGFX_TYPES_H

#include "tgfx_api.h"
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Vertex attribute data types
typedef enum tgfx_attrib_type {
    TGFX_ATTRIB_FLOAT32 = 0,
    TGFX_ATTRIB_INT32   = 1,
    TGFX_ATTRIB_UINT32  = 2,
    TGFX_ATTRIB_INT16   = 3,
    TGFX_ATTRIB_UINT16  = 4,
    TGFX_ATTRIB_INT8    = 5,
    TGFX_ATTRIB_UINT8   = 6,
} tgfx_attrib_type;

// Draw mode - primitive type for rendering
typedef enum tgfx_draw_mode {
    TGFX_DRAW_TRIANGLES = 0,
    TGFX_DRAW_LINES     = 1,
} tgfx_draw_mode;

// Vertex attribute descriptor
#define TGFX_ATTRIB_NAME_MAX 32
#define TGFX_VERTEX_ATTRIBS_MAX 8

typedef struct tgfx_vertex_attrib {
    char name[TGFX_ATTRIB_NAME_MAX];  // "position", "normal", "uv", "color", ...
    uint8_t size;                      // number of components: 1, 2, 3, 4
    uint8_t type;                      // tgfx_attrib_type
    uint8_t location;                  // shader attribute location (0-15)
    uint8_t _pad;
    uint16_t offset;                   // byte offset from vertex start
} tgfx_vertex_attrib;

// Vertex layout - describes vertex format
typedef struct tgfx_vertex_layout {
    uint16_t stride;                                    // bytes per vertex
    uint8_t attrib_count;                               // number of attributes
    uint8_t _pad;
    tgfx_vertex_attrib attribs[TGFX_VERTEX_ATTRIBS_MAX];
} tgfx_vertex_layout;

// Get size in bytes of an attribute type
TGFX_API size_t tgfx_attrib_type_size(tgfx_attrib_type type);

// Initialize empty layout
TGFX_API void tgfx_vertex_layout_init(tgfx_vertex_layout* layout);

// Add attribute to layout (updates stride automatically)
// Returns false if max attributes reached
TGFX_API bool tgfx_vertex_layout_add(
    tgfx_vertex_layout* layout,
    const char* name,
    uint8_t size,
    tgfx_attrib_type type,
    uint8_t location
);

// Find attribute by name, returns NULL if not found
TGFX_API const tgfx_vertex_attrib* tgfx_vertex_layout_find(
    const tgfx_vertex_layout* layout,
    const char* name
);

// Predefined layouts
TGFX_API tgfx_vertex_layout tgfx_vertex_layout_pos(void);
TGFX_API tgfx_vertex_layout tgfx_vertex_layout_pos_normal(void);
TGFX_API tgfx_vertex_layout tgfx_vertex_layout_pos_normal_uv(void);
TGFX_API tgfx_vertex_layout tgfx_vertex_layout_pos_normal_uv_tangent(void);
TGFX_API tgfx_vertex_layout tgfx_vertex_layout_pos_normal_uv_color(void);
TGFX_API tgfx_vertex_layout tgfx_vertex_layout_skinned(void);

#ifdef __cplusplus
}
#endif

#endif // TGFX_TYPES_H
