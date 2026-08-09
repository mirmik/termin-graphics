// tc_mesh_registry.c - Mesh registry with pool + hash table
#include "tgfx/resources/tc_mesh_registry.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <tcbase/tc_log.h>
#include <tcbase/tc_pool.h>
#include <tcbase/tc_registry_utils.h>
#include <tcbase/tc_resource_map.h>
#include <tcbase/tc_string.h>

// ============================================================================
// Global state
// ============================================================================

static tc_pool g_mesh_pool;
static tc_pool_generation_epoch g_mesh_generation_epoch;
static tc_resource_map* g_uuid_to_index = NULL;
static uint64_t g_next_uuid = 1;
static bool g_initialized = false;

// Destroy-hook subscription table. See tc_mesh_registry.h.
static tc_mesh_destroy_hook_fn g_destroy_hooks[TC_MAX_MESH_DESTROY_HOOKS];
static void* g_destroy_hook_user[TC_MAX_MESH_DESTROY_HOOKS];
static int g_destroy_hook_count = 0;

static bool tc_mesh_checked_size_mul(size_t count, size_t element_size, size_t* out, const char* context) {
    if (!out)
        return false;
    if (element_size != 0 && count > SIZE_MAX / element_size) {
        tc_log(TC_LOG_ERROR, "%s: size overflow: count=%zu element_size=%zu", context, count, element_size);
        return false;
    }
    *out = count * element_size;
    return true;
}

// Free mesh internal data (vertices, indices, submeshes)
static void mesh_free_data(tc_mesh* mesh) {
    if (!mesh)
        return;
    if (mesh->vertices) {
        free(mesh->vertices);
        mesh->vertices = NULL;
    }
    if (mesh->indices) {
        free(mesh->indices);
        mesh->indices = NULL;
    }
    if (mesh->submeshes) {
        free(mesh->submeshes);
        mesh->submeshes = NULL;
    }
    mesh->submesh_count = 0;
}

static bool tc_mesh_validate_submesh_range_for_count(
    size_t index_count, const char* mesh_name, const tc_submesh* submesh, size_t submesh_index, const char* context) {
    if (!submesh)
        return false;
    if (submesh->index_count == 0) {
        tc_log(TC_LOG_ERROR,
               "%s: submesh %zu has zero index_count for mesh '%s'",
               context,
               submesh_index,
               mesh_name ? mesh_name : "");
        return false;
    }
    if ((size_t)submesh->first_index > index_count ||
        (size_t)submesh->index_count > index_count - (size_t)submesh->first_index) {
        const size_t range_end = (size_t)submesh->first_index + (size_t)submesh->index_count;
        tc_log(TC_LOG_ERROR,
               "%s: submesh %zu range [%u, %zu) exceeds mesh '%s' index_count=%zu",
               context,
               submesh_index,
               submesh->first_index,
               range_end,
               mesh_name ? mesh_name : "",
               index_count);
        return false;
    }
    return true;
}

static bool tc_mesh_validate_submesh_range(const tc_mesh* mesh, const tc_submesh* submesh, size_t submesh_index) {
    if (!mesh)
        return false;
    return tc_mesh_validate_submesh_range_for_count(mesh->index_count,
                                                    mesh->header.name ? mesh->header.name : mesh->header.uuid,
                                                    submesh,
                                                    submesh_index,
                                                    "tc_mesh_set_submeshes");
}

static void tc_mesh_make_default_submesh(tc_mesh* mesh, tc_submesh* out) {
    memset(out, 0, sizeof(*out));
    out->first_index = 0;
    out->index_count = mesh && mesh->index_count <= UINT32_MAX ? (uint32_t)mesh->index_count : 0;
    out->vertex_offset = 0;
    out->material_slot = 0;
    out->draw_mode = mesh ? mesh->draw_mode : TC_DRAW_TRIANGLES;
    if (mesh && mesh->header.name && mesh->header.name[0]) {
        snprintf(out->name, sizeof(out->name), "%s", mesh->header.name);
    }
}

// ============================================================================
// Lifecycle
// ============================================================================

