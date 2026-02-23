// tc_texture_registry.c - Texture registry with pool + hash table
#include "tgfx/resources/tc_texture_registry.h"
#include "tgfx/tc_pool.h"
#include "tgfx/tc_resource_map.h"
#include "tgfx/tc_registry_utils.h"
#include "tgfx/tgfx_log.h"
#include "tgfx/tgfx_intern_string.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// ============================================================================
// Global state
// ============================================================================

static tc_pool g_texture_pool;
static tc_resource_map* g_uuid_to_index = NULL;
static uint64_t g_next_uuid = 1;
static bool g_initialized = false;

static void texture_free_data(tc_texture* tex) {
    if (!tex) return;
    if (tex->data) {
        free(tex->data);
        tex->data = NULL;
    }
}

// ============================================================================
// Lifecycle
// ============================================================================

void tc_texture_init(void) {
    TC_REGISTRY_INIT_GUARD(g_initialized, "tc_texture");

    if (!tc_pool_init(&g_texture_pool, sizeof(tc_texture), 64)) {
        tgfx_log(TGFX_LOG_ERROR, "tc_texture_init: failed to init pool");
        return;
    }

    g_uuid_to_index = tc_resource_map_new(NULL);
    if (!g_uuid_to_index) {
        tgfx_log(TGFX_LOG_ERROR, "tc_texture_init: failed to create uuid map");
        tc_pool_free(&g_texture_pool);
        return;
    }

    g_next_uuid = 1;
    g_initialized = true;
}

void tc_texture_shutdown(void) {
    TC_REGISTRY_SHUTDOWN_GUARD(g_initialized, "tc_texture");

    // Free texture data for all occupied slots
    for (uint32_t i = 0; i < g_texture_pool.capacity; i++) {
        if (g_texture_pool.states[i] == TC_SLOT_OCCUPIED) {
            tc_texture* tex = (tc_texture*)tc_pool_get_unchecked(&g_texture_pool, i);
            texture_free_data(tex);
        }
    }

    tc_pool_free(&g_texture_pool);
    tc_resource_map_free(g_uuid_to_index);
    g_uuid_to_index = NULL;
    g_next_uuid = 1;
    g_initialized = false;
}

// ============================================================================
// Handle-based API
// ============================================================================

tc_texture_handle tc_texture_create(const char* uuid) {
    if (!g_initialized) {
        tc_texture_init();
    }

    char uuid_buf[40];
    const char* final_uuid;

    if (uuid && uuid[0] != '\0') {
        if (tc_texture_contains(uuid)) {
            tgfx_log(TGFX_LOG_WARN, "tc_texture_create: uuid '%s' already exists", uuid);
            return tc_texture_handle_invalid();
        }
        final_uuid = uuid;
    } else {
        tc_generate_prefixed_uuid(uuid_buf, sizeof(uuid_buf), "tex", &g_next_uuid);
        final_uuid = uuid_buf;
    }

    tc_handle h = tc_pool_alloc(&g_texture_pool);
    if (tc_handle_is_invalid(h)) {
        tgfx_log(TGFX_LOG_ERROR, "tc_texture_create: pool alloc failed");
        return tc_texture_handle_invalid();
    }

    tc_texture* tex = (tc_texture*)tc_pool_get(&g_texture_pool, h);
    memset(tex, 0, sizeof(tc_texture));
    strncpy(tex->header.uuid, final_uuid, sizeof(tex->header.uuid) - 1);
    tex->header.uuid[sizeof(tex->header.uuid) - 1] = '\0';
    tex->header.version = 1;
    tex->header.ref_count = 0;
    tex->header.pool_index = h.index;
    tex->flip_y = 1;  // Default for OpenGL

    if (!tc_resource_map_add(g_uuid_to_index, tex->header.uuid, tc_pack_index(h.index))) {
        tgfx_log(TGFX_LOG_ERROR, "tc_texture_create: failed to add to uuid map");
        tc_pool_free_slot(&g_texture_pool, h);
        return tc_texture_handle_invalid();
    }

    return h;
}

tc_texture_handle tc_texture_find(const char* uuid) {
    if (!g_initialized || !uuid) {
        return tc_texture_handle_invalid();
    }

    void* ptr = tc_resource_map_get(g_uuid_to_index, uuid);
    if (!tc_has_index(ptr)) {
        return tc_texture_handle_invalid();
    }

    uint32_t index = tc_unpack_index(ptr);
    if (index >= g_texture_pool.capacity) {
        return tc_texture_handle_invalid();
    }

    if (g_texture_pool.states[index] != TC_SLOT_OCCUPIED) {
        return tc_texture_handle_invalid();
    }

    tc_texture_handle h;
    h.index = index;
    h.generation = g_texture_pool.generations[index];
    return h;
}

