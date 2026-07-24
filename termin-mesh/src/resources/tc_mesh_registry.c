// tc_mesh_registry.c - Mesh registry with pool + hash table
#include "tgfx/resources/tc_mesh_registry.h"
#include <tcbase/tc_pool.h>
#include <tcbase/tc_resource_map.h>
#include <tcbase/tc_registry_utils.h>
#include <tcbase/tc_log.h>
#include <tcbase/tc_string.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// ============================================================================
// Global state
// ============================================================================

static tc_pool g_mesh_pool;
static tc_resource_map* g_uuid_to_index = NULL;
static uint64_t g_next_uuid = 1;
static bool g_initialized = false;

// Destroy-hook subscription table. See tc_mesh_registry.h.
static tc_mesh_destroy_hook_fn g_destroy_hooks[TC_MAX_MESH_DESTROY_HOOKS];
static void* g_destroy_hook_user[TC_MAX_MESH_DESTROY_HOOKS];
static int g_destroy_hook_count = 0;

// Free mesh internal data (vertices, indices, submeshes)
static void mesh_free_data(tc_mesh* mesh) {
    if (!mesh) return;
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

static bool tc_mesh_validate_submesh_range(
    const tc_mesh* mesh,
    const tc_submesh* submesh,
    size_t submesh_index
) {
    if (!mesh || !submesh) return false;
    if (submesh->index_count == 0) {
        tc_log(TC_LOG_ERROR,
               "tc_mesh_set_submeshes: submesh %zu has zero index_count for mesh '%s'",
               submesh_index,
               mesh->header.name ? mesh->header.name : mesh->header.uuid);
        return false;
    }
    if ((size_t)submesh->first_index > mesh->index_count ||
        (size_t)submesh->index_count > mesh->index_count - (size_t)submesh->first_index) {
        tc_log(TC_LOG_ERROR,
               "tc_mesh_set_submeshes: submesh %zu range [%u, %u) exceeds mesh '%s' index_count=%zu",
               submesh_index,
               submesh->first_index,
               submesh->first_index + submesh->index_count,
               mesh->header.name ? mesh->header.name : mesh->header.uuid,
               mesh->index_count);
        return false;
    }
    return true;
}

static void tc_mesh_make_default_submesh(tc_mesh* mesh, tc_submesh* out) {
    memset(out, 0, sizeof(*out));
    out->first_index = 0;
    out->index_count = mesh && mesh->index_count <= UINT32_MAX
        ? (uint32_t)mesh->index_count
        : 0;
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

    if (!tc_pool_init(&g_mesh_pool, sizeof(tc_mesh), 64)) {
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
    mesh->header.is_loaded = 1;  // Created meshes are considered loaded (data will be set)

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
    mesh->header.is_loaded = 0;  // NOT loaded yet

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
    if (!mesh) return false;
    return mesh->header.is_loaded != 0;
}

bool tc_mesh_ensure_loaded(tc_mesh_handle h) {
    tc_mesh* mesh = tc_mesh_get(h);
    if (!mesh) return false;

    bool success = tc_resource_header_ensure_loaded(&mesh->header);
    if (!success) {
        tc_log(TC_LOG_ERROR, "tc_mesh_ensure_loaded: resource loader failed for '%s'", mesh->header.uuid);
    }
    return success;
}

bool tc_mesh_ensure_loaded_ptr(tc_mesh* mesh) {
    if (!mesh) return false;
    bool success = tc_resource_header_ensure_loaded(&mesh->header);
    if (!success) {
        tc_log(TC_LOG_ERROR, "tc_mesh_ensure_loaded_ptr: resource loader failed for '%s'", mesh->header.uuid);
    }
    return success;
}

tc_mesh* tc_mesh_get(tc_mesh_handle h) {
    if (!g_initialized) return NULL;
    return (tc_mesh*)tc_pool_get_checked(&g_mesh_pool, h, "tc_mesh");
}

bool tc_mesh_is_valid(tc_mesh_handle h) {
    if (!g_initialized) return false;
    return tc_pool_is_valid(&g_mesh_pool, h);
}

bool tc_mesh_destroy(tc_mesh_handle h) {
    if (!g_initialized) return false;

    tc_mesh* mesh = tc_mesh_get(h);
    if (!mesh) return false;

    tc_log(TC_LOG_INFO, "[tc_mesh_destroy] DESTROYING mesh uuid=%s name=%s refcount=%d",
           mesh->header.uuid, mesh->header.name ? mesh->header.name : "(null)",
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

void tc_mesh_registry_add_destroy_hook(
    tc_mesh_destroy_hook_fn cb, void* user_data
) {
    if (!cb) return;
    if (g_destroy_hook_count >= TC_MAX_MESH_DESTROY_HOOKS) {
        tc_log(TC_LOG_ERROR,
               "tc_mesh_registry: destroy-hook table full (%d)",
               TC_MAX_MESH_DESTROY_HOOKS);
        return;
    }
    g_destroy_hooks[g_destroy_hook_count] = cb;
    g_destroy_hook_user[g_destroy_hook_count] = user_data;
    g_destroy_hook_count++;
}

void tc_mesh_registry_remove_destroy_hook(
    tc_mesh_destroy_hook_fn cb, void* user_data
) {
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
    if (!g_initialized || !uuid) return false;
    return tc_resource_map_contains(g_uuid_to_index, uuid);
}

size_t tc_mesh_count(void) {
    if (!g_initialized) return 0;
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
    if (m) return m->layout;
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
    if (tc_mesh_handle_is_invalid(h)) return false;
    return tc_mesh_destroy(h);
}

// ============================================================================
// Mesh data helpers
// ============================================================================

bool tc_mesh_set_vertices(
    tc_mesh* mesh,
    const void* data,
    size_t vertex_count,
    const tc_vertex_layout* layout
) {
    if (!mesh || !layout) return false;

    size_t data_size = vertex_count * layout->stride;

    void* new_vertices = NULL;
    if (data_size > 0) {
        new_vertices = malloc(data_size);
        if (!new_vertices) return false;
        if (data) {
            memcpy(new_vertices, data, data_size);
        } else {
            memset(new_vertices, 0, data_size);
        }
    }

    if (mesh->vertices) free(mesh->vertices);

    mesh->vertices = new_vertices;
    mesh->vertex_count = vertex_count;
    mesh->layout = *layout;
    mesh->header.version++;

    return true;
}

bool tc_mesh_set_indices(
    tc_mesh* mesh,
    const uint32_t* data,
    size_t index_count
) {
    if (!mesh) return false;

    size_t data_size = index_count * sizeof(uint32_t);

    uint32_t* new_indices = NULL;
    if (data_size > 0) {
        new_indices = (uint32_t*)malloc(data_size);
        if (!new_indices) return false;
        if (data) {
            memcpy(new_indices, data, data_size);
        } else {
            memset(new_indices, 0, data_size);
        }
    }

    if (mesh->indices) free(mesh->indices);

    mesh->indices = new_indices;
    mesh->index_count = index_count;
    tc_mesh_ensure_default_submesh(mesh);
    mesh->header.version++;

    return true;
}

bool tc_mesh_set_submeshes(
    tc_mesh* mesh,
    const tc_submesh* submeshes,
    size_t submesh_count
) {
    if (!mesh) return false;

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

    size_t data_size = submesh_count * sizeof(tc_submesh);
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

    if (mesh->submeshes) free(mesh->submeshes);
    mesh->submeshes = new_submeshes;
    mesh->submesh_count = submesh_count;
    mesh->header.version++;
    return true;
}

bool tc_mesh_ensure_default_submesh(tc_mesh* mesh) {
    if (!mesh) return false;
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

    if (mesh->submeshes) free(mesh->submeshes);
    mesh->submeshes = new_submeshes;
    mesh->submesh_count = 1;
    return true;
}

size_t tc_mesh_get_submesh_count(const tc_mesh* mesh) {
    if (!mesh) return 0;
    return mesh->submesh_count;
}

const tc_submesh* tc_mesh_get_submesh(const tc_mesh* mesh, size_t index) {
    if (!mesh || index >= mesh->submesh_count) return NULL;
    return &mesh->submeshes[index];
}

bool tc_mesh_set_data(
    tc_mesh* mesh,
    const void* vertices,
    size_t vertex_count,
    const tc_vertex_layout* layout,
    const uint32_t* indices,
    size_t index_count,
    const char* name
) {
    if (!mesh || !layout) return false;

    if (name) {
        mesh->header.name = tc_intern_string(name);
    }

    size_t vertex_size = vertex_count * layout->stride;
    void* new_vertices = NULL;
    if (vertex_size > 0) {
        new_vertices = malloc(vertex_size);
        if (!new_vertices) return false;
        if (vertices) {
            memcpy(new_vertices, vertices, vertex_size);
        } else {
            memset(new_vertices, 0, vertex_size);
        }
    }

    size_t index_size = index_count * sizeof(uint32_t);
    uint32_t* new_indices = NULL;
    if (index_size > 0) {
        new_indices = (uint32_t*)malloc(index_size);
        if (!new_indices) {
            free(new_vertices);
            return false;
        }
        if (indices) {
            memcpy(new_indices, indices, index_size);
        } else {
            memset(new_indices, 0, index_size);
        }
    }

    if (mesh->vertices) free(mesh->vertices);
    if (mesh->indices) free(mesh->indices);

    mesh->vertices = new_vertices;
    mesh->vertex_count = vertex_count;
    mesh->layout = *layout;
    mesh->indices = new_indices;
    mesh->index_count = index_count;
    if (!tc_mesh_ensure_default_submesh(mesh)) {
        return false;
    }
    mesh->header.version++;
    mesh->header.is_loaded = 1;  // Data is now loaded

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
    if (!g_initialized || !callback) return;
    mesh_iter_ctx ctx = { callback, user_data };
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
    info->memory_bytes = mesh->vertex_count * mesh->layout.stride +
                         mesh->index_count * sizeof(uint32_t) +
                         mesh->submesh_count * sizeof(tc_submesh);
    info->is_loaded = mesh->header.is_loaded;

    return true;
}

tc_mesh_info* tc_mesh_get_all_info(size_t* count) {
    if (!count) return NULL;
    *count = 0;

    if (!g_initialized) {
        tc_log(TC_LOG_INFO, "[tc_mesh_get_all_info] NOT INITIALIZED!");
        return NULL;
    }

    size_t mesh_count = tc_pool_count(&g_mesh_pool);
    tc_log(TC_LOG_INFO, "[tc_mesh_get_all_info] pool_count=%zu", mesh_count);
    if (mesh_count == 0) return NULL;

    tc_mesh_info* infos = (tc_mesh_info*)malloc(mesh_count * sizeof(tc_mesh_info));
    if (!infos) {
        tc_log(TC_LOG_ERROR, "tc_mesh_get_all_info: allocation failed");
        return NULL;
    }

    info_collector collector = { infos, 0 };
    tc_mesh_foreach(collect_mesh_info, &collector);

    *count = collector.count;
    return infos;
}