void tc_mesh_init(void) {
    TC_REGISTRY_INIT_GUARD(g_initialized, "tc_mesh");

    if (!tc_pool_init_rebootstrap(&g_mesh_pool, sizeof(tc_mesh), 64, &g_mesh_generation_epoch)) {
        tc_log(TC_LOG_ERROR, "tc_mesh_init: failed to init pool");
        return;
    }

    // UUID map doesn't own resources (indices are packed as void*)
    g_uuid_to_index = tc_resource_map_new(NULL);
    if (!g_uuid_to_index) {
        tc_log(TC_LOG_ERROR, "tc_mesh_init: failed to create uuid map");
        tc_pool_free(&g_mesh_pool);
        return;
    }

    g_next_uuid = 1;
    g_initialized = true;
}

void tc_mesh_shutdown(void) {
    TC_REGISTRY_SHUTDOWN_GUARD(g_initialized, "tc_mesh");

    // Free mesh data for all occupied slots
    for (uint32_t i = 0; i < g_mesh_pool.capacity; i++) {
        if (g_mesh_pool.states[i] == TC_SLOT_OCCUPIED) {
            tc_mesh* mesh = (tc_mesh*)tc_pool_get_unchecked(&g_mesh_pool, i);
            mesh_free_data(mesh);
        }
    }

    tc_pool_free(&g_mesh_pool);
    tc_resource_map_free(g_uuid_to_index);
    g_uuid_to_index = NULL;
    g_next_uuid = 1;
    g_initialized = false;
}

// ============================================================================
// Handle-based API
// ============================================================================

tc_mesh_handle tc_mesh_create(const char* uuid) {
    if (!g_initialized) {
        tc_mesh_init();
    }

    char uuid_buf[40];
    const char* final_uuid;

    if (uuid && uuid[0] != '\0') {
        if (tc_mesh_contains(uuid)) {
            tc_log(TC_LOG_WARN, "tc_mesh_create: uuid '%s' already exists", uuid);
            return tc_mesh_handle_invalid();
        }
        final_uuid = uuid;
    } else {
        tc_generate_prefixed_uuid(uuid_buf, sizeof(uuid_buf), "mesh", &g_next_uuid);
        final_uuid = uuid_buf;
    }

    // Allocate slot in pool
    tc_handle h = tc_pool_alloc(&g_mesh_pool);
    if (tc_handle_is_invalid(h)) {
        tc_log(TC_LOG_ERROR, "tc_mesh_create: pool alloc failed");
        return tc_mesh_handle_invalid();
    }

    // Get mesh pointer and init
    tc_mesh* mesh = (tc_mesh*)tc_pool_get(&g_mesh_pool, h);
    memset(mesh, 0, sizeof(tc_mesh));
    tc_resource_header_set_uuid(&mesh->header, final_uuid, "tc_mesh_create");
    mesh->header.version = 1;
    mesh->header.ref_count = 0;
    mesh->header.pool_index = h.index;
    mesh->header.is_loaded = 1; // Created meshes are considered loaded (data will be set)

    // Add to UUID map
    if (!tc_resource_map_add(g_uuid_to_index, mesh->header.uuid, tc_pack_index(h.index))) {
        tc_log(TC_LOG_ERROR, "tc_mesh_create: failed to add to uuid map");
        tc_pool_free_slot(&g_mesh_pool, h);
        return tc_mesh_handle_invalid();
    }

    return h;
}

tc_mesh_handle tc_mesh_find(const char* uuid) {
    if (!g_initialized || !uuid) {
        return tc_mesh_handle_invalid();
    }

    void* ptr = tc_resource_map_get(g_uuid_to_index, uuid);
    if (!tc_has_index(ptr)) {
        return tc_mesh_handle_invalid();
    }

    uint32_t index = tc_unpack_index(ptr);
    if (index >= g_mesh_pool.capacity) {
        return tc_mesh_handle_invalid();
    }

    if (g_mesh_pool.states[index] != TC_SLOT_OCCUPIED) {
        return tc_mesh_handle_invalid();
    }

    tc_mesh_handle h;
    h.index = index;
    h.generation = g_mesh_pool.generations[index];
    return h;
}

tc_mesh_handle tc_mesh_find_by_name(const char* name) {
    if (!g_initialized || !name) {
        return tc_mesh_handle_invalid();
    }

    for (uint32_t i = 0; i < g_mesh_pool.capacity; i++) {
        if (g_mesh_pool.states[i] == TC_SLOT_OCCUPIED) {
            tc_mesh* mesh = (tc_mesh*)tc_pool_get_unchecked(&g_mesh_pool, i);
            if (mesh->header.name && strcmp(mesh->header.name, name) == 0) {
                tc_mesh_handle h;
                h.index = i;
                h.generation = g_mesh_pool.generations[i];
                return h;
            }
        }
    }

    return tc_mesh_handle_invalid();
}

