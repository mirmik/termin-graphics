// tc_shader_registry.c - Shader registry with pool + hash table and variant support
#include "tgfx/resources/tc_shader_registry.h"
#include <tcbase/tc_pool.h>
#include <tcbase/tc_resource_map.h>
#include <tcbase/tc_registry_utils.h>
#include <tcbase/tc_log.h>
#include <tcbase/tgfx_intern_string.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// ============================================================================
// Global state
// ============================================================================

static tc_pool g_shader_pool;
static tc_resource_map* g_shader_uuid_to_index = NULL;   // UUID -> uint32_t index
static tc_resource_map* g_shader_hash_to_index = NULL;   // source_hash -> uint32_t index
static uint64_t g_shader_next_uuid = 1;
static bool g_shader_initialized = false;
static tc_shader_destroy_hook_fn g_destroy_hooks[TC_MAX_SHADER_DESTROY_HOOKS];
static void* g_destroy_hook_user[TC_MAX_SHADER_DESTROY_HOOKS];
static int g_destroy_hook_count = 0;

// Duplicate a string (NULL-safe)
static char* dup_string(const char* s) {
    if (!s || s[0] == '\0') return NULL;
    size_t len = strlen(s) + 1;
    char* copy = (char*)malloc(len);
    if (copy) memcpy(copy, s, len);
    return copy;
}

static bool tc_shader_language_valid(tc_shader_language language) {
    return language == TC_SHADER_LANGUAGE_GLSL
        || language == TC_SHADER_LANGUAGE_SLANG
        || language == TC_SHADER_LANGUAGE_HLSL;
}

