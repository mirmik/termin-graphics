// tc_texture_registry.c - Texture registry with pool + hash table
#include "tgfx/resources/tc_texture_registry.h"
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

static tc_pool g_texture_pool;
static tc_resource_map* g_texture_uuid_to_index = NULL;
static uint64_t g_texture_next_uuid = 1;
// Survives registry shutdown so handles from a previous runtime generation
// cannot alias newly allocated slots after reinitialization.
static uint32_t g_texture_generation_floor = 1;
static bool g_texture_initialized = false;

// Destroy-hook subscription table. See tc_texture_registry.h.
static tc_texture_destroy_hook_fn g_destroy_hooks[TC_MAX_TEXTURE_DESTROY_HOOKS];
static void* g_destroy_hook_user[TC_MAX_TEXTURE_DESTROY_HOOKS];
static int g_destroy_hook_count = 0;

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
    TC_REGISTRY_INIT_GUARD(g_texture_initialized, "tc_texture");

    if (!tc_pool_init(&g_texture_pool, sizeof(tc_texture), 64)) {
        tc_log(TC_LOG_ERROR, "tc_texture_init: failed to init pool");
        return;
    }
    for (uint32_t i = 0; i < g_texture_pool.capacity; i++) {
        g_texture_pool.generations[i] = g_texture_generation_floor;
    }

    g_texture_uuid_to_index = tc_resource_map_new(NULL);
    if (!g_texture_uuid_to_index) {
        tc_log(TC_LOG_ERROR, "tc_texture_init: failed to create uuid map");
        tc_pool_free(&g_texture_pool);
        return;
    }

    g_texture_next_uuid = 1;
    g_texture_initialized = true;
}

void tc_texture_shutdown(void) {
    TC_REGISTRY_SHUTDOWN_GUARD(g_texture_initialized, "tc_texture");

    uint32_t max_generation = g_texture_generation_floor;
    for (uint32_t i = 0; i < g_texture_pool.capacity; i++) {
        if (g_texture_pool.generations[i] > max_generation) {
            max_generation = g_texture_pool.generations[i];
        }
    }
    g_texture_generation_floor = max_generation + 1;
    if (g_texture_generation_floor == 0) {
        g_texture_generation_floor = 1;
    }

    // Free texture data for all occupied slots
    for (uint32_t i = 0; i < g_texture_pool.capacity; i++) {
        if (g_texture_pool.states[i] == TC_SLOT_OCCUPIED) {
            tc_texture* tex = (tc_texture*)tc_pool_get_unchecked(&g_texture_pool, i);
            texture_free_data(tex);
        }
    }

    tc_pool_free(&g_texture_pool);
    tc_resource_map_free(g_texture_uuid_to_index);
    g_texture_uuid_to_index = NULL;
    g_texture_next_uuid = 1;
    g_texture_initialized = false;
}

static tc_handle texture_pool_alloc(void) {
    tc_handle handle = tc_pool_alloc(&g_texture_pool);
    if (!tc_handle_is_invalid(handle) &&
        handle.generation < g_texture_generation_floor) {
        g_texture_pool.generations[handle.index] = g_texture_generation_floor;
        handle.generation = g_texture_generation_floor;
    }
    return handle;
}

// ============================================================================
// Handle-based API
// ============================================================================

tc_texture_handle tc_texture_create(const char* uuid) {
    if (!g_texture_initialized) {
        tc_texture_init();
    }

    char uuid_buf[40];
    const char* final_uuid;

    if (uuid && uuid[0] != '\0') {
        if (tc_texture_contains(uuid)) {
            tc_log(TC_LOG_WARN, "tc_texture_create: uuid '%s' already exists", uuid);
            return tc_texture_handle_invalid();
        }
        final_uuid = uuid;
    } else {
        tc_generate_prefixed_uuid(uuid_buf, sizeof(uuid_buf), "tex", &g_texture_next_uuid);
        final_uuid = uuid_buf;
    }

    tc_handle h = texture_pool_alloc();
    if (tc_handle_is_invalid(h)) {
        tc_log(TC_LOG_ERROR, "tc_texture_create: pool alloc failed");
        return tc_texture_handle_invalid();
    }

    tc_texture* tex = (tc_texture*)tc_pool_get(&g_texture_pool, h);
    memset(tex, 0, sizeof(tc_texture));
    tc_resource_header_set_uuid(&tex->header, final_uuid, "tc_texture_create");
    tex->header.version = 1;
    tex->header.ref_count = 0;
    tex->header.pool_index = h.index;
    tex->header.is_loaded = 1;
    tex->flip_y = 1;  // Default for OpenGL
    tex->storage_kind = TC_TEXTURE_STORAGE_CPU_FIRST;
    tex->usage = TC_TEXTURE_USAGE_SAMPLED;

    if (!tc_resource_map_add(g_texture_uuid_to_index, tex->header.uuid, tc_pack_index(h.index))) {
        tc_log(TC_LOG_ERROR, "tc_texture_create: failed to add to uuid map");
        tc_pool_free_slot(&g_texture_pool, h);
        return tc_texture_handle_invalid();
    }

    return h;
}