tc_mesh_handle tc_mesh_get_or_create(const char* uuid) {
    if (!uuid || uuid[0] == '\0') {
        tc_log(TC_LOG_WARN, "tc_mesh_get_or_create: empty uuid");
        return tc_mesh_handle_invalid();
    }

    tc_mesh_handle h = tc_mesh_find(uuid);
    if (!tc_mesh_handle_is_invalid(h)) {
        return h;
    }

    return tc_mesh_create(uuid);
}

// ============================================================================
// Lazy loading API
// ============================================================================

tc_mesh_handle tc_mesh_declare(const char* uuid, const char* name) {
    if (!g_initialized) {
        tc_mesh_init();
    }

    // Check if already exists
    tc_mesh_handle existing = tc_mesh_find(uuid);
    if (!tc_mesh_handle_is_invalid(existing)) {
        return existing;
    }

    // Allocate slot in pool
    tc_handle h = tc_pool_alloc(&g_mesh_pool);
    if (tc_handle_is_invalid(h)) {
        tc_log(TC_LOG_ERROR, "tc_mesh_declare: pool alloc failed");
        return tc_mesh_handle_invalid();
    }

    // Get mesh pointer and init (declared but not loaded)
    tc_mesh* mesh = (tc_mesh*)tc_pool_get(&g_mesh_pool, h);
    memset(mesh, 0, sizeof(tc_mesh));
    tc_resource_header_set_uuid(&mesh->header, uuid, "tc_mesh_declare");
    mesh->header.version = 0;
    mesh->header.ref_count = 0;
    mesh->header.pool_index = h.index;
    mesh->header.is_loaded = 0; // NOT loaded yet

    if (name && name[0] != '\0') {
        mesh->header.name = tc_intern_string(name);
    }

    // Add to UUID map
    if (!tc_resource_map_add(g_uuid_to_index, mesh->header.uuid, tc_pack_index(h.index))) {
        tc_log(TC_LOG_ERROR, "tc_mesh_declare: failed to add to uuid map");
        tc_pool_free_slot(&g_mesh_pool, h);
        return tc_mesh_handle_invalid();
    }

    return h;
}

bool tc_mesh_is_loaded(tc_mesh_handle h) {
    tc_mesh* mesh = tc_mesh_get(h);
    if (!mesh)
        return false;
    return mesh->header.is_loaded != 0;
}

bool tc_mesh_ensure_loaded(tc_mesh_handle h) {
    tc_mesh* mesh = tc_mesh_get(h);
    if (!mesh)
        return false;

    bool success = tc_resource_header_ensure_loaded(&mesh->header);
    if (!success) {
        tc_log(TC_LOG_ERROR, "tc_mesh_ensure_loaded: resource loader failed for '%s'", mesh->header.uuid);
    }
    return success;
}

bool tc_mesh_ensure_loaded_ptr(tc_mesh* mesh) {
    if (!mesh)
        return false;
    bool success = tc_resource_header_ensure_loaded(&mesh->header);
    if (!success) {
        tc_log(TC_LOG_ERROR, "tc_mesh_ensure_loaded_ptr: resource loader failed for '%s'", mesh->header.uuid);
    }
    return success;
}

tc_mesh* tc_mesh_get(tc_mesh_handle h) {
    if (!g_initialized)
        return NULL;
    return (tc_mesh*)tc_pool_get_checked(&g_mesh_pool, h, "tc_mesh");
}

bool tc_mesh_is_valid(tc_mesh_handle h) {
    if (!g_initialized)
        return false;
    return tc_pool_is_valid(&g_mesh_pool, h);
}

