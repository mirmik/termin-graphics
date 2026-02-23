// tc_shader_registry.c - Shader registry with pool + hash table and variant support
#include "tgfx/resources/tc_shader_registry.h"
#include "tgfx/tc_pool.h"
#include "tgfx/tc_resource_map.h"
#include "tgfx/tc_registry_utils.h"
#include <tcbase/tc_log.h>
#include "tgfx/tgfx_intern_string.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// ============================================================================
// Global state
// ============================================================================

static tc_pool g_shader_pool;
static tc_resource_map* g_uuid_to_index = NULL;   // UUID -> uint32_t index
static tc_resource_map* g_hash_to_index = NULL;   // source_hash -> uint32_t index
static uint64_t g_next_uuid = 1;
static bool g_initialized = false;

// Duplicate a string (NULL-safe)
static char* dup_string(const char* s) {
    if (!s || s[0] == '\0') return NULL;
    size_t len = strlen(s) + 1;
    char* copy = (char*)malloc(len);
    if (copy) memcpy(copy, s, len);
    return copy;
}

// Free shader internal data (sources)
static void shader_free_data(tc_shader* shader) {
    if (!shader) return;
    if (shader->vertex_source) {
        free(shader->vertex_source);
        shader->vertex_source = NULL;
    }
    if (shader->fragment_source) {
        free(shader->fragment_source);
        shader->fragment_source = NULL;
    }
    if (shader->geometry_source) {
        free(shader->geometry_source);
        shader->geometry_source = NULL;
    }
}

// ============================================================================
// Hash computation (FNV-1a)
// ============================================================================

static uint64_t fnv1a_string(const char* str, uint64_t hash) {
    if (!str) return hash;
    while (*str) {
        hash ^= (uint8_t)*str++;
        hash *= 0x100000001b3ULL;
    }
    return hash;
}

void tc_shader_compute_hash(
    const char* vertex_source,
    const char* fragment_source,
    const char* geometry_source,
    char* hash_out
) {
    uint64_t hash = 0xcbf29ce484222325ULL;  // FNV offset basis

    hash = fnv1a_string(vertex_source, hash);
    hash = fnv1a_string("::", hash);  // separator
    hash = fnv1a_string(fragment_source, hash);
    hash = fnv1a_string("::", hash);
    hash = fnv1a_string(geometry_source, hash);

    snprintf(hash_out, TC_SHADER_HASH_LEN, "%016llx", (unsigned long long)hash);
}

void tc_shader_update_hash(tc_shader* shader) {
    if (!shader) return;
    tc_shader_compute_hash(
        shader->vertex_source,
        shader->fragment_source,
        shader->geometry_source,
        shader->source_hash
    );
}

// ============================================================================
// Lifecycle
// ============================================================================

void tc_shader_init(void) {
    TC_REGISTRY_INIT_GUARD(g_initialized, "tc_shader");

    if (!tc_pool_init(&g_shader_pool, sizeof(tc_shader), 64)) {
        tc_log(TC_LOG_ERROR, "tc_shader_init: failed to init pool");
        return;
    }

    g_uuid_to_index = tc_resource_map_new(NULL);
    if (!g_uuid_to_index) {
        tc_log(TC_LOG_ERROR, "tc_shader_init: failed to create uuid map");
        tc_pool_free(&g_shader_pool);
        return;
    }

    g_hash_to_index = tc_resource_map_new(NULL);
    if (!g_hash_to_index) {
        tc_log(TC_LOG_ERROR, "tc_shader_init: failed to create hash map");
        tc_resource_map_free(g_uuid_to_index);
        g_uuid_to_index = NULL;
        tc_pool_free(&g_shader_pool);
        return;
    }

    g_next_uuid = 1;
    g_initialized = true;
}

void tc_shader_shutdown(void) {
    TC_REGISTRY_SHUTDOWN_GUARD(g_initialized, "tc_shader");

    // Free shader data for all occupied slots
    for (uint32_t i = 0; i < g_shader_pool.capacity; i++) {
        if (g_shader_pool.states[i] == TC_SLOT_OCCUPIED) {
            tc_shader* shader = (tc_shader*)tc_pool_get_unchecked(&g_shader_pool, i);
            shader_free_data(shader);
        }
    }

    tc_pool_free(&g_shader_pool);
    tc_resource_map_free(g_uuid_to_index);
    tc_resource_map_free(g_hash_to_index);
    g_uuid_to_index = NULL;
    g_hash_to_index = NULL;
    g_next_uuid = 1;
    g_initialized = false;
}

// ============================================================================
// Handle-based API
// ============================================================================

