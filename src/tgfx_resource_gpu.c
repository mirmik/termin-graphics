// tgfx_resource_gpu.c - GPU operations for resources (texture, shader, mesh)
// Uses tgfx_gpu_ops vtable and tc_gpu_context.
#include <tgfx/tgfx_resource_gpu.h>
#include <tgfx/tgfx_gpu_ops.h>
#include <tgfx/tc_gpu_context.h>
#include <tgfx/tc_gpu_share_group.h>
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

// ============================================================================
// Helpers: get GL IDs from current GPU context
// ============================================================================

static uint32_t shader_current_program(const tc_shader* shader) {
    tc_gpu_context* ctx = tc_gpu_get_context();
    if (!ctx) return 0;
    tc_gpu_slot* slot = tc_gpu_context_shader_slot(ctx, shader->pool_index);
    return slot ? slot->gl_id : 0;
}

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
// Shader GPU operations
// ============================================================================

uint32_t tc_shader_compile_gpu(tc_shader* shader) {
    if (!shader) {
        tc_log_error("tc_shader_compile_gpu: shader is NULL");
        return 0;
    }

    const tgfx_gpu_ops* ops = tgfx_gpu_get_ops();
    if (!ops || !ops->shader_compile) {
        tc_log_error("tc_shader_compile_gpu: GPU ops not set");
        return 0;
    }

    tc_gpu_context* ctx = tc_gpu_get_context();
    if (!ctx) {
        tc_log_error("tc_shader_compile_gpu: no GPUContext set");
        return 0;
    }

    tc_gpu_slot* slot = tc_gpu_context_shader_slot(ctx, shader->pool_index);
    if (!slot) {
        tc_log_error("tc_shader_compile_gpu: failed to get context slot");
        return 0;
    }

    // Already compiled and up to date?
    if (slot->gl_id != 0 && slot->version == (int32_t)shader->version) {
        return slot->gl_id;
    }

    // Check if sources are available
    if (!shader->vertex_source || !shader->fragment_source) {
        tc_log_error("tc_shader_compile_gpu: missing sources for '%s' (vertex=%p, fragment=%p)",
               shader->name ? shader->name : shader->uuid,
               (void*)shader->vertex_source, (void*)shader->fragment_source);
        return 0;
    }

    // Delete old program if exists
    if (slot->gl_id != 0 && ops->shader_delete) {
        ops->shader_delete(slot->gl_id);
    }

    // Preprocess sources if preprocessor is available
    const char* vertex_src = shader->vertex_source;
    const char* fragment_src = shader->fragment_source;
    const char* geometry_src = shader->geometry_source;

    char* preprocessed_vertex = NULL;
    char* preprocessed_fragment = NULL;
    char* preprocessed_geometry = NULL;

    const char* shader_name = shader->name ? shader->name : shader->uuid;

    if (g_shader_preprocess) {
        if (vertex_src && strstr(vertex_src, "#include")) {
            preprocessed_vertex = g_shader_preprocess(vertex_src, shader_name);
            if (preprocessed_vertex) {
                vertex_src = preprocessed_vertex;
            }
        }
        if (fragment_src && strstr(fragment_src, "#include")) {
            preprocessed_fragment = g_shader_preprocess(fragment_src, shader_name);
            if (preprocessed_fragment) {
                fragment_src = preprocessed_fragment;
            }
        }
        if (geometry_src && strstr(geometry_src, "#include")) {
            preprocessed_geometry = g_shader_preprocess(geometry_src, shader_name);
            if (preprocessed_geometry) {
                geometry_src = preprocessed_geometry;
            }
        }
    }

    // Compile new program
    uint32_t program = ops->shader_compile(
        vertex_src,
        fragment_src,
        geometry_src
    );

    // Free preprocessed sources
    free(preprocessed_vertex);
    free(preprocessed_fragment);
    free(preprocessed_geometry);

    if (program == 0) {
        tc_log_error("tc_shader_compile_gpu: compile failed for '%s'",
               shader_name);
        return 0;
    }

    // Store in context slot
    slot->gl_id = program;
    slot->version = (int32_t)shader->version;
    return program;
}

void tc_shader_use_gpu(tc_shader* shader) {
    if (!shader) {
        return;
    }

    tc_gpu_context* ctx = tc_gpu_get_context();
    if (!ctx) {
        return;
    }

    const tgfx_gpu_ops* ops = tgfx_gpu_get_ops();
    tc_gpu_slot* slot = tc_gpu_context_shader_slot(ctx, shader->pool_index);
    uint32_t program = slot ? slot->gl_id : 0;

    if (program == 0 || (slot && slot->version != (int32_t)shader->version)) {
        program = tc_shader_compile_gpu(shader);
        if (program == 0) {
            return;
        }
    }

    if (ops && ops->shader_use) {
        ops->shader_use(program);
    }
}