bool tc_mesh_destroy(tc_mesh_handle h) {
    if (!g_initialized)
        return false;

    tc_mesh* mesh = tc_mesh_get(h);
    if (!mesh)
        return false;

    tc_log(TC_LOG_INFO,
           "[tc_mesh_destroy] DESTROYING mesh uuid=%s name=%s refcount=%d",
           mesh->header.uuid,
           mesh->header.name ? mesh->header.name : "(null)",
           mesh->header.ref_count);

    // Fire destroy-hooks before releasing CPU data / pool slot so GPU-side
    // caches keyed by pool_index can drop their entries first.
    const uint32_t pool_index = mesh->header.pool_index;
    for (int i = 0; i < g_destroy_hook_count; i++) {
        g_destroy_hooks[i](pool_index, g_destroy_hook_user[i]);
    }

    // Remove from UUID map
    tc_resource_map_remove(g_uuid_to_index, mesh->header.uuid);

    // Free mesh data
    mesh_free_data(mesh);

    // Free slot in pool (bumps generation)
    return tc_pool_free_slot(&g_mesh_pool, h);
}

void tc_mesh_registry_add_destroy_hook(tc_mesh_destroy_hook_fn cb, void* user_data) {
    if (!cb)
        return;
    if (g_destroy_hook_count >= TC_MAX_MESH_DESTROY_HOOKS) {
        tc_log(TC_LOG_ERROR, "tc_mesh_registry: destroy-hook table full (%d)", TC_MAX_MESH_DESTROY_HOOKS);
        return;
    }
    g_destroy_hooks[g_destroy_hook_count] = cb;
    g_destroy_hook_user[g_destroy_hook_count] = user_data;
    g_destroy_hook_count++;
}

void tc_mesh_registry_remove_destroy_hook(tc_mesh_destroy_hook_fn cb, void* user_data) {
    for (int i = 0; i < g_destroy_hook_count; i++) {
        if (g_destroy_hooks[i] == cb && g_destroy_hook_user[i] == user_data) {
            g_destroy_hooks[i] = g_destroy_hooks[g_destroy_hook_count - 1];
            g_destroy_hook_user[i] = g_destroy_hook_user[g_destroy_hook_count - 1];
            g_destroy_hook_count--;
            return;
        }
    }
}

bool tc_mesh_contains(const char* uuid) {
    if (!g_initialized || !uuid)
        return false;
    return tc_resource_map_contains(g_uuid_to_index, uuid);
}

size_t tc_mesh_count(void) {
    if (!g_initialized)
        return 0;
    return tc_pool_count(&g_mesh_pool);
}

// ============================================================================
// Mesh data export
// ============================================================================

const char* tc_mesh_get_uuid_str(tc_mesh_handle h) {
    tc_mesh* m = tc_mesh_get(h);
    return m ? m->header.uuid : NULL;
}

const char* tc_mesh_get_name_str(tc_mesh_handle h) {
    tc_mesh* m = tc_mesh_get(h);
    return m ? m->header.name : NULL;
}

const void* tc_mesh_get_vertices(tc_mesh_handle h) {
    tc_mesh* m = tc_mesh_get(h);
    return m ? m->vertices : NULL;
}

size_t tc_mesh_get_vertex_count(tc_mesh_handle h) {
    tc_mesh* m = tc_mesh_get(h);
    return m ? m->vertex_count : 0;
}

const uint32_t* tc_mesh_get_indices(tc_mesh_handle h) {
    tc_mesh* m = tc_mesh_get(h);
    return m ? (const uint32_t*)m->indices : NULL;
}

size_t tc_mesh_get_index_count(tc_mesh_handle h) {
    tc_mesh* m = tc_mesh_get(h);
    return m ? m->index_count : 0;
}

tc_vertex_layout tc_mesh_get_layout(tc_mesh_handle h) {
    tc_mesh* m = tc_mesh_get(h);
    if (m)
        return m->layout;
    tc_vertex_layout empty;
    memset(&empty, 0, sizeof(empty));
    return empty;
}

uint8_t tc_mesh_get_draw_mode(tc_mesh_handle h) {
    tc_mesh* m = tc_mesh_get(h);
    return m ? m->draw_mode : 0;
}

// ============================================================================
// Legacy pointer-based API
// ============================================================================

tc_mesh* tc_mesh_add(const char* uuid) {
    tc_mesh_handle h = tc_mesh_create(uuid);
    return tc_mesh_get(h);
}

bool tc_mesh_remove(const char* uuid) {
    tc_mesh_handle h = tc_mesh_find(uuid);
    if (tc_mesh_handle_is_invalid(h))
        return false;
    return tc_mesh_destroy(h);
}

// ============================================================================
// Mesh data helpers
// ============================================================================