tc_texture_handle tc_texture_find_by_name(const char* name) {
    if (!g_initialized || !name) {
        return tc_texture_handle_invalid();
    }

    for (uint32_t i = 0; i < g_texture_pool.capacity; i++) {
        if (g_texture_pool.states[i] == TC_SLOT_OCCUPIED) {
            tc_texture* tex = (tc_texture*)tc_pool_get_unchecked(&g_texture_pool, i);
            if (tex->header.name && strcmp(tex->header.name, name) == 0) {
                tc_texture_handle h;
                h.index = i;
                h.generation = g_texture_pool.generations[i];
                return h;
            }
        }
    }

    return tc_texture_handle_invalid();
}

tc_texture_handle tc_texture_get_or_create(const char* uuid) {
    if (!uuid || uuid[0] == '\0') {
        tgfx_log(TGFX_LOG_WARN, "tc_texture_get_or_create: empty uuid");
        return tc_texture_handle_invalid();
    }

    tc_texture_handle h = tc_texture_find(uuid);
    if (!tc_texture_handle_is_invalid(h)) {
        return h;
    }

    return tc_texture_create(uuid);
}

tc_texture* tc_texture_get(tc_texture_handle h) {
    if (!g_initialized) return NULL;
    return (tc_texture*)tc_pool_get(&g_texture_pool, h);
}

bool tc_texture_is_valid(tc_texture_handle h) {
    if (!g_initialized) return false;
    return tc_pool_is_valid(&g_texture_pool, h);
}

bool tc_texture_destroy(tc_texture_handle h) {
    if (!g_initialized) return false;

    tc_texture* tex = tc_texture_get(h);
    if (!tex) return false;

    tc_resource_map_remove(g_uuid_to_index, tex->header.uuid);
    texture_free_data(tex);

    return tc_pool_free_slot(&g_texture_pool, h);
}

bool tc_texture_contains(const char* uuid) {
    if (!g_initialized || !uuid) return false;
    return tc_resource_map_contains(g_uuid_to_index, uuid);
}

size_t tc_texture_count(void) {
    if (!g_initialized) return 0;
    return tc_pool_count(&g_texture_pool);
}

// ============================================================================
// Legacy pointer-based API
// ============================================================================

tc_texture* tc_texture_add(const char* uuid) {
    tc_texture_handle h = tc_texture_create(uuid);
    return tc_texture_get(h);
}

bool tc_texture_remove(const char* uuid) {
    tc_texture_handle h = tc_texture_find(uuid);
    if (tc_texture_handle_is_invalid(h)) return false;
    return tc_texture_destroy(h);
}

// ============================================================================
// Helper functions
// ============================================================================

size_t tc_texture_format_bpp(tc_texture_format format) {
    switch (format) {
        case TC_TEXTURE_RGBA8: return 4;
        case TC_TEXTURE_RGB8: return 3;
        case TC_TEXTURE_RG8: return 2;
        case TC_TEXTURE_R8: return 1;
        case TC_TEXTURE_RGBA16F: return 8;
        case TC_TEXTURE_RGB16F: return 6;
        default: return 4;
    }
}

uint8_t tc_texture_format_channels(tc_texture_format format) {
    switch (format) {
        case TC_TEXTURE_RGBA8:
        case TC_TEXTURE_RGBA16F: return 4;
        case TC_TEXTURE_RGB8:
        case TC_TEXTURE_RGB16F: return 3;
        case TC_TEXTURE_RG8: return 2;
        case TC_TEXTURE_R8: return 1;
        default: return 4;
    }
}

// ============================================================================
// Reference counting
// ============================================================================

void tc_texture_add_ref(tc_texture* tex) {
    if (tex) tex->header.ref_count++;
}

bool tc_texture_release(tc_texture* tex) {
    if (!tex) return false;
    if (tex->header.ref_count > 0) {
        tex->header.ref_count--;
    }
    return tex->header.ref_count == 0;
}

// ============================================================================
// Texture data helpers
// ============================================================================