tc_texture_handle tc_texture_find(const char* uuid) {
    if (!g_texture_initialized || !uuid) {
        return tc_texture_handle_invalid();
    }

    void* ptr = tc_resource_map_get(g_texture_uuid_to_index, uuid);
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
    if (!g_texture_initialized || !name) {
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
        tc_log(TC_LOG_WARN, "tc_texture_get_or_create: empty uuid");
        return tc_texture_handle_invalid();
    }

    tc_texture_handle h = tc_texture_find(uuid);
    if (!tc_texture_handle_is_invalid(h)) {
        return h;
    }

    return tc_texture_create(uuid);
}

static tc_texture_handle tc_texture_get_builtin_rgba8(
    const char* uuid,
    const uint8_t pixel[4]
) {
    tc_texture_handle handle = tc_texture_find(uuid);
    if (!tc_texture_handle_is_invalid(handle)) {
        return handle;
    }

    handle = tc_texture_create(uuid);
    tc_texture* texture = tc_texture_get(handle);
    if (!texture) {
        tc_log(TC_LOG_ERROR,
               "tc_texture: failed to create built-in texture '%s'", uuid);
        return tc_texture_handle_invalid();
    }

    if (!tc_texture_set_data(texture, pixel, 1, 1, 4, uuid, NULL)) {
        tc_log(TC_LOG_ERROR,
               "tc_texture: failed to initialize built-in texture '%s'", uuid);
        tc_texture_destroy(handle);
        return tc_texture_handle_invalid();
    }
    tc_texture_set_transforms(texture, false, false, false);
    return handle;
}

tc_texture_handle tc_texture_get_white_1x1(void) {
    static const uint8_t pixel[4] = {255, 255, 255, 255};
    return tc_texture_get_builtin_rgba8("__white_1x1__", pixel);
}

tc_texture_handle tc_texture_get_normal_1x1(void) {
    static const uint8_t pixel[4] = {128, 128, 255, 255};
    return tc_texture_get_builtin_rgba8("__normal_1x1__", pixel);
}

// ============================================================================
// Lazy loading API
// ============================================================================

tc_texture_handle tc_texture_declare(const char* uuid, const char* name) {
    if (!g_texture_initialized) {
        tc_texture_init();
    }

    if (!uuid || uuid[0] == '\0') {
        tc_log(TC_LOG_WARN, "tc_texture_declare: empty uuid");
        return tc_texture_handle_invalid();
    }

    tc_texture_handle existing = tc_texture_find(uuid);
    if (!tc_texture_handle_is_invalid(existing)) {
        return existing;
    }

    tc_handle h = texture_pool_alloc();
    if (tc_handle_is_invalid(h)) {
        tc_log(TC_LOG_ERROR, "tc_texture_declare: pool alloc failed");
        return tc_texture_handle_invalid();
    }

    tc_texture* tex = (tc_texture*)tc_pool_get(&g_texture_pool, h);
    memset(tex, 0, sizeof(tc_texture));
    tc_resource_header_set_uuid(&tex->header, uuid, "tc_texture_declare");
    tex->header.version = 0;
    tex->header.ref_count = 0;
    tex->header.pool_index = h.index;
    tex->header.is_loaded = 0;
    tex->header.load_callback = NULL;
    tex->header.load_user_data = NULL;
    tex->flip_y = 1;
    tex->storage_kind = TC_TEXTURE_STORAGE_CPU_FIRST;
    tex->usage = TC_TEXTURE_USAGE_SAMPLED;

    if (name && name[0] != '\0') {
        tex->header.name = tc_intern_string(name);
    }

    if (!tc_resource_map_add(g_texture_uuid_to_index, tex->header.uuid, tc_pack_index(h.index))) {
        tc_log(TC_LOG_ERROR, "tc_texture_declare: failed to add to uuid map");
        tc_pool_free_slot(&g_texture_pool, h);
        return tc_texture_handle_invalid();
    }

    return h;
}