static bool tc_shader_artifact_policy_valid(tc_shader_artifact_policy policy) {
    return policy == TC_SHADER_ARTIFACT_OPTIONAL
        || policy == TC_SHADER_ARTIFACT_REQUIRED;
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
    if (shader->vertex_entry) {
        free(shader->vertex_entry);
        shader->vertex_entry = NULL;
    }
    if (shader->fragment_entry) {
        free(shader->fragment_entry);
        shader->fragment_entry = NULL;
    }
    if (shader->geometry_entry) {
        free(shader->geometry_entry);
        shader->geometry_entry = NULL;
    }
    if (shader->material_ubo_entries) {
        free(shader->material_ubo_entries);
        shader->material_ubo_entries = NULL;
    }
    shader->material_ubo_entry_count = 0;
    shader->material_ubo_block_size = 0;
    if (shader->resource_bindings) {
        for (uint32_t i = 0; i < shader->resource_binding_count; ++i) {
            if (shader->resource_bindings[i].fields) {
                free(shader->resource_bindings[i].fields);
                shader->resource_bindings[i].fields = NULL;
            }
        }
        free(shader->resource_bindings);
        shader->resource_bindings = NULL;
    }
    shader->resource_binding_count = 0;
    shader->has_resource_layout = 0;
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

static uint64_t fnv1a_u32(uint32_t value, uint64_t hash) {
    for (uint32_t i = 0; i < 4; i++) {
        hash ^= (uint8_t)((value >> (i * 8)) & 0xffu);
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

static void tc_shader_compute_identity_hash(
    const char* vertex_source,
    const char* fragment_source,
    const char* geometry_source,
    tc_shader_language language,
    tc_shader_artifact_policy artifact_policy,
    char* hash_out
) {
    if (language == TC_SHADER_LANGUAGE_GLSL
        && artifact_policy == TC_SHADER_ARTIFACT_OPTIONAL) {
        tc_shader_compute_hash(vertex_source, fragment_source, geometry_source, hash_out);
        return;
    }

    uint64_t hash = 0xcbf29ce484222325ULL;

    hash = fnv1a_string(vertex_source, hash);
    hash = fnv1a_string("::", hash);
    hash = fnv1a_string(fragment_source, hash);
    hash = fnv1a_string("::", hash);
    hash = fnv1a_string(geometry_source, hash);
    hash = fnv1a_string("::meta::", hash);
    hash = fnv1a_u32((uint32_t)language, hash);
    hash = fnv1a_string("::", hash);
    hash = fnv1a_u32((uint32_t)artifact_policy, hash);

    snprintf(hash_out, TC_SHADER_HASH_LEN, "%016llx", (unsigned long long)hash);
}

static void shader_remove_hash_mapping(tc_shader* shader) {
    if (shader && shader->source_hash[0] != '\0') {
        tc_resource_map_remove(g_shader_hash_to_index, shader->source_hash);
    }
}

static void shader_add_hash_mapping(tc_shader* shader) {
    if (!shader || shader->source_hash[0] == '\0') {
        return;
    }
    tc_resource_map_add(
        g_shader_hash_to_index,
        shader->source_hash,
        tc_pack_index(shader->pool_index)
    );
}

void tc_shader_update_hash(tc_shader* shader) {
    if (!shader) return;
    tc_shader_compute_identity_hash(
        shader->vertex_source,
        shader->fragment_source,
        shader->geometry_source,
        (tc_shader_language)shader->language,
        (tc_shader_artifact_policy)shader->artifact_policy,
        shader->source_hash
    );
}

// ============================================================================
// Lifecycle
// ============================================================================

void tc_shader_init(void) {
    TC_REGISTRY_INIT_GUARD(g_shader_initialized, "tc_shader");

    if (!tc_pool_init(&g_shader_pool, sizeof(tc_shader), 64)) {
        tc_log(TC_LOG_ERROR, "tc_shader_init: failed to init pool");
        return;
    }

    g_shader_uuid_to_index = tc_resource_map_new(NULL);
    if (!g_shader_uuid_to_index) {
        tc_log(TC_LOG_ERROR, "tc_shader_init: failed to create uuid map");
        tc_pool_free(&g_shader_pool);
        return;
    }

    g_shader_hash_to_index = tc_resource_map_new(NULL);
    if (!g_shader_hash_to_index) {
        tc_log(TC_LOG_ERROR, "tc_shader_init: failed to create hash map");
        tc_resource_map_free(g_shader_uuid_to_index);
        g_shader_uuid_to_index = NULL;
        tc_pool_free(&g_shader_pool);
        return;
    }

    g_shader_next_uuid = 1;
    g_shader_initialized = true;
}

void tc_shader_shutdown(void) {
    TC_REGISTRY_SHUTDOWN_GUARD(g_shader_initialized, "tc_shader");

    // Free shader data for all occupied slots
    for (uint32_t i = 0; i < g_shader_pool.capacity; i++) {
        if (g_shader_pool.states[i] == TC_SLOT_OCCUPIED) {
            tc_shader* shader = (tc_shader*)tc_pool_get_unchecked(&g_shader_pool, i);
            shader_free_data(shader);
        }
    }

    tc_pool_free(&g_shader_pool);
    tc_resource_map_free(g_shader_uuid_to_index);
    tc_resource_map_free(g_shader_hash_to_index);
    g_shader_uuid_to_index = NULL;
    g_shader_hash_to_index = NULL;
    g_shader_next_uuid = 1;
    g_shader_initialized = false;
}

// ============================================================================
// Handle-based API
// ============================================================================

tc_shader_handle tc_shader_create(const char* uuid) {
    if (!g_shader_initialized) {
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
        tc_generate_prefixed_uuid(uuid_buf, sizeof(uuid_buf), "shader", &g_shader_next_uuid);
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
    shader->language = TC_SHADER_LANGUAGE_GLSL;
    shader->artifact_policy = TC_SHADER_ARTIFACT_OPTIONAL;
    shader->pool_index = h.index;
    shader->original_handle = tc_shader_handle_invalid();

    // Add to UUID map
    if (!tc_resource_map_add(g_shader_uuid_to_index, shader->uuid, tc_pack_index(h.index))) {
        tc_log(TC_LOG_ERROR, "tc_shader_create: failed to add to uuid map");
        tc_pool_free_slot(&g_shader_pool, h);
        return tc_shader_handle_invalid();
    }

    return h;
}

tc_shader_handle tc_shader_find(const char* uuid) {
    if (!g_shader_initialized || !uuid) {
        return tc_shader_handle_invalid();
    }

    void* ptr = tc_resource_map_get(g_shader_uuid_to_index, uuid);
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
    if (!g_shader_initialized || !source_hash) {
        return tc_shader_handle_invalid();
    }

    void* ptr = tc_resource_map_get(g_shader_hash_to_index, source_hash);
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
    if (!g_shader_initialized || !name) {
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
    if (!g_shader_initialized) return NULL;
    return (tc_shader*)tc_pool_get(&g_shader_pool, h);
}

bool tc_shader_is_valid(tc_shader_handle h) {
    if (!g_shader_initialized) return false;
    return tc_pool_is_valid(&g_shader_pool, h);
}

bool tc_shader_destroy(tc_shader_handle h) {
    if (!g_shader_initialized) return false;

    tc_shader* shader = tc_shader_get(h);
    if (!shader) return false;

    const uint32_t pool_index = shader->pool_index;
    for (int i = 0; i < g_destroy_hook_count; i++) {
        g_destroy_hooks[i](pool_index, g_destroy_hook_user[i]);
    }

    // Remove from UUID map
    tc_resource_map_remove(g_shader_uuid_to_index, shader->uuid);

    // Remove from hash map
    if (shader->source_hash[0] != '\0') {
        tc_resource_map_remove(g_shader_hash_to_index, shader->source_hash);
    }

    // Free shader data
    shader_free_data(shader);

    // Free slot in pool (bumps generation)
    return tc_pool_free_slot(&g_shader_pool, h);
}

void tc_shader_registry_add_destroy_hook(
    tc_shader_destroy_hook_fn cb, void* user_data
) {
    if (!cb) return;
    if (g_destroy_hook_count >= TC_MAX_SHADER_DESTROY_HOOKS) {
        tc_log(TC_LOG_ERROR,
               "tc_shader_registry: destroy-hook table full (%d)",
               TC_MAX_SHADER_DESTROY_HOOKS);
        return;
    }
    g_destroy_hooks[g_destroy_hook_count] = cb;
    g_destroy_hook_user[g_destroy_hook_count] = user_data;
    g_destroy_hook_count++;
}

void tc_shader_registry_remove_destroy_hook(
    tc_shader_destroy_hook_fn cb, void* user_data
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

bool tc_shader_contains(const char* uuid) {
    if (!g_shader_initialized || !uuid) return false;
    return tc_resource_map_contains(g_shader_uuid_to_index, uuid);
}

size_t tc_shader_count(void) {
    if (!g_shader_initialized) return 0;
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
    tc_shader_compute_identity_hash(
        vertex_source,
        fragment_source,
        geometry_source,
        (tc_shader_language)shader->language,
        (tc_shader_artifact_policy)shader->artifact_policy,
        new_hash
    );

    // Check if sources are the same (by hash)
    if (shader->source_hash[0] != '\0' && strcmp(shader->source_hash, new_hash) == 0) {
        return false;  // No change
    }

    // Remove from old hash mapping
    shader_remove_hash_mapping(shader);

    // Free old sources
    shader_free_data(shader);

    // Copy new sources
    shader->vertex_source = dup_string(vertex_source);
    shader->fragment_source = dup_string(fragment_source);
    shader->geometry_source = dup_string(geometry_source);

    // Update hash
    memcpy(shader->source_hash, new_hash, TC_SHADER_HASH_LEN);

    // Add to hash map (find by hash for deduplication)
    shader_add_hash_mapping(shader);

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

tc_shader_handle tc_shader_from_sources_ex(
    const char* vertex_source,
    const char* fragment_source,
    const char* geometry_source,
    const char* name,
    const char* source_path,
    const char* uuid,
    tc_shader_language language,
    tc_shader_artifact_policy artifact_policy
) {
    // NULL vertex_source is permitted for FS-only shaders — e.g. post-
    // effect passes that reuse RenderContext2's built-in FSQ vertex shader
    // and only need to register the FS in the tc_shader registry for
    // hash-based dedup + compile caching.
    if (!fragment_source) {
        tc_log(TC_LOG_ERROR, "tc_shader_from_sources: fragment_source required");
        return tc_shader_handle_invalid();
    }

    if (!tc_shader_language_valid(language)) {
        tc_log(TC_LOG_ERROR,
               "tc_shader_from_sources_ex: invalid shader language %u",
               (unsigned)language);
        return tc_shader_handle_invalid();
    }
    if (!tc_shader_artifact_policy_valid(artifact_policy)) {
        tc_log(TC_LOG_ERROR,
               "tc_shader_from_sources_ex: invalid artifact policy %u",
               (unsigned)artifact_policy);
        return tc_shader_handle_invalid();
    }

    // If uuid provided, find or create shader with that uuid
    if (uuid && uuid[0] != '\0') {
        tc_shader_handle existing = tc_shader_find(uuid);
        if (!tc_shader_handle_is_invalid(existing)) {
            // Update existing shader's sources
            tc_shader* shader = tc_shader_get(existing);
            tc_shader_set_language(shader, language);
            tc_shader_set_artifact_policy(shader, artifact_policy);
            tc_shader_set_sources(shader, vertex_source, fragment_source, geometry_source, name, source_path);
            return existing;
        }
        // Create new shader with specified uuid
        tc_shader_handle h = tc_shader_create(uuid);
        if (tc_shader_handle_is_invalid(h)) {
            return h;
        }
        tc_shader* shader = tc_shader_get(h);
        shader->language = (uint32_t)language;
        shader->artifact_policy = (uint32_t)artifact_policy;
        if (!tc_shader_set_sources(shader, vertex_source, fragment_source, geometry_source, name, source_path)) {
            tc_shader_destroy(h);
            return tc_shader_handle_invalid();
        }
        return h;
    }

    // No uuid - use hash-based lookup
    char hash[TC_SHADER_HASH_LEN];
    tc_shader_compute_identity_hash(
        vertex_source,
        fragment_source,
        geometry_source,
        language,
        artifact_policy,
        hash
    );

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
    shader->language = (uint32_t)language;
    shader->artifact_policy = (uint32_t)artifact_policy;
    if (!tc_shader_set_sources(shader, vertex_source, fragment_source, geometry_source, name, source_path)) {
        tc_shader_destroy(h);
        return tc_shader_handle_invalid();
    }

    return h;
}

tc_shader_handle tc_shader_from_sources(
    const char* vertex_source,
    const char* fragment_source,
    const char* geometry_source,
    const char* name,
    const char* source_path,
    const char* uuid
) {
    return tc_shader_from_sources_ex(
        vertex_source,
        fragment_source,
        geometry_source,
        name,
        source_path,
        uuid,
        TC_SHADER_LANGUAGE_GLSL,
        TC_SHADER_ARTIFACT_OPTIONAL
    );
}

tc_shader_handle tc_shader_register_static(
    const char* vertex_source,
    const char* fragment_source,
    const char* geometry_source,
    const char* name
) {
    // Re-use the normal creation / hash-dedup path, then mark the shader
    // as static and install the registry-held ref. The `is_static` flag
    // guards against double-ref on repeated calls with the same source
    // (hash hit returns the already-static handle — no extra add_ref).
    tc_shader_handle h = tc_shader_from_sources(
        vertex_source, fragment_source, geometry_source,
        name, /*source_path=*/NULL, /*uuid=*/NULL);
    if (tc_shader_handle_is_invalid(h)) return h;

    tc_shader* shader = tc_shader_get(h);
    if (shader && !shader->is_static) {
        shader->is_static = 1;
        tc_shader_add_ref(shader);
    }
    return h;
}

tc_shader_handle tc_shader_register_static_uuid(
    const char* vertex_source,
    const char* fragment_source,
    const char* geometry_source,
    const char* name,
    const char* uuid
) {
    if (!uuid || uuid[0] == '\0') {
        tc_log(TC_LOG_ERROR, "tc_shader_register_static_uuid: uuid required");
        return tc_shader_handle_invalid();
    }

    tc_shader_handle h = tc_shader_from_sources(
        vertex_source, fragment_source, geometry_source,
        name, /*source_path=*/NULL, uuid);
    if (tc_shader_handle_is_invalid(h)) return h;

    tc_shader* shader = tc_shader_get(h);
    if (shader && !shader->is_static) {
        shader->is_static = 1;
        tc_shader_add_ref(shader);
    }
    return h;
}

bool tc_shader_set_language(tc_shader* shader, tc_shader_language language) {
    if (!shader) {
        tc_log(TC_LOG_ERROR, "tc_shader_set_language: shader is NULL");
        return false;
    }
    if (!tc_shader_language_valid(language)) {
        tc_log(TC_LOG_ERROR,
               "tc_shader_set_language: invalid shader language %u for '%s'",
               (unsigned)language,
               shader->name ? shader->name : shader->uuid);
        return false;
    }
    if (shader->language == (uint32_t)language) {
        return false;
    }

    shader_remove_hash_mapping(shader);
    shader->language = (uint32_t)language;
    tc_shader_update_hash(shader);
    shader_add_hash_mapping(shader);
    shader->version++;
    return true;
}

tc_shader_language tc_shader_get_language(const tc_shader* shader) {
    if (!shader || !tc_shader_language_valid((tc_shader_language)shader->language)) {
        return TC_SHADER_LANGUAGE_GLSL;
    }
    return (tc_shader_language)shader->language;
}

bool tc_shader_set_artifact_policy(tc_shader* shader, tc_shader_artifact_policy policy) {
    if (!shader) {
        tc_log(TC_LOG_ERROR, "tc_shader_set_artifact_policy: shader is NULL");
        return false;
    }
    if (!tc_shader_artifact_policy_valid(policy)) {
        tc_log(TC_LOG_ERROR,
               "tc_shader_set_artifact_policy: invalid artifact policy %u for '%s'",
               (unsigned)policy,
               shader->name ? shader->name : shader->uuid);
        return false;
    }
    if (shader->artifact_policy == (uint32_t)policy) {
        return false;
    }

    shader_remove_hash_mapping(shader);
    shader->artifact_policy = (uint32_t)policy;
    tc_shader_update_hash(shader);
    shader_add_hash_mapping(shader);
    shader->version++;
    return true;
}

tc_shader_artifact_policy tc_shader_get_artifact_policy(const tc_shader* shader) {
    if (!shader || !tc_shader_artifact_policy_valid((tc_shader_artifact_policy)shader->artifact_policy)) {
        return TC_SHADER_ARTIFACT_OPTIONAL;
    }
    return (tc_shader_artifact_policy)shader->artifact_policy;
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
    tc_shader_set_language(shader, tc_shader_get_language(orig));
    tc_shader_set_artifact_policy(shader, tc_shader_get_artifact_policy(orig));
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

void tc_shader_make_variant_uuid(
    char* out_uuid,
    size_t out_size,
    const char* base_uuid,
    tc_shader_variant_op op
) {
    if (!out_uuid || out_size == 0) {
        return;
    }

    uint64_t hash = 0xcbf29ce484222325ULL;
    hash = fnv1a_string(base_uuid ? base_uuid : "", hash);
    hash = fnv1a_string("::variant::", hash);

    char op_buf[16];
    snprintf(op_buf, sizeof(op_buf), "%u", (unsigned)op);
    hash = fnv1a_string(op_buf, hash);

    snprintf(out_uuid, out_size, "shv_%016llx", (unsigned long long)hash);
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
    if (!g_shader_initialized || !callback) return;
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
    info->language = shader->language;
    info->artifact_policy = shader->artifact_policy;
    info->source_size = tc_shader_source_size(shader);
    info->is_variant = shader->is_variant;
    info->variant_op = shader->variant_op;
    info->has_geometry = tc_shader_has_geometry(shader);

    return true;
}

tc_shader_info* tc_shader_get_all_info(size_t* count) {
    if (!count) return NULL;
    *count = 0;

    if (!g_shader_initialized) return NULL;

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

// ============================================================================
// Material UBO layout transport
// ============================================================================


static int tc_shader_resource_binding_compare(const void* a, const void* b) {
    const tc_shader_resource_binding* ra = (const tc_shader_resource_binding*)a;
    const tc_shader_resource_binding* rb = (const tc_shader_resource_binding*)b;
    return strcmp(ra->name, rb->name);
}

static int tc_shader_find_resource_binding_index(
    const tc_shader* shader,
    const char* name
) {
    if (!shader || !name || name[0] == '\0') return -1;
    for (uint32_t i = 0; i < shader->resource_binding_count; i++) {
        if (strcmp(shader->resource_bindings[i].name, name) == 0) {
            return (int)i;
        }
    }
    return -1;
}

static int tc_shader_find_resource_binding_index_sorted(
    const tc_shader* shader,
    const char* name
) {
    if (!shader || !name || name[0] == '\0' || shader->resource_binding_count == 0) {
        return -1;
    }
    uint32_t lo = 0;
    uint32_t hi = shader->resource_binding_count;
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2;
        int cmp = strcmp(shader->resource_bindings[mid].name, name);
        if (cmp < 0) {
            lo = mid + 1;
        } else if (cmp > 0) {
            hi = mid;
        } else {
            return (int)mid;
        }
    }
    return -1;
}

static bool tc_shader_upsert_resource_binding(
    tc_shader* shader,
    const tc_shader_resource_binding* binding
) {
    if (!shader || !binding || binding->name[0] == '\0') return false;

    int existing = tc_shader_find_resource_binding_index(shader, binding->name);
    if (existing >= 0) {
        shader->resource_bindings[existing] = *binding;
        shader->resource_bindings[existing].name[TC_SHADER_RESOURCE_NAME_MAX - 1] = '\0';
        return true;
    }

    uint32_t new_count = shader->resource_binding_count + 1u;
    size_t bytes = (size_t)new_count * sizeof(tc_shader_resource_binding);
    tc_shader_resource_binding* copy =
        (tc_shader_resource_binding*)realloc(shader->resource_bindings, bytes);
    if (!copy) {
        tc_log(TC_LOG_ERROR,
               "tc_shader_upsert_resource_binding: allocation failed (%u entries)",
               new_count);
        return false;
    }
    shader->resource_bindings = copy;
    shader->resource_bindings[shader->resource_binding_count] = *binding;
    shader->resource_bindings[shader->resource_binding_count]
        .name[TC_SHADER_RESOURCE_NAME_MAX - 1] = '\0';
    shader->resource_binding_count = new_count;
    return true;
}

static void tc_shader_remove_resource_binding(tc_shader* shader, const char* name) {
    int index = tc_shader_find_resource_binding_index(shader, name);
    if (index < 0) return;

    uint32_t last = shader->resource_binding_count - 1u;
    if ((uint32_t)index != last) {
        shader->resource_bindings[index] = shader->resource_bindings[last];
    }
    shader->resource_binding_count = last;
    if (last == 0) {
        free(shader->resource_bindings);
        shader->resource_bindings = NULL;
        return;
    }

    size_t bytes = (size_t)last * sizeof(tc_shader_resource_binding);
    tc_shader_resource_binding* copy =
        (tc_shader_resource_binding*)realloc(shader->resource_bindings, bytes);
    if (copy) {
        shader->resource_bindings = copy;
    }
}

void tc_shader_set_material_ubo_layout(
    tc_shader* shader,
    const tc_material_ubo_entry* entries,
    uint32_t count,
    uint32_t block_size
) {
    if (!shader) {
        tc_log(TC_LOG_ERROR, "[Stage 5.H bridge] set_material_ubo_layout called with NULL shader");
        return;
    }

    // Release any previous layout.
    if (shader->material_ubo_entries) {
        free(shader->material_ubo_entries);
        shader->material_ubo_entries = NULL;
    }
    shader->material_ubo_entry_count = 0;
    shader->material_ubo_block_size = 0;

    if (count == 0 || !entries) {
        tc_shader_remove_resource_binding(shader, TC_SHADER_RESOURCE_MATERIAL);
        return;
    }

    size_t bytes = (size_t)count * sizeof(tc_material_ubo_entry);
    tc_material_ubo_entry* copy = (tc_material_ubo_entry*)malloc(bytes);
    if (!copy) {
        tc_log(TC_LOG_ERROR, "tc_shader_set_material_ubo_layout: allocation failed (%u entries)", count);
        return;
    }
    memcpy(copy, entries, bytes);

    shader->material_ubo_entries = copy;
    shader->material_ubo_entry_count = count;
    shader->material_ubo_block_size = block_size;

    tc_shader_resource_binding material_binding;
    memset(&material_binding, 0, sizeof(material_binding));
    strncpy(
        material_binding.name,
        TC_SHADER_RESOURCE_MATERIAL,
        TC_SHADER_RESOURCE_NAME_MAX - 1);
    material_binding.name[TC_SHADER_RESOURCE_NAME_MAX - 1] = '\0';
    material_binding.kind = TC_SHADER_RESOURCE_CONSTANT_BUFFER;
    material_binding.scope = TC_SHADER_RESOURCE_SCOPE_MATERIAL;
    material_binding.set = TC_SHADER_RESOURCE_SET_DEFAULT;
    material_binding.binding = TC_SHADER_RESOURCE_BINDING_MATERIAL;
    material_binding.stage_mask = TC_SHADER_STAGE_ALL_GRAPHICS;
    material_binding.size = block_size;
    tc_shader_upsert_resource_binding(shader, &material_binding);
}

uint32_t tc_shader_material_ubo_entry_count(const tc_shader* shader) {
    return shader ? shader->material_ubo_entry_count : 0u;
}

const tc_material_ubo_entry* tc_shader_material_ubo_entries(const tc_shader* shader) {
    return shader ? shader->material_ubo_entries : NULL;
}

uint32_t tc_shader_material_ubo_block_size(const tc_shader* shader) {
    return shader ? shader->material_ubo_block_size : 0u;
}

// ============================================================================
// Shader resource layout
// ============================================================================

static void tc_shader_free_resource_binding_array(
    tc_shader_resource_binding* bindings,
    uint32_t count
) {
    if (!bindings) return;
    for (uint32_t i = 0; i < count; ++i) {
        free(bindings[i].fields);
        bindings[i].fields = NULL;
        bindings[i].field_count = 0;
    }
    free(bindings);
}

static bool tc_shader_validate_resource_layout(
    const tc_shader_resource_binding* bindings,
    uint32_t count
) {
    if (!bindings || count == 0) return true;
    for (uint32_t i = 0; i < count; ++i) {
        for (uint32_t j = i + 1u; j < count; ++j) {
            const tc_shader_resource_binding* a = &bindings[i];
            const tc_shader_resource_binding* b = &bindings[j];
            if (a->set != b->set || a->binding != b->binding) {
                continue;
            }
            if (strncmp(a->name, b->name, TC_SHADER_RESOURCE_NAME_MAX) != 0 ||
                a->kind != b->kind ||
                a->scope != b->scope) {
                tc_log(
                    TC_LOG_ERROR,
                    "tc_shader_set_resource_layout: conflicting resources at set=%u binding=%u: '%s' kind=%u scope=%u vs '%s' kind=%u scope=%u",
                    a->set,
                    a->binding,
                    a->name,
                    a->kind,
                    a->scope,
                    b->name,
                    b->kind,
                    b->scope);
                return false;
            }
        }
    }
    return true;
}

void tc_shader_set_resource_layout(
    tc_shader* shader,
    const tc_shader_resource_binding* bindings,
    uint32_t count
) {
    if (!shader) {
        tc_log(TC_LOG_ERROR, "tc_shader_set_resource_layout called with NULL shader");
        return;
    }

    if (count == 0 || !bindings) {
        if (shader->resource_bindings) {
            tc_shader_free_resource_binding_array(
                shader->resource_bindings,
                shader->resource_binding_count);
            shader->resource_bindings = NULL;
        }
        shader->resource_binding_count = 0;
        shader->has_resource_layout = 0;
        return;
    }

    tc_shader_resource_binding* copy =
        (tc_shader_resource_binding*)calloc(count, sizeof(tc_shader_resource_binding));
    if (!copy) {
        tc_log(TC_LOG_ERROR,
               "tc_shader_set_resource_layout: allocation failed (%u entries)",
               count);
        return;
    }
    for (uint32_t i = 0; i < count; i++) {
        copy[i] = bindings[i];
        copy[i].name[TC_SHADER_RESOURCE_NAME_MAX - 1] = '\0';
        copy[i].fields = NULL;
        if (bindings[i].field_count > 0 && bindings[i].fields) {
            size_t field_bytes =
                (size_t)bindings[i].field_count * sizeof(tc_shader_resource_field);
            copy[i].fields = (tc_shader_resource_field*)malloc(field_bytes);
            if (!copy[i].fields) {
                tc_log(
                    TC_LOG_ERROR,
                    "tc_shader_set_resource_layout: field allocation failed for '%s' (%u fields)",
                    copy[i].name,
                    bindings[i].field_count);
                tc_shader_free_resource_binding_array(copy, count);
                return;
            }
            memcpy(copy[i].fields, bindings[i].fields, field_bytes);
            for (uint32_t f = 0; f < bindings[i].field_count; ++f) {
                copy[i].fields[f].name[TC_SHADER_RESOURCE_NAME_MAX - 1] = '\0';
            }
        } else {
            copy[i].field_count = 0;
        }
    }

    if (!tc_shader_validate_resource_layout(copy, count)) {
        tc_shader_free_resource_binding_array(copy, count);
        return;
    }

    if (count > 1) {
        qsort(copy, count, sizeof(tc_shader_resource_binding),
              tc_shader_resource_binding_compare);
    }

    if (shader->resource_bindings) {
        tc_shader_free_resource_binding_array(
            shader->resource_bindings,
            shader->resource_binding_count);
        shader->resource_bindings = NULL;
    }
    shader->resource_binding_count = 0;

    shader->resource_bindings = copy;
    shader->resource_binding_count = count;
    shader->has_resource_layout = 1;
}

uint32_t tc_shader_resource_binding_count(const tc_shader* shader) {
    return shader ? shader->resource_binding_count : 0u;
}

const tc_shader_resource_binding* tc_shader_resource_bindings(const tc_shader* shader) {
    return shader ? shader->resource_bindings : NULL;
}

const tc_shader_resource_binding* tc_shader_find_resource_binding(
    const tc_shader* shader,
    const char* name
) {
    int index = tc_shader_find_resource_binding_index_sorted(shader, name);
    if (index < 0) return NULL;
    return &shader->resource_bindings[index];
}

bool tc_shader_has_resource_layout(const tc_shader* shader) {
    return shader && shader->has_resource_layout != 0;
}

void tc_shader_mark_resource_layout_known(tc_shader* shader) {
    if (shader) {
        shader->has_resource_layout = 1;
    }
}
