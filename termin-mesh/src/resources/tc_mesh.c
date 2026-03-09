// tc_mesh.c - Mesh reference counting and UUID computation
#include "tgfx/resources/tc_mesh.h"
#include "tgfx/resources/tc_mesh_registry.h"
#include <tcbase/tc_log.h>
#include <string.h>
#include <stdio.h>

// ============================================================================
// GPU operations callback vtable
// ============================================================================

static const tc_mesh_gpu_ops* g_mesh_gpu_ops = NULL;

void tc_mesh_set_gpu_ops(const tc_mesh_gpu_ops* ops) {
    g_mesh_gpu_ops = ops;
}

const tc_mesh_gpu_ops* tc_mesh_get_gpu_ops(void) {
    return g_mesh_gpu_ops;
}

void tc_mesh_draw_gpu(tc_mesh* mesh) {
    if (mesh && g_mesh_gpu_ops && g_mesh_gpu_ops->draw) {
        g_mesh_gpu_ops->draw(mesh);
    }
}

uint32_t tc_mesh_upload_gpu(tc_mesh* mesh) {
    if (mesh && g_mesh_gpu_ops && g_mesh_gpu_ops->upload) {
        return g_mesh_gpu_ops->upload(mesh);
    }
    return 0;
}

void tc_mesh_delete_gpu(tc_mesh* mesh) {
    if (mesh && g_mesh_gpu_ops && g_mesh_gpu_ops->delete_gpu) {
        g_mesh_gpu_ops->delete_gpu(mesh);
    }
}

// ============================================================================
// Reference counting
// ============================================================================

void tc_mesh_add_ref(tc_mesh* mesh) {
    if (mesh) {
        mesh->header.ref_count++;
    }
}

bool tc_mesh_release(tc_mesh* mesh) {
    if (!mesh) {
        return false;
    }
    if (mesh->header.ref_count == 0) {
        tc_log(TC_LOG_WARN, "[tc_mesh_release] uuid=%s name=%s refcount already zero!",
               mesh->header.uuid, mesh->header.name ? mesh->header.name : "(null)");
        return false;
    }

    mesh->header.ref_count--;

    if (mesh->header.ref_count == 0) {
        tc_mesh_remove(mesh->header.uuid);
        return true;
    }
    return false;
}

// ============================================================================
// UUID computation (FNV-1a hash)
// ============================================================================

static uint64_t fnv1a_hash(const uint8_t* data, size_t len) {
    uint64_t hash = 14695981039346656037ULL;
    for (size_t i = 0; i < len; i++) {
        hash ^= data[i];
        hash *= 1099511628211ULL;
    }
    return hash;
}

void tc_mesh_compute_uuid(
    const void* vertices, size_t vertex_size,
    const uint32_t* indices, size_t index_count,
    char* uuid_out
) {
    // Hash vertices
    uint64_t h1 = fnv1a_hash((const uint8_t*)vertices, vertex_size);

    // Hash indices
    uint64_t h2 = fnv1a_hash((const uint8_t*)indices, index_count * sizeof(uint32_t));

    // Combine hashes
    uint64_t combined = h1 ^ (h2 * 1099511628211ULL);

    // Format as hex string (16 chars)
    snprintf(uuid_out, 40, "%016llx", (unsigned long long)combined);
}
