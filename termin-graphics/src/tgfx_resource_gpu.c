// tgfx_resource_gpu.c - GPU operations for resources (texture, shader, mesh)
// Uses tgfx_gpu_ops vtable and tc_gpu_context.
#include <tgfx/tgfx_resource_gpu.h>
#include <tgfx/tgfx_gpu_ops.h>
#include <tgfx/tc_gpu_context.h>
#include <tgfx/tc_gpu_share_group.h>
#include <tgfx/resources/tc_mesh_registry.h>
#include <tcbase/tc_log.h>
#include <string.h>
#include <stdlib.h>

// ============================================================================
// Shader preprocessor
// ============================================================================

static tgfx_shader_preprocess_fn g_shader_preprocess = NULL;

void tgfx_gpu_set_shader_preprocess(tgfx_shader_preprocess_fn fn) {
    g_shader_preprocess = fn;
}

tgfx_shader_preprocess_fn tgfx_gpu_get_shader_preprocess(void) {
    return g_shader_preprocess;
}

// ============================================================================
// Helpers: get GL IDs from current GPU context
// ============================================================================

static uint32_t texture_current_gpu_id(const tc_texture* tex) {
    tc_gpu_context* ctx = tc_gpu_get_context();
    if (!ctx) return 0;
    tc_gpu_slot* slot = tc_gpu_context_texture_slot(ctx, tex->header.pool_index);
    return slot ? slot->gl_id : 0;
}

// ============================================================================
// Texture GPU operations
// ============================================================================

bool tc_texture_needs_upload(const tc_texture* tex) {
    if (!tex) return false;
    tc_gpu_context* ctx = tc_gpu_get_context();
    if (!ctx) return true;
    tc_gpu_slot* slot = tc_gpu_context_texture_slot(ctx, tex->header.pool_index);
    if (!slot) return true;
    return slot->gl_id == 0 || slot->version != (int32_t)tex->header.version;
}

bool tc_texture_upload_gpu(tc_texture* tex) {
    if (!tex || !tex->data) {
        return false;
    }

    const tgfx_gpu_ops* ops = tgfx_gpu_get_ops();
    if (!ops) {
        tc_log_error("tc_texture_upload_gpu: GPU ops not set");
        return false;
    }

    tc_gpu_context* ctx = tc_gpu_get_context();
    if (!ctx) {
        tc_log_error("tc_texture_upload_gpu: no GPUContext set");
        return false;
    }

    tc_gpu_slot* slot = tc_gpu_context_texture_slot(ctx, tex->header.pool_index);
    if (!slot) {
        tc_log_error("tc_texture_upload_gpu: failed to get context slot");
        return false;
    }

    // Already up to date?
    if (slot->gl_id != 0 && slot->version == (int32_t)tex->header.version) {
        return true;
    }

    // Delete old GPU texture if exists
    if (slot->gl_id != 0 && ops->texture_delete) {
        ops->texture_delete(slot->gl_id);
    }

    uint32_t gpu_id = 0;

    // Check if this is a depth texture
    if (tex->format == TC_TEXTURE_DEPTH24) {
        if (!ops->depth_texture_upload) {
            tc_log_error("tc_texture_upload_gpu: depth_texture_upload not set");
            return false;
        }
        gpu_id = ops->depth_texture_upload(
            (const float*)tex->data,
            (int)tex->width,
            (int)tex->height,
            tex->compare_mode != 0
        );
    } else {
        if (!ops->texture_upload) {
            tc_log_error("tc_texture_upload_gpu: texture_upload not set");
            return false;
        }
        gpu_id = ops->texture_upload(
            (const uint8_t*)tex->data,
            (int)tex->width,
            (int)tex->height,
            (int)tex->channels,
            tex->mipmap != 0,
            tex->clamp != 0
        );
    }

    if (gpu_id == 0) {
        tc_log_error("tc_texture_upload_gpu: upload failed for '%s'",
               tex->header.name ? tex->header.name : tex->header.uuid);
        return false;
    }

    // Store in context slot
    slot->gl_id = gpu_id;
    slot->version = (int32_t)tex->header.version;
    return true;
}