tc_shader_handle tc_shader_create(const char* uuid) {
    if (!g_initialized) {
        tc_shader_init();
    }

    char uuid_buf[40];
    const char* final_uuid;

    if (uuid && uuid[0] != '\0') {
        if (tc_shader_contains(uuid)) {
            tc_log(TC_LOG_WARN, "tc_shader_create: uuid '%s' already exists", uuid);
            return tc_shader_handle_invalid();
        }
        final_uuid = uuid;
    } else {
        tc_generate_prefixed_uuid(uuid_buf, sizeof(uuid_buf), "shader", &g_next_uuid);
        final_uuid = uuid_buf;
    }

    // Allocate slot in pool
    tc_handle h = tc_pool_alloc(&g_shader_pool);
    if (tc_handle_is_invalid(h)) {
        tc_log(TC_LOG_ERROR, "tc_shader_create: pool alloc failed");
        return tc_shader_handle_invalid();
    }

    // Get shader pointer and init
    tc_shader* shader = (tc_shader*)tc_pool_get(&g_shader_pool, h);
    memset(shader, 0, sizeof(tc_shader));
    strncpy(shader->uuid, final_uuid, sizeof(shader->uuid) - 1);
    shader->uuid[sizeof(shader->uuid) - 1] = '\0';
    shader->version = 1;
    shader->ref_count = 0;
    shader->pool_index = h.index;
    shader->original_handle = tc_shader_handle_invalid();

    // Add to UUID map
    if (!tc_resource_map_add(g_uuid_to_index, shader->uuid, tc_pack_index(h.index))) {
        tc_log(TC_LOG_ERROR, "tc_shader_create: failed to add to uuid map");
        tc_pool_free_slot(&g_shader_pool, h);
        return tc_shader_handle_invalid();
    }

    return h;
}

tc_shader_handle tc_shader_find(const char* uuid) {
    if (!g_initialized || !uuid) {
        return tc_shader_handle_invalid();
    }

    void* ptr = tc_resource_map_get(g_uuid_to_index, uuid);
    if (!tc_has_index(ptr)) {
        return tc_shader_handle_invalid();
    }

    uint32_t index = tc_unpack_index(ptr);
    if (index >= g_shader_pool.capacity) {
        return tc_shader_handle_invalid();
    }

    if (g_shader_pool.states[index] != TC_SLOT_OCCUPIED) {
        return tc_shader_handle_invalid();
    }

    tc_shader_handle h;
    h.index = index;
    h.generation = g_shader_pool.generations[index];
    return h;
}

tc_shader_handle tc_shader_find_by_hash(const char* source_hash) {
    if (!g_initialized || !source_hash) {
        return tc_shader_handle_invalid();
    }

    void* ptr = tc_resource_map_get(g_hash_to_index, source_hash);
    if (!tc_has_index(ptr)) {
        return tc_shader_handle_invalid();
    }

    uint32_t index = tc_unpack_index(ptr);
    if (index >= g_shader_pool.capacity) {
        return tc_shader_handle_invalid();
    }

    if (g_shader_pool.states[index] != TC_SLOT_OCCUPIED) {
        return tc_shader_handle_invalid();
    }

    tc_shader_handle h;
    h.index = index;
    h.generation = g_shader_pool.generations[index];
    return h;
}

tc_shader_handle tc_shader_find_by_name(const char* name) {
    if (!g_initialized || !name) {
        return tc_shader_handle_invalid();
    }

    for (uint32_t i = 0; i < g_shader_pool.capacity; i++) {
        if (g_shader_pool.states[i] == TC_SLOT_OCCUPIED) {
            tc_shader* shader = (tc_shader*)tc_pool_get_unchecked(&g_shader_pool, i);
            if (shader->name && strcmp(shader->name, name) == 0) {
                tc_shader_handle h;
                h.index = i;
                h.generation = g_shader_pool.generations[i];
                return h;
            }
        }
    }

    return tc_shader_handle_invalid();
}

tc_shader_handle tc_shader_get_or_create(const char* uuid) {
    if (!uuid || uuid[0] == '\0') {
        tc_log(TC_LOG_WARN, "tc_shader_get_or_create: empty uuid");
        return tc_shader_handle_invalid();
    }

    tc_shader_handle h = tc_shader_find(uuid);
    if (!tc_shader_handle_is_invalid(h)) {
        return h;
    }

    return tc_shader_create(uuid);
}

tc_shader* tc_shader_get(tc_shader_handle h) {
    if (!g_initialized) return NULL;
    return (tc_shader*)tc_pool_get(&g_shader_pool, h);
}

bool tc_shader_is_valid(tc_shader_handle h) {
    if (!g_initialized) return false;
    return tc_pool_is_valid(&g_shader_pool, h);
}