void tc_shader_delete_gpu(tc_shader* shader) {
    if (!shader) return;

    tc_gpu_context* ctx = tc_gpu_get_context();
    if (!ctx) {
        return;
    }

    const tgfx_gpu_ops* ops = tgfx_gpu_get_ops();
    tc_gpu_slot* slot = tc_gpu_context_shader_slot(ctx, shader->pool_index);
    if (slot) {
        if (slot->gl_id != 0 && ops && ops->shader_delete) {
            ops->shader_delete(slot->gl_id);
        }
        slot->gl_id = 0;
        slot->version = -1;
    }
}

// ============================================================================
// Mesh GPU operations
// ============================================================================

uint32_t tc_mesh_upload_gpu(tc_mesh* mesh) {
    if (!mesh || !mesh->vertices) {
        return 0;
    }

    const tgfx_gpu_ops* ops = tgfx_gpu_get_ops();
    if (!ops || !ops->mesh_upload) {
        tc_log_error("tc_mesh_upload_gpu: GPU ops not set");
        return 0;
    }

    tc_gpu_context* ctx = tc_gpu_get_context();
    if (!ctx) {
        tc_log_error("tc_mesh_upload_gpu: no GPUContext set");
        return 0;
    }

    tc_gpu_share_group* group = ctx->share_group;
    tc_gpu_mesh_data_slot* shared = tc_gpu_share_group_mesh_data_slot(group, mesh->header.pool_index);
    tc_gpu_vao_slot* vao_slot = tc_gpu_context_vao_slot(ctx, mesh->header.pool_index);
    if (!shared || !vao_slot) {
        tc_log_error("tc_mesh_upload_gpu: failed to get slots");
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
            tc_log_error("tc_mesh_upload_gpu: mesh_create_vao failed for '%s'",
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
        tc_log_error("tc_mesh_upload_gpu: upload failed for '%s'",
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

void tc_mesh_draw_gpu(tc_mesh* mesh) {
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
        if (tc_mesh_upload_gpu(mesh) == 0) {
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

void tc_mesh_delete_gpu(tc_mesh* mesh) {
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

// ============================================================================
// Shader uniform operations
// ============================================================================

void tc_shader_set_int(tc_shader* shader, const char* name, int value) {
    if (!shader) return;
    uint32_t program = shader_current_program(shader);
    if (program == 0) return;
    const tgfx_gpu_ops* ops = tgfx_gpu_get_ops();
    if (ops && ops->shader_set_int) {
        ops->shader_set_int(program, name, value);
    }
}

void tc_shader_set_float(tc_shader* shader, const char* name, float value) {
    if (!shader) return;
    uint32_t program = shader_current_program(shader);
    if (program == 0) return;
    const tgfx_gpu_ops* ops = tgfx_gpu_get_ops();
    if (ops && ops->shader_set_float) {
        ops->shader_set_float(program, name, value);
    }
}

void tc_shader_set_vec2(tc_shader* shader, const char* name, float x, float y) {
    if (!shader) return;
    uint32_t program = shader_current_program(shader);
    if (program == 0) return;
    const tgfx_gpu_ops* ops = tgfx_gpu_get_ops();
    if (ops && ops->shader_set_vec2) {
        ops->shader_set_vec2(program, name, x, y);
    }
}

void tc_shader_set_vec3(tc_shader* shader, const char* name, float x, float y, float z) {
    if (!shader) return;
    uint32_t program = shader_current_program(shader);
    if (program == 0) return;
    const tgfx_gpu_ops* ops = tgfx_gpu_get_ops();
    if (ops && ops->shader_set_vec3) {
        ops->shader_set_vec3(program, name, x, y, z);
    }
}

void tc_shader_set_vec4(tc_shader* shader, const char* name, float x, float y, float z, float w) {
    if (!shader) return;
    uint32_t program = shader_current_program(shader);
    if (program == 0) return;
    const tgfx_gpu_ops* ops = tgfx_gpu_get_ops();
    if (ops && ops->shader_set_vec4) {
        ops->shader_set_vec4(program, name, x, y, z, w);
    }
}

void tc_shader_set_mat4(tc_shader* shader, const char* name, const float* data, bool transpose) {
    if (!shader) return;
    uint32_t program = shader_current_program(shader);
    if (program == 0) return;
    const tgfx_gpu_ops* ops = tgfx_gpu_get_ops();
    if (ops && ops->shader_set_mat4) {
        ops->shader_set_mat4(program, name, data, transpose);
    }
}

void tc_shader_set_mat4_array(tc_shader* shader, const char* name, const float* data, int count, bool transpose) {
    if (!shader) return;
    uint32_t program = shader_current_program(shader);
    if (program == 0) return;
    const tgfx_gpu_ops* ops = tgfx_gpu_get_ops();
    if (ops && ops->shader_set_mat4_array) {
        ops->shader_set_mat4_array(program, name, data, count, transpose);
    }
}

void tc_shader_set_block_binding(tc_shader* shader, const char* block_name, int binding_point) {
    if (!shader) return;
    uint32_t program = shader_current_program(shader);
    if (program == 0) return;
    const tgfx_gpu_ops* ops = tgfx_gpu_get_ops();
    if (ops && ops->shader_set_block_binding) {
        ops->shader_set_block_binding(program, block_name, binding_point);
    }
}