bool tc_texture_bind_gpu(tc_texture* tex, int unit) {
    if (!tex) {
        return false;
    }

    const tgfx_gpu_ops* ops = tgfx_gpu_get_ops();
    if (!ops) {
        tc_log_error("tc_texture_bind_gpu: GPU ops not set");
        return false;
    }

    // Upload if needed
    if (tc_texture_needs_upload(tex)) {
        if (!tc_texture_upload_gpu(tex)) {
            return false;
        }
    }

    uint32_t gpu_id = texture_current_gpu_id(tex);
    if (gpu_id == 0) return false;

    // Bind using appropriate function for texture type
    if (tex->format == TC_TEXTURE_DEPTH24) {
        if (!ops->depth_texture_bind) {
            tc_log_error("tc_texture_bind_gpu: depth_texture_bind not set");
            return false;
        }
        ops->depth_texture_bind(gpu_id, unit);
    } else {
        if (!ops->texture_bind) {
            tc_log_error("tc_texture_bind_gpu: texture_bind not set");
            return false;
        }
        ops->texture_bind(gpu_id, unit);
    }
    return true;
}

void tc_texture_delete_gpu(tc_texture* tex) {
    if (!tex) return;

    tc_gpu_context* ctx = tc_gpu_get_context();
    if (!ctx) {
        return;
    }

    const tgfx_gpu_ops* ops = tgfx_gpu_get_ops();
    tc_gpu_slot* slot = tc_gpu_context_texture_slot(ctx, tex->header.pool_index);
    if (slot) {
        if (slot->gl_id != 0 && ops && ops->texture_delete) {
            ops->texture_delete(slot->gl_id);
        }
        slot->gl_id = 0;
        slot->version = -1;
    }
}

// ============================================================================
// Mesh GPU operations
// ============================================================================

uint32_t tgfx_mesh_upload_gpu(tc_mesh* mesh) {
    if (!mesh) {
        return 0;
    }

    // Trigger lazy loading if mesh is declared but not yet loaded
    if (!mesh->header.is_loaded) {
        tc_mesh_ensure_loaded_ptr(mesh);
    }

    if (!mesh->vertices) {
        return 0;
    }

    const tgfx_gpu_ops* ops = tgfx_gpu_get_ops();
    if (!ops || !ops->mesh_upload) {
        tc_log_error("tgfx_mesh_upload_gpu: GPU ops not set");
        return 0;
    }

    tc_gpu_context* ctx = tc_gpu_get_context();
    if (!ctx) {
        tc_log_error("tgfx_mesh_upload_gpu: no GPUContext set");
        return 0;
    }

    tc_gpu_share_group* group = ctx->share_group;
    tc_gpu_mesh_data_slot* shared = tc_gpu_share_group_mesh_data_slot(group, mesh->header.pool_index);
    tc_gpu_vao_slot* vao_slot = tc_gpu_context_vao_slot(ctx, mesh->header.pool_index);
    if (!shared || !vao_slot) {
        tc_log_error("tgfx_mesh_upload_gpu: failed to get slots");
        return 0;
    }

    bool data_current = (shared->vbo != 0 &&
                         shared->version == (int32_t)mesh->header.version);

    if (data_current) {
        // VBO/EBO data is up to date. Check if VAO exists and is not stale.
        if (vao_slot->vao != 0 &&
            vao_slot->bound_vbo == shared->vbo &&
            vao_slot->bound_ebo == shared->ebo) {
            return vao_slot->vao;
        }

        // VAO missing or stale — (re)create from shared VBO/EBO
        if (vao_slot->vao != 0 && ops->mesh_delete) {
            ops->mesh_delete(vao_slot->vao);
        }

        uint32_t vao = 0;
        if (ops->mesh_create_vao) {
            vao = ops->mesh_create_vao(&mesh->layout, shared->vbo, shared->ebo);
        }
        if (vao == 0) {
            tc_log_error("tgfx_mesh_upload_gpu: mesh_create_vao failed for '%s'",
                   mesh->header.name ? mesh->header.name : mesh->header.uuid);
            return 0;
        }

        vao_slot->vao = vao;
        vao_slot->bound_vbo = shared->vbo;
        vao_slot->bound_ebo = shared->ebo;
        return vao;
    }

    // VBO/EBO data needs upload (first time or version changed).
    // Delete existing per-context VAO
    if (vao_slot->vao != 0 && ops->mesh_delete) {
        ops->mesh_delete(vao_slot->vao);
        vao_slot->vao = 0;
        vao_slot->bound_vbo = 0;
        vao_slot->bound_ebo = 0;
    }
    // Delete old shared VBO/EBO
    if (shared->vbo != 0 && ops->buffer_delete) {
        ops->buffer_delete(shared->vbo);
    }
    if (shared->ebo != 0 && ops->buffer_delete) {
        ops->buffer_delete(shared->ebo);
    }
    shared->vbo = 0;
    shared->ebo = 0;

    // Full upload: creates VBO + EBO + VAO, outputs VBO/EBO through pointers
    uint32_t out_vbo = 0, out_ebo = 0;
    uint32_t vao = ops->mesh_upload(
        mesh->vertices,
        mesh->vertex_count,
        mesh->indices,
        mesh->index_count,
        &mesh->layout,
        &out_vbo,
        &out_ebo
    );
    if (vao == 0) {
        tc_log_error("tgfx_mesh_upload_gpu: upload failed for '%s'",
               mesh->header.name ? mesh->header.name : mesh->header.uuid);
        return 0;
    }

    // Store shared data
    shared->vbo = out_vbo;
    shared->ebo = out_ebo;
    shared->version = (int32_t)mesh->header.version;

    // Store per-context VAO
    vao_slot->vao = vao;
    vao_slot->bound_vbo = out_vbo;
    vao_slot->bound_ebo = out_ebo;

    return vao;
}