bool tc_shader_destroy(tc_shader_handle h) {
    if (!g_initialized) return false;

    tc_shader* shader = tc_shader_get(h);
    if (!shader) return false;

    // Remove from UUID map
    tc_resource_map_remove(g_uuid_to_index, shader->uuid);

    // Remove from hash map
    if (shader->source_hash[0] != '\0') {
        tc_resource_map_remove(g_hash_to_index, shader->source_hash);
    }

    // Free shader data
    shader_free_data(shader);

    // Free slot in pool (bumps generation)
    return tc_pool_free_slot(&g_shader_pool, h);
}

bool tc_shader_contains(const char* uuid) {
    if (!g_initialized || !uuid) return false;
    return tc_resource_map_contains(g_uuid_to_index, uuid);
}

size_t tc_shader_count(void) {
    if (!g_initialized) return 0;
    return tc_pool_count(&g_shader_pool);
}

// ============================================================================
// Shader source operations
// ============================================================================

bool tc_shader_set_sources(
    tc_shader* shader,
    const char* vertex_source,
    const char* fragment_source,
    const char* geometry_source,
    const char* name,
    const char* source_path
) {
    if (!shader) return false;

    // Compute new hash to check if sources actually changed
    char new_hash[TC_SHADER_HASH_LEN];
    tc_shader_compute_hash(vertex_source, fragment_source, geometry_source, new_hash);

    // Check if sources are the same (by hash)
    if (shader->source_hash[0] != '\0' && strcmp(shader->source_hash, new_hash) == 0) {
        return false;  // No change
    }

    // Remove from old hash mapping
    if (shader->source_hash[0] != '\0') {
        tc_resource_map_remove(g_hash_to_index, shader->source_hash);
    }

    // Free old sources
    shader_free_data(shader);

    // Copy new sources
    shader->vertex_source = dup_string(vertex_source);
    shader->fragment_source = dup_string(fragment_source);
    shader->geometry_source = dup_string(geometry_source);

    // Update hash
    memcpy(shader->source_hash, new_hash, TC_SHADER_HASH_LEN);

    // Add to hash map (find by hash for deduplication)
    if (shader->source_hash[0] != '\0') {
        // Get handle from pool to find index
        for (uint32_t i = 0; i < g_shader_pool.capacity; i++) {
            if (g_shader_pool.states[i] == TC_SLOT_OCCUPIED) {
                tc_shader* s = (tc_shader*)tc_pool_get_unchecked(&g_shader_pool, i);
                if (s == shader) {
                    tc_resource_map_add(g_hash_to_index, shader->source_hash, tc_pack_index(i));
                    break;
                }
            }
        }
    }

    // Set name and path
    if (name && name[0] != '\0') {
        shader->name = tgfx_intern_string(name);
    }
    if (source_path && source_path[0] != '\0') {
        shader->source_path = tgfx_intern_string(source_path);
    }

    shader->version++;
    return true;
}

tc_shader_handle tc_shader_from_sources(
    const char* vertex_source,
    const char* fragment_source,
    const char* geometry_source,
    const char* name,
    const char* source_path,
    const char* uuid
) {
    if (!vertex_source || !fragment_source) {
        tc_log(TC_LOG_ERROR, "tc_shader_from_sources: vertex and fragment sources required");
        return tc_shader_handle_invalid();
    }

    // If uuid provided, find or create shader with that uuid
    if (uuid && uuid[0] != '\0') {
        tc_shader_handle existing = tc_shader_find(uuid);
        if (!tc_shader_handle_is_invalid(existing)) {
            // Update existing shader's sources
            tc_shader* shader = tc_shader_get(existing);
            tc_shader_set_sources(shader, vertex_source, fragment_source, geometry_source, name, source_path);
            return existing;
        }
        // Create new shader with specified uuid
        tc_shader_handle h = tc_shader_create(uuid);
        if (tc_shader_handle_is_invalid(h)) {
            return h;
        }
        tc_shader* shader = tc_shader_get(h);
        if (!tc_shader_set_sources(shader, vertex_source, fragment_source, geometry_source, name, source_path)) {
            tc_shader_destroy(h);
            return tc_shader_handle_invalid();
        }
        return h;
    }

    // No uuid - use hash-based lookup
    char hash[TC_SHADER_HASH_LEN];
    tc_shader_compute_hash(vertex_source, fragment_source, geometry_source, hash);

    tc_shader_handle existing = tc_shader_find_by_hash(hash);
    if (!tc_shader_handle_is_invalid(existing)) {
        return existing;
    }

    // Create new shader with auto-generated uuid
    tc_shader_handle h = tc_shader_create(NULL);
    if (tc_shader_handle_is_invalid(h)) {
        return h;
    }

    tc_shader* shader = tc_shader_get(h);
    if (!tc_shader_set_sources(shader, vertex_source, fragment_source, geometry_source, name, source_path)) {
        tc_shader_destroy(h);
        return tc_shader_handle_invalid();
    }

    return h;
}

