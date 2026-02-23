// tc_gpu_share_group.c - Shared GL resource group implementation
#include "tgfx/tc_gpu_share_group.h"
#include "tgfx/tgfx_gpu_ops.h"
#include <tcbase/tc_log.h>
#include <stdlib.h>
#include <string.h>

// ============================================================================
// Global registry of share groups (typically 1-2 in an app)
// ============================================================================

#define TC_MAX_SHARE_GROUPS 16

static tc_gpu_share_group* g_registry[TC_MAX_SHARE_GROUPS];
static int g_registry_count = 0;

// ============================================================================
// Internal: grow array to fit index
// ============================================================================

static bool ensure_capacity(
    void** array,
    uint32_t* capacity,
    uint32_t required_index,
    size_t item_size
) {
    if (required_index < *capacity) {
        return true;
    }

    uint32_t new_cap = *capacity;
    if (new_cap == 0) new_cap = 64;
    while (new_cap <= required_index) {
        new_cap *= 2;
    }

    void* new_array = realloc(*array, new_cap * item_size);
    if (!new_array) {
        tc_log(TC_LOG_ERROR, "tc_gpu_share_group: realloc failed (cap %u -> %u)", *capacity, new_cap);
        return false;
    }

    // Zero-init new slots
    memset((char*)new_array + (*capacity) * item_size, 0,
           (new_cap - *capacity) * item_size);

    // Set version = -1 for new slots
    if (item_size == sizeof(tc_gpu_slot)) {
        tc_gpu_slot* slots = (tc_gpu_slot*)new_array;
        for (uint32_t i = *capacity; i < new_cap; i++) {
            slots[i].version = -1;
        }
    } else if (item_size == sizeof(tc_gpu_mesh_data_slot)) {
        tc_gpu_mesh_data_slot* slots = (tc_gpu_mesh_data_slot*)new_array;
        for (uint32_t i = *capacity; i < new_cap; i++) {
            slots[i].version = -1;
        }
    }

    *array = new_array;
    *capacity = new_cap;
    return true;
}

// ============================================================================
// Lifecycle
// ============================================================================

tc_gpu_share_group* tc_gpu_share_group_get_or_create(uintptr_t key) {
    // Search existing groups
    for (int i = 0; i < g_registry_count; i++) {
        if (g_registry[i] && g_registry[i]->key == key) {
            g_registry[i]->refcount++;
            return g_registry[i];
        }
    }

    // Create new group
    if (g_registry_count >= TC_MAX_SHARE_GROUPS) {
        tc_log(TC_LOG_ERROR, "tc_gpu_share_group: registry full (%d groups)", TC_MAX_SHARE_GROUPS);
        return NULL;
    }

    tc_gpu_share_group* group = (tc_gpu_share_group*)calloc(1, sizeof(tc_gpu_share_group));
    if (!group) {
        tc_log(TC_LOG_ERROR, "tc_gpu_share_group: alloc failed");
        return NULL;
    }

    group->key = key;
    group->refcount = 1;

    g_registry[g_registry_count++] = group;
    return group;
}

tc_gpu_share_group* tc_gpu_share_group_ref(tc_gpu_share_group* group) {
    if (group) {
        group->refcount++;
    }
    return group;
}

void tc_gpu_share_group_unref(tc_gpu_share_group* group) {
    if (!group) return;

    group->refcount--;
    if (group->refcount > 0) return;

    // Last reference — delete all GL resources
    const tgfx_gpu_ops* ops = tgfx_gpu_get_ops();
    if (ops) {
        // Delete textures
        for (uint32_t i = 0; i < group->texture_capacity; i++) {
            if (group->textures[i].gl_id != 0 && ops->texture_delete) {
                ops->texture_delete(group->textures[i].gl_id);
            }
        }
        // Delete shaders
        for (uint32_t i = 0; i < group->shader_capacity; i++) {
            if (group->shaders[i].gl_id != 0 && ops->shader_delete) {
                ops->shader_delete(group->shaders[i].gl_id);
            }
        }
        // Delete mesh VBO/EBO
        for (uint32_t i = 0; i < group->mesh_data_capacity; i++) {
            if (group->mesh_data[i].vbo != 0 && ops->buffer_delete) {
                ops->buffer_delete(group->mesh_data[i].vbo);
            }
            if (group->mesh_data[i].ebo != 0 && ops->buffer_delete) {
                ops->buffer_delete(group->mesh_data[i].ebo);
            }
        }
        // Delete backend VBOs
        if (group->backend_ui_vbo != 0 && ops->buffer_delete) {
            ops->buffer_delete(group->backend_ui_vbo);
        }
        if (group->backend_immediate_vbo != 0 && ops->buffer_delete) {
            ops->buffer_delete(group->backend_immediate_vbo);
        }
    }

    // Remove from registry
    for (int i = 0; i < g_registry_count; i++) {
        if (g_registry[i] == group) {
            g_registry[i] = g_registry[g_registry_count - 1];
            g_registry[g_registry_count - 1] = NULL;
            g_registry_count--;
            break;
        }
    }

    free(group->textures);
    free(group->shaders);
    free(group->mesh_data);
    free(group);
}

// ============================================================================
// Slot access
// ============================================================================

tc_gpu_slot* tc_gpu_share_group_texture_slot(tc_gpu_share_group* g, uint32_t index) {
    if (!g) return NULL;
    if (!ensure_capacity(
            (void**)&g->textures, &g->texture_capacity,
            index, sizeof(tc_gpu_slot))) {
        return NULL;
    }
    return &g->textures[index];
}

tc_gpu_slot* tc_gpu_share_group_shader_slot(tc_gpu_share_group* g, uint32_t index) {
    if (!g) return NULL;
    if (!ensure_capacity(
            (void**)&g->shaders, &g->shader_capacity,
            index, sizeof(tc_gpu_slot))) {
        return NULL;
    }
    return &g->shaders[index];
}

tc_gpu_mesh_data_slot* tc_gpu_share_group_mesh_data_slot(tc_gpu_share_group* g, uint32_t index) {
    if (!g) return NULL;
    if (!ensure_capacity(
            (void**)&g->mesh_data, &g->mesh_data_capacity,
            index, sizeof(tc_gpu_mesh_data_slot))) {
        return NULL;
    }
    return &g->mesh_data[index];
}
