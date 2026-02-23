// tc_gpu_context.c - Per-context GPU resource state implementation
#include "tgfx/tc_gpu_context.h"
#include "tgfx/tgfx_gpu_ops.h"
#include <tcbase/tc_log.h>
#include <stdlib.h>
#include <string.h>

// Thread-local current GPU context
static _Thread_local tc_gpu_context* g_current_gpu_context = NULL;

// ============================================================================
// Internal: grow VAO array
// ============================================================================

static bool ensure_vao_capacity(tc_gpu_context* ctx, uint32_t required_index) {
    if (required_index < ctx->mesh_vao_capacity) {
        return true;
    }

    uint32_t new_cap = ctx->mesh_vao_capacity;
    if (new_cap == 0) new_cap = 64;
    while (new_cap <= required_index) {
        new_cap *= 2;
    }

    tc_gpu_vao_slot* new_array = (tc_gpu_vao_slot*)realloc(
        ctx->mesh_vaos, new_cap * sizeof(tc_gpu_vao_slot));
    if (!new_array) {
        tc_log(TC_LOG_ERROR, "tc_gpu_context: vao realloc failed (cap %u -> %u)",
               ctx->mesh_vao_capacity, new_cap);
        return false;
    }

    // Zero-init new slots
    memset(&new_array[ctx->mesh_vao_capacity], 0,
           (new_cap - ctx->mesh_vao_capacity) * sizeof(tc_gpu_vao_slot));

    ctx->mesh_vaos = new_array;
    ctx->mesh_vao_capacity = new_cap;
    return true;
}

// ============================================================================
// Lifecycle
// ============================================================================

tc_gpu_context* tc_gpu_context_new(uintptr_t key, tc_gpu_share_group* group) {
    tc_gpu_context* ctx = (tc_gpu_context*)calloc(1, sizeof(tc_gpu_context));
    if (!ctx) {
        tc_log(TC_LOG_ERROR, "tc_gpu_context_new: alloc failed");
        return NULL;
    }
    ctx->key = key;

    if (group) {
        ctx->share_group = tc_gpu_share_group_ref(group);
    } else {
        // Standalone: create own share group
        ctx->share_group = tc_gpu_share_group_get_or_create(key);
        if (!ctx->share_group) {
            tc_log(TC_LOG_ERROR, "tc_gpu_context_new: failed to create share group");
            free(ctx);
            return NULL;
        }
    }

    return ctx;
}

void tc_gpu_context_free(tc_gpu_context* ctx) {
    if (!ctx) return;

    const tgfx_gpu_ops* ops = tgfx_gpu_get_ops();

    if (ops) {
        // Delete per-context mesh VAOs
        for (uint32_t i = 0; i < ctx->mesh_vao_capacity; i++) {
            if (ctx->mesh_vaos[i].vao != 0 && ops->mesh_delete) {
                ops->mesh_delete(ctx->mesh_vaos[i].vao);
            }
        }

        // Delete per-context backend VAOs
        if (ctx->backend_ui_vao != 0 && ops->mesh_delete) {
            ops->mesh_delete(ctx->backend_ui_vao);
        }
        if (ctx->backend_immediate_vao != 0 && ops->mesh_delete) {
            ops->mesh_delete(ctx->backend_immediate_vao);
        }
    }

    // Unref share group (may free shared GL resources if last ref)
    tc_gpu_share_group_unref(ctx->share_group);

    free(ctx->mesh_vaos);
    free(ctx);
}

// ============================================================================
// Thread-local current context
// ============================================================================

void tc_gpu_set_context(tc_gpu_context* ctx) {
    g_current_gpu_context = ctx;
}

tc_gpu_context* tc_gpu_get_context(void) {
    return g_current_gpu_context;
}

// Default context for standalone paths
static tc_gpu_context* g_default_gpu_context = NULL;

void tc_ensure_default_gpu_context(void) {
    if (!g_current_gpu_context) {
        if (!g_default_gpu_context) {
            g_default_gpu_context = tc_gpu_context_new(0, NULL);
        }
        tc_gpu_set_context(g_default_gpu_context);
    }
}

// ============================================================================
// Slot access (delegates to share_group for shared resources)
// ============================================================================

tc_gpu_slot* tc_gpu_context_texture_slot(tc_gpu_context* ctx, uint32_t index) {
    if (!ctx || !ctx->share_group) return NULL;
    return tc_gpu_share_group_texture_slot(ctx->share_group, index);
}

tc_gpu_slot* tc_gpu_context_shader_slot(tc_gpu_context* ctx, uint32_t index) {
    if (!ctx || !ctx->share_group) return NULL;
    return tc_gpu_share_group_shader_slot(ctx->share_group, index);
}

tc_gpu_vao_slot* tc_gpu_context_vao_slot(tc_gpu_context* ctx, uint32_t index) {
    if (!ctx) return NULL;
    if (!ensure_vao_capacity(ctx, index)) {
        return NULL;
    }
    return &ctx->mesh_vaos[index];
}