void tc_mesh_data_builder_discard(tc_mesh_data_builder* builder) {
    if (!builder)
        return;
    free(builder->vertices);
    free(builder->indices);
    free(builder->submeshes);
    memset(builder, 0, sizeof(*builder));
}

bool tc_mesh_data_builder_allocate(tc_mesh_data_builder* builder,
                                   size_t vertex_count,
                                   const tc_vertex_layout* layout,
                                   size_t index_count,
                                   size_t submesh_count) {
    if (!builder || !layout) {
        tc_log(TC_LOG_ERROR, "tc_mesh_data_builder_allocate: builder and layout are required");
        return false;
    }

    memset(builder, 0, sizeof(*builder));

    if (vertex_count > 0 && layout->stride == 0) {
        tc_log(TC_LOG_ERROR, "tc_mesh_data_builder_allocate: non-empty vertex payload has zero stride");
        return false;
    }
    if (index_count > UINT32_MAX) {
        tc_log(
            TC_LOG_ERROR, "tc_mesh_data_builder_allocate: index_count=%zu exceeds uint32 submesh range", index_count);
        return false;
    }

    size_t vertex_size = 0;
    size_t index_size = 0;
    size_t effective_submesh_count = submesh_count;
    if (effective_submesh_count == 0 && index_count > 0)
        effective_submesh_count = 1;
    size_t submesh_size = 0;
    if (!tc_mesh_checked_size_mul(
            vertex_count, layout->stride, &vertex_size, "tc_mesh_data_builder_allocate(vertices)") ||
        !tc_mesh_checked_size_mul(
            index_count, sizeof(uint32_t), &index_size, "tc_mesh_data_builder_allocate(indices)") ||
        !tc_mesh_checked_size_mul(
            effective_submesh_count, sizeof(tc_submesh), &submesh_size, "tc_mesh_data_builder_allocate(submeshes)")) {
        return false;
    }

    void* vertices = vertex_size > 0 ? calloc(1, vertex_size) : NULL;
    if (vertex_size > 0 && !vertices) {
        tc_log(TC_LOG_ERROR, "tc_mesh_data_builder_allocate: vertex allocation failed (%zu bytes)", vertex_size);
        return false;
    }
    uint32_t* indices = index_size > 0 ? (uint32_t*)calloc(1, index_size) : NULL;
    if (index_size > 0 && !indices) {
        tc_log(TC_LOG_ERROR, "tc_mesh_data_builder_allocate: index allocation failed (%zu bytes)", index_size);
        free(vertices);
        return false;
    }
    tc_submesh* submeshes = submesh_size > 0 ? (tc_submesh*)calloc(1, submesh_size) : NULL;
    if (submesh_size > 0 && !submeshes) {
        tc_log(TC_LOG_ERROR, "tc_mesh_data_builder_allocate: submesh allocation failed (%zu bytes)", submesh_size);
        free(indices);
        free(vertices);
        return false;
    }

    builder->vertices = vertices;
    builder->vertex_count = vertex_count;
    builder->indices = indices;
    builder->index_count = index_count;
    builder->submeshes = submeshes;
    builder->submesh_count = effective_submesh_count;
    builder->layout = *layout;
    builder->vertex_capacity_bytes = vertex_size;
    builder->index_capacity = index_count;
    builder->submesh_capacity = effective_submesh_count;

    if (submesh_count == 0 && index_count > 0) {
        builder->submeshes[0].first_index = 0;
        builder->submeshes[0].index_count = (uint32_t)index_count;
        builder->submeshes[0].draw_mode = TC_DRAW_TRIANGLES;
    }
    return true;
}