void tc_texture_set_load_callback(
    tc_texture_handle h,
    tc_resource_load_fn callback,
    void* user_data
) {
    tc_texture* tex = tc_texture_get(h);
    if (!tex) return;

    tex->header.load_callback = callback;
    tex->header.load_user_data = user_data;
}

bool tc_texture_is_loaded(tc_texture_handle h) {
    tc_texture* tex = tc_texture_get(h);
    if (!tex) return false;
    return tex->header.is_loaded != 0;
}

bool tc_texture_ensure_loaded(tc_texture_handle h) {
    tc_texture* tex = tc_texture_get(h);
    if (!tex) return false;
    return tc_texture_ensure_loaded_ptr(tex);
}

bool tc_texture_ensure_loaded_ptr(tc_texture* tex) {
    if (!tex) return false;
    if (tex->header.is_loaded) return true;

    if (!tex->header.load_callback) {
        tc_log(TC_LOG_WARN, "tc_texture_ensure_loaded_ptr: texture '%s' has no load callback", tex->header.uuid);
        return false;
    }

    bool success = tex->header.load_callback(tex, tex->header.load_user_data);
    if (success) {
        tex->header.is_loaded = 1;
    } else {
        tc_log(TC_LOG_ERROR, "tc_texture_ensure_loaded_ptr: load callback failed for '%s'", tex->header.uuid);
    }

    return success;
}

tc_texture* tc_texture_get(tc_texture_handle h) {
    if (!g_texture_initialized) return NULL;
    return (tc_texture*)tc_pool_get_checked(&g_texture_pool, h, "tc_texture");
}

bool tc_texture_is_valid(tc_texture_handle h) {
    if (!g_texture_initialized) return false;
    return tc_pool_is_valid(&g_texture_pool, h);
}

bool tc_texture_destroy(tc_texture_handle h) {
    if (!g_texture_initialized) return false;

    tc_texture* tex = tc_texture_get(h);
    if (!tex) return false;

    // Fire destroy-hooks before releasing CPU data / pool slot. Hooks get
    // the pool_index so GPU-side caches (e.g. VulkanRenderDevice's
    // per-device tc_texture cache) can drop their entries and destroy the
    // underlying VkImage / GLuint before the index is recycled.
    const uint32_t pool_index = tex->header.pool_index;
    for (int i = 0; i < g_destroy_hook_count; i++) {
        g_destroy_hooks[i](pool_index, g_destroy_hook_user[i]);
    }

    tc_resource_map_remove(g_texture_uuid_to_index, tex->header.uuid);
    texture_free_data(tex);

    return tc_pool_free_slot(&g_texture_pool, h);
}

void tc_texture_registry_add_destroy_hook(
    tc_texture_destroy_hook_fn cb, void* user_data
) {
    if (!cb) return;
    if (g_destroy_hook_count >= TC_MAX_TEXTURE_DESTROY_HOOKS) {
        tc_log(TC_LOG_ERROR,
               "tc_texture_registry: destroy-hook table full (%d)",
               TC_MAX_TEXTURE_DESTROY_HOOKS);
        return;
    }
    g_destroy_hooks[g_destroy_hook_count] = cb;
    g_destroy_hook_user[g_destroy_hook_count] = user_data;
    g_destroy_hook_count++;
}

void tc_texture_registry_remove_destroy_hook(
    tc_texture_destroy_hook_fn cb, void* user_data
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

bool tc_texture_contains(const char* uuid) {
    if (!g_texture_initialized || !uuid) return false;
    return tc_resource_map_contains(g_texture_uuid_to_index, uuid);
}

size_t tc_texture_count(void) {
    if (!g_texture_initialized) return 0;
    return tc_pool_count(&g_texture_pool);
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
        case TC_TEXTURE_DEPTH24: return 4;
        case TC_TEXTURE_DEPTH32F: return 4;
        case TC_TEXTURE_R16F: return 2;
        case TC_TEXTURE_R32F: return 4;
    }
    return 0;
}