// ============================================================================
// Reference counting
// ============================================================================

void tc_shader_add_ref(tc_shader* shader) {
    if (shader) {
        shader->ref_count++;
    }
}

bool tc_shader_release(tc_shader* shader) {
    if (!shader) {
        tc_log(TC_LOG_WARN, "tc_shader_release: null shader");
        return false;
    }
    if (shader->ref_count == 0) {
        tc_log(TC_LOG_WARN, "tc_shader_release: '%s' [%s] already at ref_count=0",
            shader->name ? shader->name : "?", shader->uuid);
        return false;
    }

    shader->ref_count--;

    if (shader->ref_count == 0) {
        // Find and destroy by uuid
        tc_shader_handle h = tc_shader_find(shader->uuid);
        if (!tc_shader_handle_is_invalid(h)) {
            tc_shader_destroy(h);
        }
        return true;
    }
    return false;
}

// ============================================================================
// Variant support (registry stores relationship, caller manages variants)
// ============================================================================

void tc_shader_set_variant_info(
    tc_shader* shader,
    tc_shader_handle original,
    tc_shader_variant_op op
) {
    if (!shader) return;

    tc_shader* orig = tc_shader_get(original);
    if (!orig) {
        tc_log(TC_LOG_WARN, "tc_shader_set_variant_info: invalid original handle");
        return;
    }

    shader->is_variant = 1;
    shader->variant_op = (uint8_t)op;
    shader->original_handle = original;
    shader->original_version = orig->version;
}

bool tc_shader_variant_is_stale(tc_shader_handle variant) {
    tc_shader* v = tc_shader_get(variant);
    if (!v || !v->is_variant) {
        return false;
    }

    tc_shader* orig = tc_shader_get(v->original_handle);
    if (!orig) {
        // Original was destroyed, variant is stale
        return true;
    }

    return orig->version != v->original_version;
}

// ============================================================================
// Iteration
// ============================================================================

typedef struct {
    tc_shader_iter_fn callback;
    void* user_data;
} shader_iter_ctx;

static bool shader_iter_adapter(uint32_t index, void* item, void* ctx_ptr) {
    shader_iter_ctx* ctx = (shader_iter_ctx*)ctx_ptr;
    tc_shader* shader = (tc_shader*)item;

    tc_shader_handle h;
    h.index = index;
    h.generation = g_shader_pool.generations[index];

    return ctx->callback(h, shader, ctx->user_data);
}

void tc_shader_foreach(tc_shader_iter_fn callback, void* user_data) {
    if (!g_initialized || !callback) return;
    shader_iter_ctx ctx = { callback, user_data };
    tc_pool_foreach(&g_shader_pool, shader_iter_adapter, &ctx);
}

// ============================================================================
// Info collection
// ============================================================================

typedef struct {
    tc_shader_info* infos;
    size_t count;
} shader_info_collector;

static bool collect_shader_info(tc_shader_handle h, tc_shader* shader, void* user_data) {
    shader_info_collector* collector = (shader_info_collector*)user_data;

    tc_shader_info* info = &collector->infos[collector->count++];
    info->handle = h;
    strncpy(info->uuid, shader->uuid, sizeof(info->uuid) - 1);
    info->uuid[sizeof(info->uuid) - 1] = '\0';
    strncpy(info->source_hash, shader->source_hash, sizeof(info->source_hash) - 1);
    info->source_hash[sizeof(info->source_hash) - 1] = '\0';
    info->name = shader->name;
    info->source_path = shader->source_path;
    info->ref_count = shader->ref_count;
    info->version = shader->version;
    info->features = shader->features;
    info->source_size = tc_shader_source_size(shader);
    info->is_variant = shader->is_variant;
    info->variant_op = shader->variant_op;
    info->has_geometry = tc_shader_has_geometry(shader);

    return true;
}

tc_shader_info* tc_shader_get_all_info(size_t* count) {
    if (!count) return NULL;
    *count = 0;

    if (!g_initialized) return NULL;

    size_t shader_count = tc_pool_count(&g_shader_pool);
    if (shader_count == 0) return NULL;

    tc_shader_info* infos = (tc_shader_info*)malloc(shader_count * sizeof(tc_shader_info));
    if (!infos) {
        tc_log(TC_LOG_ERROR, "tc_shader_get_all_info: allocation failed");
        return NULL;
    }

    shader_info_collector collector = { infos, 0 };
    tc_shader_foreach(collect_shader_info, &collector);

    *count = collector.count;
    return infos;
}