void tgfx_mesh_draw_gpu(tc_mesh* mesh) {
    if (!mesh) {
        return;
    }

    tc_gpu_context* ctx = tc_gpu_get_context();
    if (!ctx) {
        return;
    }

    tc_gpu_share_group* group = ctx->share_group;
    tc_gpu_mesh_data_slot* shared = tc_gpu_share_group_mesh_data_slot(group, mesh->header.pool_index);
    tc_gpu_vao_slot* vao_slot = tc_gpu_context_vao_slot(ctx, mesh->header.pool_index);
    if (!shared || !vao_slot) {
        return;
    }

    // Check if VBO/EBO data is current
    bool data_stale = (shared->vbo == 0 ||
                       shared->version != (int32_t)mesh->header.version);

    if (data_stale || vao_slot->vao == 0 ||
        vao_slot->bound_vbo != shared->vbo ||
        vao_slot->bound_ebo != shared->ebo) {
        // Need upload or VAO recreation
        if (tgfx_mesh_upload_gpu(mesh) == 0) {
            return;
        }
    }

    uint32_t vao = vao_slot->vao;
    if (vao == 0) return;

    const tgfx_gpu_ops* ops = tgfx_gpu_get_ops();
    if (ops && ops->mesh_draw) {
        ops->mesh_draw(vao, mesh->index_count, (tgfx_draw_mode)mesh->draw_mode);
    }
}

void tgfx_mesh_delete_gpu(tc_mesh* mesh) {
    if (!mesh) {
        return;
    }

    tc_gpu_context* ctx = tc_gpu_get_context();
    if (!ctx) {
        return;
    }

    const tgfx_gpu_ops* ops = tgfx_gpu_get_ops();

    // Delete per-context VAO
    tc_gpu_vao_slot* vao_slot = tc_gpu_context_vao_slot(ctx, mesh->header.pool_index);
    if (vao_slot) {
        if (vao_slot->vao != 0 && ops && ops->mesh_delete) {
            ops->mesh_delete(vao_slot->vao);
        }
        vao_slot->vao = 0;
        vao_slot->bound_vbo = 0;
        vao_slot->bound_ebo = 0;
    }

    // Delete shared VBO/EBO
    tc_gpu_share_group* group = ctx->share_group;
    tc_gpu_mesh_data_slot* shared = tc_gpu_share_group_mesh_data_slot(group, mesh->header.pool_index);
    if (shared && ops) {
        if (shared->vbo != 0 && ops->buffer_delete) {
            ops->buffer_delete(shared->vbo);
        }
        if (shared->ebo != 0 && ops->buffer_delete) {
            ops->buffer_delete(shared->ebo);
        }
        shared->vbo = 0;
        shared->ebo = 0;
        shared->version = -1;
    }
}

