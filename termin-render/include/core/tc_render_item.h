#ifndef TC_RENDER_ITEM_H
#define TC_RENDER_ITEM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "core/tc_component.h"
#include "tgfx/resources/tc_material.h"
#include "tgfx/resources/tc_mesh_registry.h"

#ifdef __cplusplus
extern "C" {
#endif

struct tc_mesh;

typedef enum tc_render_item_kind {
    TC_RENDER_ITEM_KIND_INVALID = 0,
    TC_RENDER_ITEM_KIND_MESH = 1,
    TC_RENDER_ITEM_KIND_LINE_BATCH = 2,
    TC_RENDER_ITEM_KIND_TEXT_BATCH = 3,
    TC_RENDER_ITEM_KIND_FOLIAGE_BATCH = 4,
} tc_render_item_kind;

typedef enum tc_render_item_flags {
    TC_RENDER_ITEM_FLAG_NONE = 0,
    TC_RENDER_ITEM_FLAG_HAS_MODEL_MATRIX = 1u << 0,
    TC_RENDER_ITEM_FLAG_HAS_MATERIAL_PHASE = 1u << 1,
    TC_RENDER_ITEM_FLAG_HAS_SKINNING_MATRICES = 1u << 2,
} tc_render_item_flags;

typedef enum tc_render_item_collect_flags {
    TC_RENDER_ITEM_COLLECT_FLAG_NONE = 0,
    TC_RENDER_ITEM_COLLECT_FLAG_ALLOW_MISSING_MATERIAL_PHASE = 1u << 0,
} tc_render_item_collect_flags;

typedef struct tc_render_item_vec3 {
    double x;
    double y;
    double z;
} tc_render_item_vec3;

typedef struct tc_render_item_mesh_payload {
    struct tc_mesh* mesh;
    tc_mesh_handle mesh_handle;
    size_t submesh_index;
    const float* skinning_matrices;
    uint32_t skinning_matrix_count;
} tc_render_item_mesh_payload;

typedef struct tc_render_item_line_batch_payload {
    /* Borrowed unless the item is stored in termin::RenderItemCollection. */
    const tc_render_item_vec3* points;
    size_t point_count;
    float width;
    uint32_t render_mode;
    tc_render_item_vec3 up_hint;
    int32_t tube_sides;
} tc_render_item_line_batch_payload;

typedef union tc_render_item_payload {
    tc_render_item_mesh_payload mesh;
    tc_render_item_line_batch_payload line_batch;
} tc_render_item_payload;

typedef struct tc_render_item {
    uint32_t kind;
    uint32_t flags;
    tc_component* component;
    int geometry_id;
    tc_material_phase* material_phase;
    tc_material_handle material;
    size_t material_phase_index;
    float model_matrix[16];
    tc_render_item_payload payload;
} tc_render_item;

typedef struct tc_render_item_collect_context {
    const char* phase_mark;
    uint32_t flags;
    uint64_t layer_mask;
    uint64_t render_category_mask;
    const char* debug_pass_name;
    const void* pass_contract;
    const void* scene;
    const void* camera;
    void* user_context;
} tc_render_item_collect_context;

typedef bool (*tc_render_item_emit_fn)(
    const tc_render_item* item,
    void* user_data);

typedef struct tc_render_item_sink {
    tc_render_item_emit_fn emit;
    void* user_data;
} tc_render_item_sink;

#ifdef __cplusplus
}
#endif

#endif