bool tc_mesh_data_builder_commit(tc_mesh* mesh, tc_mesh_data_builder* builder, const char* name) {
    if (!mesh || !builder) {
        tc_log(TC_LOG_ERROR, "tc_mesh_data_builder_commit: mesh and builder are required");
        return false;
    }
    if ((builder->vertex_count > 0 && (!builder->vertices || builder->layout.stride == 0)) ||
        (builder->index_count > 0 && !builder->indices) || (builder->submesh_count > 0 && !builder->submeshes)) {
        tc_log(TC_LOG_ERROR, "tc_mesh_data_builder_commit: builder storage is incomplete");
        return false;
    }
    size_t vertex_size = 0;
    if (!tc_mesh_checked_size_mul(
            builder->vertex_count, builder->layout.stride, &vertex_size, "tc_mesh_data_builder_commit(vertices)")) {
        return false;
    }
    if (vertex_size > builder->vertex_capacity_bytes || builder->index_count > builder->index_capacity ||
        builder->submesh_count > builder->submesh_capacity) {
        tc_log(TC_LOG_ERROR,
               "tc_mesh_data_builder_commit: payload exceeds allocation "
               "(vertices=%zu/%zu indices=%zu/%zu submeshes=%zu/%zu)",
               vertex_size,
               builder->vertex_capacity_bytes,
               builder->index_count,
               builder->index_capacity,
               builder->submesh_count,
               builder->submesh_capacity);
        return false;
    }
    if (builder->index_count > UINT32_MAX) {
        tc_log(TC_LOG_ERROR,
               "tc_mesh_data_builder_commit: index_count=%zu exceeds uint32 submesh range",
               builder->index_count);
        return false;
    }
    if (builder->index_count > 0 && builder->submesh_count == 0) {
        tc_log(TC_LOG_ERROR, "tc_mesh_data_builder_commit: indexed mesh has no submeshes");
        return false;
    }

    const char* effective_name = name ? name : (mesh->header.name ? mesh->header.name : mesh->header.uuid);
    for (size_t i = 0; i < builder->submesh_count; ++i) {
        if (!tc_mesh_validate_submesh_range_for_count(
                builder->index_count, effective_name, &builder->submeshes[i], i, "tc_mesh_data_builder_commit")) {
            return false;
        }
    }

    for (size_t i = 0; i < builder->submesh_count; ++i) {
        builder->submeshes[i].name[TC_SUBMESH_NAME_MAX - 1] = '\0';
        if (builder->submeshes[i].draw_mode != TC_DRAW_LINES)
            builder->submeshes[i].draw_mode = TC_DRAW_TRIANGLES;
        if (builder->submeshes[i].name[0] == '\0' && effective_name)
            snprintf(builder->submeshes[i].name, sizeof(builder->submeshes[i].name), "%s", effective_name);
    }

    void* old_vertices = mesh->vertices;
    uint32_t* old_indices = mesh->indices;
    tc_submesh* old_submeshes = mesh->submeshes;

    mesh->vertices = builder->vertices;
    mesh->vertex_count = builder->vertex_count;
    mesh->indices = builder->indices;
    mesh->index_count = builder->index_count;
    mesh->submeshes = builder->submeshes;
    mesh->submesh_count = builder->submesh_count;
    mesh->layout = builder->layout;
    if (name)
        mesh->header.name = tc_intern_string(name);
    mesh->header.version++;
    mesh->header.is_loaded = 1;

    memset(builder, 0, sizeof(*builder));
    free(old_vertices);
    free(old_indices);
    free(old_submeshes);
    return true;
}

bool tc_mesh_set_vertices(tc_mesh* mesh, const void* data, size_t vertex_count, const tc_vertex_layout* layout) {
    if (!mesh || !layout) {
        tc_log(TC_LOG_ERROR, "tc_mesh_set_vertices: mesh and layout are required");
        return false;
    }

    size_t data_size = 0;
    if (!tc_mesh_checked_size_mul(vertex_count, layout->stride, &data_size, "tc_mesh_set_vertices"))
        return false;

    void* new_vertices = NULL;
    if (data_size > 0) {
        new_vertices = malloc(data_size);
        if (!new_vertices)
            return false;
        if (data) {
            memcpy(new_vertices, data, data_size);
        } else {
            memset(new_vertices, 0, data_size);
        }
    }

    if (mesh->vertices)
        free(mesh->vertices);

    mesh->vertices = new_vertices;
    mesh->vertex_count = vertex_count;
    mesh->layout = *layout;
    mesh->header.version++;

    return true;
}