bool tc_texture_set_data(
    tc_texture* tex,
    const void* data,
    uint32_t width,
    uint32_t height,
    uint8_t channels,
    const char* name,
    const char* source_path
) {
    if (!tex) return false;

    size_t data_size = (size_t)width * height * channels;

    void* new_data = NULL;
    if (data_size > 0) {
        new_data = malloc(data_size);
        if (!new_data) return false;
        if (data) {
            memcpy(new_data, data, data_size);
        } else {
            memset(new_data, 0, data_size);
        }
    }

    if (tex->data) free(tex->data);

    tex->data = new_data;
    tex->width = width;
    tex->height = height;
    tex->channels = channels;
    tex->format = TC_TEXTURE_RGBA8;
    tex->header.version++;

    if (name) {
        tex->header.name = tgfx_intern_string(name);
    }
    if (source_path) {
        tex->source_path = tgfx_intern_string(source_path);
    }

    return true;
}

void tc_texture_set_transforms(
    tc_texture* tex,
    bool flip_x,
    bool flip_y,
    bool transpose
) {
    if (!tex) return;
    tex->flip_x = flip_x ? 1 : 0;
    tex->flip_y = flip_y ? 1 : 0;
    tex->transpose = transpose ? 1 : 0;
    tex->header.version++;
}

// ============================================================================
// UUID computation
// ============================================================================

void tc_texture_compute_uuid(
    const void* data, size_t size,
    uint32_t width, uint32_t height, uint8_t channels,
    char* uuid_out
) {
    uint64_t hash = 14695981039346656037ULL;
    const uint8_t* bytes = (const uint8_t*)data;

    uint32_t dims[3] = { width, height, channels };
    for (size_t i = 0; i < sizeof(dims); i++) {
        hash ^= ((const uint8_t*)dims)[i];
        hash *= 1099511628211ULL;
    }

    for (size_t i = 0; i < size; i++) {
        hash ^= bytes[i];
        hash *= 1099511628211ULL;
    }

    snprintf(uuid_out, 40, "%016llx", (unsigned long long)hash);
}

// ============================================================================
// Iteration
// ============================================================================

typedef struct {
    tc_texture_iter_fn callback;
    void* user_data;
} texture_iter_ctx;

static bool texture_iter_adapter(uint32_t index, void* item, void* ctx_ptr) {
    texture_iter_ctx* ctx = (texture_iter_ctx*)ctx_ptr;
    tc_texture* tex = (tc_texture*)item;

    tc_texture_handle h;
    h.index = index;
    h.generation = g_texture_pool.generations[index];

    return ctx->callback(h, tex, ctx->user_data);
}

void tc_texture_foreach(tc_texture_iter_fn callback, void* user_data) {
    if (!g_initialized || !callback) return;
    texture_iter_ctx ctx = { callback, user_data };
    tc_pool_foreach(&g_texture_pool, texture_iter_adapter, &ctx);
}

// ============================================================================
// Info collection
// ============================================================================

typedef struct {
    tc_texture_info* infos;
    size_t count;
} info_collector;

static bool collect_texture_info(tc_texture_handle h, tc_texture* tex, void* user_data) {
    info_collector* collector = (info_collector*)user_data;

    tc_texture_info* info = &collector->infos[collector->count++];
    info->handle = h;
    strncpy(info->uuid, tex->header.uuid, sizeof(info->uuid) - 1);
    info->uuid[sizeof(info->uuid) - 1] = '\0';
    info->name = tex->header.name;
    info->source_path = tex->source_path;
    info->ref_count = tex->header.ref_count;
    info->version = tex->header.version;
    info->width = tex->width;
    info->height = tex->height;
    info->channels = tex->channels;
    info->format = tex->format;
    info->memory_bytes = (size_t)tex->width * tex->height * tex->channels;

    return true;
}

tc_texture_info* tc_texture_get_all_info(size_t* count) {
    if (!count) return NULL;
    *count = 0;

    if (!g_initialized) return NULL;

    size_t tex_count = tc_pool_count(&g_texture_pool);
    if (tex_count == 0) return NULL;

    tc_texture_info* infos = (tc_texture_info*)malloc(tex_count * sizeof(tc_texture_info));
    if (!infos) {
        tgfx_log(TGFX_LOG_ERROR, "tc_texture_get_all_info: allocation failed");
        return NULL;
    }

    info_collector collector = { infos, 0 };
    tc_texture_foreach(collect_texture_info, &collector);

    *count = collector.count;
    return infos;
}