uint8_t tc_texture_format_channels(tc_texture_format format) {
    switch (format) {
        case TC_TEXTURE_RGBA8:
        case TC_TEXTURE_RGBA16F: return 4;
        case TC_TEXTURE_RGB8:
        case TC_TEXTURE_RGB16F: return 3;
        case TC_TEXTURE_RG8: return 2;
        case TC_TEXTURE_R8: return 1;
        case TC_TEXTURE_R16F:
        case TC_TEXTURE_R32F: return 1;
        case TC_TEXTURE_DEPTH24:
        case TC_TEXTURE_DEPTH32F: return 1;
    }
    return 0;
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
    tex->header.is_loaded = 1;
    tex->header.version++;

    if (name) {
        tex->header.name = tc_intern_string(name);
    }
    if (source_path) {
        tex->source_path = tc_intern_string(source_path);
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

void tc_texture_set_storage_kind(tc_texture* tex, tc_texture_storage_kind kind) {
    if (!tex) return;
    if (tex->storage_kind == (uint8_t)kind) return;
    tex->storage_kind = (uint8_t)kind;
    // Bump version: tgfx2 device caches key on (pool_index, version),
    // so changing storage kind after creation must force re-allocation
    // of the backing GPU image with the new flags.
    tex->header.version++;
}

void tc_texture_set_usage(tc_texture* tex, uint32_t usage) {
    if (!tex) return;
    if (tex->usage == usage) return;
    tex->usage = usage;
    // Bump version — usage drives VkImageUsageFlags on Vulkan and the
    // GL_TEXTURE_2D allocation path on GL; cached handles built with
    // the old flags must be invalidated.
    tex->header.version++;
}

void tc_texture_set_size_format(
    tc_texture* tex,
    uint32_t width, uint32_t height,
    tc_texture_format format
) {
    if (!tex) return;
    tex->width = width;
    tex->height = height;
    tex->format = (uint8_t)format;
    tex->channels = tc_texture_format_channels(format);
    tex->header.is_loaded = 1;
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

    const uint32_t dims[3] = { width, height, channels };
    for (size_t i = 0; i < 3; i++) {
        uint32_t value = dims[i];
        for (int byte = 0; byte < 4; byte++) {
            hash ^= (uint8_t)((value >> (byte * 8)) & 0xFFu);
            hash *= 1099511628211ULL;
        }
    }

    if (!data && size > 0) {
        tc_log_error("tc_texture_compute_uuid: data is NULL for non-empty texture data");
        snprintf(uuid_out, 40, "%016llx", (unsigned long long)hash);
        return;
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
    if (!g_texture_initialized || !callback) return;
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
    info->is_loaded = tex->header.is_loaded;
    info->has_load_callback = tex->header.load_callback != NULL;
    info->memory_bytes = (size_t)tex->width * tex->height * tex->channels;

    return true;
}

// ============================================================================
// GPU sync
// ============================================================================

bool tc_texture_sync_to_cpu(tc_texture* tex) {
    if (!tex) return false;

    // CPU-first textures already have data on CPU
    if (tex->storage_kind == TC_TEXTURE_STORAGE_CPU_FIRST) return true;

    static bool warned_gpu_first_readback_removed = false;
    if (!warned_gpu_first_readback_removed) {
        tc_log(TC_LOG_ERROR,
               "tc_texture_sync_to_cpu: GPU-first readback requires a tgfx2 device path");
        warned_gpu_first_readback_removed = true;
    }
    return false;
}

tc_texture_info* tc_texture_get_all_info(size_t* count) {
    if (!count) return NULL;
    *count = 0;

    if (!g_texture_initialized) return NULL;

    size_t tex_count = tc_pool_count(&g_texture_pool);
    if (tex_count == 0) return NULL;

    tc_texture_info* infos = (tc_texture_info*)malloc(tex_count * sizeof(tc_texture_info));
    if (!infos) {
        tc_log(TC_LOG_ERROR, "tc_texture_get_all_info: allocation failed");
        return NULL;
    }

    info_collector collector = { infos, 0 };
    tc_texture_foreach(collect_texture_info, &collector);

    *count = collector.count;
    return infos;
}