bool tc_mesh_set_indices(tc_mesh* mesh, const uint32_t* data, size_t index_count) {
    if (!mesh) {
        tc_log(TC_LOG_ERROR, "tc_mesh_set_indices: mesh is required");
        return false;
    }
    if (index_count > UINT32_MAX) {
        tc_log(TC_LOG_ERROR, "tc_mesh_set_indices: index_count=%zu exceeds uint32 submesh range", index_count);
        return false;
    }

    size_t data_size = 0;
    if (!tc_mesh_checked_size_mul(index_count, sizeof(uint32_t), &data_size, "tc_mesh_set_indices"))
        return false;

    uint32_t* new_indices = NULL;
    if (data_size > 0) {
        new_indices = (uint32_t*)malloc(data_size);
        if (!new_indices)
            return false;
        if (data) {
            memcpy(new_indices, data, data_size);
        } else {
            memset(new_indices, 0, data_size);
        }
    }

    if (mesh->indices)
        free(mesh->indices);

    mesh->indices = new_indices;
    mesh->index_count = index_count;
    tc_mesh_ensure_default_submesh(mesh);
    mesh->header.version++;

    return true;
}

bool tc_mesh_set_submeshes(tc_mesh* mesh, const tc_submesh* submeshes, size_t submesh_count) {
    if (!mesh)
        return false;

    if (submesh_count == 0) {
        return tc_mesh_ensure_default_submesh(mesh);
    }
    if (!submeshes) {
        tc_log(TC_LOG_ERROR, "tc_mesh_set_submeshes: submeshes is NULL with count=%zu", submesh_count);
        return false;
    }
    if (mesh->index_count > UINT32_MAX) {
        tc_log(TC_LOG_ERROR,
               "tc_mesh_set_submeshes: mesh '%s' index_count=%zu exceeds uint32 submesh range",
               mesh->header.name ? mesh->header.name : mesh->header.uuid,
               mesh->index_count);
        return false;
    }

    for (size_t i = 0; i < submesh_count; ++i) {
        if (!tc_mesh_validate_submesh_range(mesh, &submeshes[i], i)) {
            return false;
        }
    }

    size_t data_size = 0;
    if (!tc_mesh_checked_size_mul(submesh_count, sizeof(tc_submesh), &data_size, "tc_mesh_set_submeshes"))
        return false;
    tc_submesh* new_submeshes = (tc_submesh*)malloc(data_size);
    if (!new_submeshes) {
        tc_log(TC_LOG_ERROR, "tc_mesh_set_submeshes: allocation failed");
        return false;
    }
    memcpy(new_submeshes, submeshes, data_size);
    for (size_t i = 0; i < submesh_count; ++i) {
        new_submeshes[i].name[TC_SUBMESH_NAME_MAX - 1] = '\0';
        if (new_submeshes[i].draw_mode != TC_DRAW_LINES) {
            new_submeshes[i].draw_mode = TC_DRAW_TRIANGLES;
        }
    }

    if (mesh->submeshes)
        free(mesh->submeshes);
    mesh->submeshes = new_submeshes;
    mesh->submesh_count = submesh_count;
    mesh->header.version++;
    return true;
}

bool tc_mesh_ensure_default_submesh(tc_mesh* mesh) {
    if (!mesh)
        return false;
    if (mesh->index_count == 0) {
        if (mesh->submeshes) {
            free(mesh->submeshes);
            mesh->submeshes = NULL;
        }
        mesh->submesh_count = 0;
        return true;
    }
    if (mesh->index_count > UINT32_MAX) {
        tc_log(TC_LOG_ERROR,
               "tc_mesh_ensure_default_submesh: mesh '%s' index_count=%zu exceeds uint32 submesh range",
               mesh->header.name ? mesh->header.name : mesh->header.uuid,
               mesh->index_count);
        return false;
    }

    tc_submesh submesh;
    tc_mesh_make_default_submesh(mesh, &submesh);
    tc_submesh* new_submeshes = (tc_submesh*)malloc(sizeof(tc_submesh));
    if (!new_submeshes) {
        tc_log(TC_LOG_ERROR, "tc_mesh_ensure_default_submesh: allocation failed");
        return false;
    }
    *new_submeshes = submesh;

    if (mesh->submeshes)
        free(mesh->submeshes);
    mesh->submeshes = new_submeshes;
    mesh->submesh_count = 1;
    return true;
}

size_t tc_mesh_get_submesh_count(const tc_mesh* mesh) {
    if (!mesh)
        return 0;
    return mesh->submesh_count;
}

const tc_submesh* tc_mesh_get_submesh(const tc_mesh* mesh, size_t index) {
    if (!mesh || index >= mesh->submesh_count)
        return NULL;
    return &mesh->submeshes[index];
}

