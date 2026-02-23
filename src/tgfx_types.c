#include "tgfx/tgfx_types.h"
#include <string.h>

size_t tgfx_attrib_type_size(tgfx_attrib_type type) {
    switch (type) {
        case TGFX_ATTRIB_FLOAT32: return 4;
        case TGFX_ATTRIB_INT32:   return 4;
        case TGFX_ATTRIB_UINT32:  return 4;
        case TGFX_ATTRIB_INT16:   return 2;
        case TGFX_ATTRIB_UINT16:  return 2;
        case TGFX_ATTRIB_INT8:    return 1;
        case TGFX_ATTRIB_UINT8:   return 1;
        default:                   return 0;
    }
}

void tgfx_vertex_layout_init(tgfx_vertex_layout* layout) {
    if (!layout) return;
    memset(layout, 0, sizeof(tgfx_vertex_layout));
}

bool tgfx_vertex_layout_add(
    tgfx_vertex_layout* layout,
    const char* name,
    uint8_t size,
    tgfx_attrib_type type,
    uint8_t location
) {
    if (!layout || !name) return false;
    if (layout->attrib_count >= TGFX_VERTEX_ATTRIBS_MAX) return false;

    tgfx_vertex_attrib* attr = &layout->attribs[layout->attrib_count];

    strncpy(attr->name, name, TGFX_ATTRIB_NAME_MAX - 1);
    attr->name[TGFX_ATTRIB_NAME_MAX - 1] = '\0';

    attr->size = size;
    attr->type = (uint8_t)type;
    attr->location = location;
    attr->offset = layout->stride;

    layout->stride += (uint16_t)(size * tgfx_attrib_type_size(type));
    layout->attrib_count++;

    return true;
}

const tgfx_vertex_attrib* tgfx_vertex_layout_find(
    const tgfx_vertex_layout* layout,
    const char* name
) {
    if (!layout || !name) return NULL;

    for (uint8_t i = 0; i < layout->attrib_count; i++) {
        if (strcmp(layout->attribs[i].name, name) == 0) {
            return &layout->attribs[i];
        }
    }
    return NULL;
}

// Predefined layouts

tgfx_vertex_layout tgfx_vertex_layout_pos(void) {
    tgfx_vertex_layout layout;
    tgfx_vertex_layout_init(&layout);
    tgfx_vertex_layout_add(&layout, "position", 3, TGFX_ATTRIB_FLOAT32, 0);
    return layout;
}

tgfx_vertex_layout tgfx_vertex_layout_pos_normal(void) {
    tgfx_vertex_layout layout;
    tgfx_vertex_layout_init(&layout);
    tgfx_vertex_layout_add(&layout, "position", 3, TGFX_ATTRIB_FLOAT32, 0);
    tgfx_vertex_layout_add(&layout, "normal", 3, TGFX_ATTRIB_FLOAT32, 1);
    return layout;
}

tgfx_vertex_layout tgfx_vertex_layout_pos_normal_uv(void) {
    tgfx_vertex_layout layout;
    tgfx_vertex_layout_init(&layout);
    tgfx_vertex_layout_add(&layout, "position", 3, TGFX_ATTRIB_FLOAT32, 0);
    tgfx_vertex_layout_add(&layout, "normal", 3, TGFX_ATTRIB_FLOAT32, 1);
    tgfx_vertex_layout_add(&layout, "uv", 2, TGFX_ATTRIB_FLOAT32, 2);
    return layout;
}

tgfx_vertex_layout tgfx_vertex_layout_pos_normal_uv_tangent(void) {
    tgfx_vertex_layout layout;
    tgfx_vertex_layout_init(&layout);
    tgfx_vertex_layout_add(&layout, "position", 3, TGFX_ATTRIB_FLOAT32, 0);
    tgfx_vertex_layout_add(&layout, "normal", 3, TGFX_ATTRIB_FLOAT32, 1);
    tgfx_vertex_layout_add(&layout, "uv", 2, TGFX_ATTRIB_FLOAT32, 2);
    tgfx_vertex_layout_add(&layout, "tangent", 4, TGFX_ATTRIB_FLOAT32, 3);
    return layout;
}

tgfx_vertex_layout tgfx_vertex_layout_pos_normal_uv_color(void) {
    tgfx_vertex_layout layout;
    tgfx_vertex_layout_init(&layout);
    tgfx_vertex_layout_add(&layout, "position", 3, TGFX_ATTRIB_FLOAT32, 0);
    tgfx_vertex_layout_add(&layout, "normal", 3, TGFX_ATTRIB_FLOAT32, 1);
    tgfx_vertex_layout_add(&layout, "uv", 2, TGFX_ATTRIB_FLOAT32, 2);
    tgfx_vertex_layout_add(&layout, "color", 4, TGFX_ATTRIB_FLOAT32, 5);
    return layout;
}

tgfx_vertex_layout tgfx_vertex_layout_skinned(void) {
    tgfx_vertex_layout layout;
    tgfx_vertex_layout_init(&layout);
    tgfx_vertex_layout_add(&layout, "position", 3, TGFX_ATTRIB_FLOAT32, 0);
    tgfx_vertex_layout_add(&layout, "normal", 3, TGFX_ATTRIB_FLOAT32, 1);
    tgfx_vertex_layout_add(&layout, "uv", 2, TGFX_ATTRIB_FLOAT32, 2);
    tgfx_vertex_layout_add(&layout, "joints", 4, TGFX_ATTRIB_FLOAT32, 3);
    tgfx_vertex_layout_add(&layout, "weights", 4, TGFX_ATTRIB_FLOAT32, 4);
    return layout;
}