bool tc_mesh_set_data(tc_mesh* mesh,
                      const void* vertices,
                      size_t vertex_count,
                      const tc_vertex_layout* layout,
                      const uint32_t* indices,
                      size_t index_count,
                      const char* name) {
    if (!mesh || !layout) {
        tc_log(TC_LOG_ERROR, "tc_mesh_set_data: mesh and layout are required");
        return false;
    }

    tc_mesh_data_builder builder;
    if (!tc_mesh_data_builder_allocate(&builder, vertex_count, layout, index_count, 0))
        return false;
    if (builder.submesh_count > 0)
        builder.submeshes[0].draw_mode = mesh->draw_mode;
    size_t vertex_size = 0;
    size_t index_size = 0;
    if (!tc_mesh_checked_size_mul(vertex_count, layout->stride, &vertex_size, "tc_mesh_set_data(vertices)") ||
        !tc_mesh_checked_size_mul(index_count, sizeof(uint32_t), &index_size, "tc_mesh_set_data(indices)")) {
        tc_mesh_data_builder_discard(&builder);
        return false;
    }
    if (vertices && vertex_size > 0)
        memcpy(builder.vertices, vertices, vertex_size);
    if (indices && index_size > 0)
        memcpy(builder.indices, indices, index_size);
    if (!tc_mesh_data_builder_commit(mesh, &builder, name)) {
        tc_mesh_data_builder_discard(&builder);
        return false;
    }
    return true;
}

// ============================================================================
// Iteration
// ============================================================================

typedef struct {
    tc_mesh_iter_fn callback;
    void* user_data;
} mesh_iter_ctx;

static bool mesh_iter_adapter(uint32_t index, void* item, void* ctx_ptr) {
    mesh_iter_ctx* ctx = (mesh_iter_ctx*)ctx_ptr;
    tc_mesh* mesh = (tc_mesh*)item;

    tc_mesh_handle h;
    h.index = index;
    h.generation = g_mesh_pool.generations[index];

    return ctx->callback(h, mesh, ctx->user_data);
}

void tc_mesh_foreach(tc_mesh_iter_fn callback, void* user_data) {
    if (!g_initialized || !callback)
        return;
    mesh_iter_ctx ctx = {callback, user_data};
    tc_pool_foreach(&g_mesh_pool, mesh_iter_adapter, &ctx);
}

// ============================================================================
// Info collection
// ============================================================================

typedef struct {
    tc_mesh_info* infos;
    size_t count;
} info_collector;

static bool collect_mesh_info(tc_mesh_handle h, tc_mesh* mesh, void* user_data) {
    info_collector* collector = (info_collector*)user_data;

    tc_mesh_info* info = &collector->infos[collector->count++];
    info->handle = h;
    strncpy(info->uuid, mesh->header.uuid, sizeof(info->uuid) - 1);
    info->uuid[sizeof(info->uuid) - 1] = '\0';
    info->name = mesh->header.name;
    info->ref_count = mesh->header.ref_count;
    info->version = mesh->header.version;
    info->vertex_count = mesh->vertex_count;
    info->index_count = mesh->index_count;
    info->stride = mesh->layout.stride;
    info->memory_bytes = mesh->vertex_count * mesh->layout.stride + mesh->index_count * sizeof(uint32_t) +
                         mesh->submesh_count * sizeof(tc_submesh);
    info->is_loaded = mesh->header.is_loaded;

    return true;
}

tc_mesh_info* tc_mesh_get_all_info(size_t* count) {
    if (!count)
        return NULL;
    *count = 0;

    if (!g_initialized) {
        tc_log(TC_LOG_INFO, "[tc_mesh_get_all_info] NOT INITIALIZED!");
        return NULL;
    }

    size_t mesh_count = tc_pool_count(&g_mesh_pool);
    tc_log(TC_LOG_INFO, "[tc_mesh_get_all_info] pool_count=%zu", mesh_count);
    if (mesh_count == 0)
        return NULL;

    tc_mesh_info* infos = (tc_mesh_info*)malloc(mesh_count * sizeof(tc_mesh_info));
    if (!infos) {
        tc_log(TC_LOG_ERROR, "tc_mesh_get_all_info: allocation failed");
        return NULL;
    }

    info_collector collector = {infos, 0};
    tc_mesh_foreach(collect_mesh_info, &collector);

    *count = collector.count;
    return infos;
}
