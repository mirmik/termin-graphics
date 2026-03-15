#include "resources/tc_skeleton_registry.h"

#include <stdlib.h>
#include <string.h>

#include <tcbase/tc_log.h>
#include <tcbase/tc_pool.h>
#include <tcbase/tc_resource_map.h>
#include <tcbase/tgfx_intern_string.h>
#include <tgfx/tc_registry_utils.h>

static tc_pool g_skeleton_pool;
static tc_resource_map* g_uuid_to_index = NULL;
static uint64_t g_next_uuid = 1;
static bool g_initialized = false;

static void skeleton_free_data(tc_skeleton* skeleton) {
    if (!skeleton) return;
    if (skeleton->bones) {
        free(skeleton->bones);
        skeleton->bones = NULL;
    }
    if (skeleton->root_indices) {
        free(skeleton->root_indices);
        skeleton->root_indices = NULL;
    }
    skeleton->bone_count = 0;
    skeleton->root_count = 0;
}

void tc_skeleton_init(void) {
    TC_REGISTRY_INIT_GUARD(g_initialized, "tc_skeleton");

    if (!tc_pool_init(&g_skeleton_pool, sizeof(tc_skeleton), 32)) {
        tc_log_error("tc_skeleton_init: failed to init pool");
        return;
    }

    g_uuid_to_index = tc_resource_map_new(NULL);
    if (!g_uuid_to_index) {
        tc_log_error("tc_skeleton_init: failed to create uuid map");
        tc_pool_free(&g_skeleton_pool);
        return;
    }

    g_next_uuid = 1;
    g_initialized = true;
}

void tc_skeleton_shutdown(void) {
    TC_REGISTRY_SHUTDOWN_GUARD(g_initialized, "tc_skeleton");

    for (uint32_t i = 0; i < g_skeleton_pool.capacity; i++) {
        if (g_skeleton_pool.states[i] == TC_SLOT_OCCUPIED) {
            tc_skeleton* skeleton = (tc_skeleton*)tc_pool_get_unchecked(&g_skeleton_pool, i);
            skeleton_free_data(skeleton);
        }
    }

    tc_pool_free(&g_skeleton_pool);
    tc_resource_map_free(g_uuid_to_index);
    g_uuid_to_index = NULL;
    g_next_uuid = 1;
    g_initialized = false;
}

tc_skeleton_handle tc_skeleton_create(const char* uuid) {
    if (!g_initialized) {
        tc_skeleton_init();
    }

    char uuid_buf[TC_UUID_SIZE];
    const char* final_uuid;

    if (uuid && uuid[0] != '\0') {
        if (tc_skeleton_contains(uuid)) {
            tc_log_warn("tc_skeleton_create: uuid '%s' already exists", uuid);
            return tc_skeleton_handle_invalid();
        }
        final_uuid = uuid;
    } else {
        tc_generate_prefixed_uuid(uuid_buf, sizeof(uuid_buf), "skel", &g_next_uuid);
        final_uuid = uuid_buf;
    }

    tc_handle h = tc_pool_alloc(&g_skeleton_pool);
    if (tc_handle_is_invalid(h)) {
        tc_log_error("tc_skeleton_create: pool alloc failed");
        return tc_skeleton_handle_invalid();
    }

    tc_skeleton* skeleton = (tc_skeleton*)tc_pool_get(&g_skeleton_pool, h);
    memset(skeleton, 0, sizeof(tc_skeleton));
    strncpy(skeleton->header.uuid, final_uuid, sizeof(skeleton->header.uuid) - 1);
    skeleton->header.uuid[sizeof(skeleton->header.uuid) - 1] = '\0';
    skeleton->header.version = 1;
    skeleton->header.ref_count = 0;
    skeleton->header.is_loaded = 1;

    if (!tc_resource_map_add(g_uuid_to_index, skeleton->header.uuid, tc_pack_index(h.index))) {
        tc_log_error("tc_skeleton_create: failed to add to uuid map");
        tc_pool_free_slot(&g_skeleton_pool, h);
        return tc_skeleton_handle_invalid();
    }

    return h;
}

tc_skeleton_handle tc_skeleton_find(const char* uuid) {
    if (!g_initialized || !uuid) {
        return tc_skeleton_handle_invalid();
    }

    void* ptr = tc_resource_map_get(g_uuid_to_index, uuid);
    if (!tc_has_index(ptr)) {
        return tc_skeleton_handle_invalid();
    }

    uint32_t index = tc_unpack_index(ptr);
    if (index >= g_skeleton_pool.capacity) {
        return tc_skeleton_handle_invalid();
    }

    if (g_skeleton_pool.states[index] != TC_SLOT_OCCUPIED) {
        return tc_skeleton_handle_invalid();
    }

    tc_skeleton_handle h;
    h.index = index;
    h.generation = g_skeleton_pool.generations[index];
    return h;
}

tc_skeleton_handle tc_skeleton_find_by_name(const char* name) {
    if (!g_initialized || !name) {
        return tc_skeleton_handle_invalid();
    }

    for (uint32_t i = 0; i < g_skeleton_pool.capacity; i++) {
        if (g_skeleton_pool.states[i] == TC_SLOT_OCCUPIED) {
            tc_skeleton* skeleton = (tc_skeleton*)tc_pool_get_unchecked(&g_skeleton_pool, i);
            if (skeleton->header.name && strcmp(skeleton->header.name, name) == 0) {
                tc_skeleton_handle h;
                h.index = i;
                h.generation = g_skeleton_pool.generations[i];
                return h;
            }
        }
    }

    return tc_skeleton_handle_invalid();
}

tc_skeleton_handle tc_skeleton_get_or_create(const char* uuid) {
    if (!uuid || uuid[0] == '\0') {
        tc_log_warn("tc_skeleton_get_or_create: empty uuid");
        return tc_skeleton_handle_invalid();
    }

    tc_skeleton_handle h = tc_skeleton_find(uuid);
    if (!tc_skeleton_handle_is_invalid(h)) {
        return h;
    }

    return tc_skeleton_create(uuid);
}

tc_skeleton_handle tc_skeleton_declare(const char* uuid, const char* name) {
    if (!g_initialized) {
        tc_skeleton_init();
    }

    tc_skeleton_handle existing = tc_skeleton_find(uuid);
    if (!tc_skeleton_handle_is_invalid(existing)) {
        return existing;
    }

    tc_handle h = tc_pool_alloc(&g_skeleton_pool);
    if (tc_handle_is_invalid(h)) {
        tc_log_error("tc_skeleton_declare: pool alloc failed");
        return tc_skeleton_handle_invalid();
    }

    tc_skeleton* skeleton = (tc_skeleton*)tc_pool_get(&g_skeleton_pool, h);
    memset(skeleton, 0, sizeof(tc_skeleton));
    strncpy(skeleton->header.uuid, uuid, sizeof(skeleton->header.uuid) - 1);
    skeleton->header.uuid[sizeof(skeleton->header.uuid) - 1] = '\0';
    skeleton->header.version = 0;
    skeleton->header.ref_count = 0;
    skeleton->header.is_loaded = 0;

    if (name && name[0] != '\0') {
        skeleton->header.name = tgfx_intern_string(name);
    }

    if (!tc_resource_map_add(g_uuid_to_index, skeleton->header.uuid, tc_pack_index(h.index))) {
        tc_log_error("tc_skeleton_declare: failed to add to uuid map");
        tc_pool_free_slot(&g_skeleton_pool, h);
        return tc_skeleton_handle_invalid();
    }

    return h;
}

tc_skeleton* tc_skeleton_get(tc_skeleton_handle h) {
    if (!g_initialized) return NULL;
    return (tc_skeleton*)tc_pool_get(&g_skeleton_pool, h);
}

bool tc_skeleton_is_valid(tc_skeleton_handle h) {
    if (!g_initialized) return false;
    return tc_pool_is_valid(&g_skeleton_pool, h);
}

bool tc_skeleton_destroy(tc_skeleton_handle h) {
    if (!g_initialized) return false;

    tc_skeleton* skeleton = tc_skeleton_get(h);
    if (!skeleton) return false;

    tc_resource_map_remove(g_uuid_to_index, skeleton->header.uuid);
    skeleton_free_data(skeleton);
    return tc_pool_free_slot(&g_skeleton_pool, h);
}

bool tc_skeleton_contains(const char* uuid) {
    if (!g_initialized || !uuid) return false;
    return tc_resource_map_contains(g_uuid_to_index, uuid);
}

size_t tc_skeleton_count(void) {
    if (!g_initialized) return 0;
    return tc_pool_count(&g_skeleton_pool);
}

bool tc_skeleton_is_loaded(tc_skeleton_handle h) {
    tc_skeleton* skeleton = tc_skeleton_get(h);
    if (!skeleton) return false;
    return skeleton->header.is_loaded != 0;
}

void tc_skeleton_set_load_callback(tc_skeleton_handle h, tc_skeleton_load_fn callback, void* user_data) {
    tc_skeleton* skeleton = tc_skeleton_get(h);
    if (!skeleton) return;
    skeleton->header.load_callback = (tc_resource_load_fn)callback;
    skeleton->header.load_user_data = user_data;
}

bool tc_skeleton_ensure_loaded(tc_skeleton_handle h) {
    tc_skeleton* skeleton = tc_skeleton_get(h);
    if (!skeleton) return false;

    if (skeleton->header.is_loaded) return true;
    if (!skeleton->header.load_callback) {
        tc_log_warn("tc_skeleton_ensure_loaded: skeleton '%s' has no load callback", skeleton->header.uuid);
        return false;
    }

    bool success = skeleton->header.load_callback(skeleton, skeleton->header.load_user_data);
    if (success) {
        skeleton->header.is_loaded = 1;
    }
    return success;
}

void tc_skeleton_add_ref(tc_skeleton* skeleton) {
    if (skeleton) {
        skeleton->header.ref_count++;
    }
}

bool tc_skeleton_release(tc_skeleton* skeleton) {
    if (!skeleton || skeleton->header.ref_count == 0) return false;

    skeleton->header.ref_count--;
    if (skeleton->header.ref_count == 0) {
        tc_skeleton_handle h = tc_skeleton_find(skeleton->header.uuid);
        if (!tc_skeleton_handle_is_invalid(h)) {
            tc_skeleton_destroy(h);
            return true;
        }
    }
    return false;
}

tc_bone* tc_skeleton_alloc_bones(tc_skeleton* skeleton, size_t count) {
    if (!skeleton) return NULL;

    if (skeleton->bones) {
        free(skeleton->bones);
        skeleton->bones = NULL;
    }
    if (skeleton->root_indices) {
        free(skeleton->root_indices);
        skeleton->root_indices = NULL;
    }
    skeleton->bone_count = 0;
    skeleton->root_count = 0;

    if (count == 0) return NULL;

    skeleton->bones = (tc_bone*)calloc(count, sizeof(tc_bone));
    if (!skeleton->bones) {
        tc_log_error("tc_skeleton_alloc_bones: allocation failed");
        return NULL;
    }

    skeleton->bone_count = count;
    for (size_t i = 0; i < count; i++) {
        tc_bone_init(&skeleton->bones[i]);
        skeleton->bones[i].index = (int32_t)i;
    }

    skeleton->header.is_loaded = 1;
    skeleton->header.version++;
    return skeleton->bones;
}

tc_bone* tc_skeleton_get_bone(tc_skeleton* skeleton, size_t index) {
    if (!skeleton || index >= skeleton->bone_count) return NULL;
    return &skeleton->bones[index];
}

const tc_bone* tc_skeleton_get_bone_const(const tc_skeleton* skeleton, size_t index) {
    if (!skeleton || index >= skeleton->bone_count) return NULL;
    return &skeleton->bones[index];
}

int tc_skeleton_find_bone(const tc_skeleton* skeleton, const char* name) {
    if (!skeleton || !name || !skeleton->bones) return -1;

    for (size_t i = 0; i < skeleton->bone_count; i++) {
        if (strcmp(skeleton->bones[i].name, name) == 0) {
            return (int)i;
        }
    }
    return -1;
}

void tc_skeleton_rebuild_roots(tc_skeleton* skeleton) {
    if (!skeleton) return;

    if (skeleton->root_indices) {
        free(skeleton->root_indices);
        skeleton->root_indices = NULL;
    }
    skeleton->root_count = 0;

    if (!skeleton->bones || skeleton->bone_count == 0) return;

    size_t root_count = 0;
    for (size_t i = 0; i < skeleton->bone_count; i++) {
        if (tc_bone_is_root(&skeleton->bones[i])) {
            root_count++;
        }
    }
    if (root_count == 0) return;

    skeleton->root_indices = (int32_t*)malloc(root_count * sizeof(int32_t));
    if (!skeleton->root_indices) return;

    size_t idx = 0;
    for (size_t i = 0; i < skeleton->bone_count; i++) {
        if (tc_bone_is_root(&skeleton->bones[i])) {
            skeleton->root_indices[idx++] = (int32_t)i;
        }
    }
    skeleton->root_count = root_count;
}

typedef struct {
    tc_skeleton_iter_fn callback;
    void* user_data;
} skeleton_iter_ctx;

static bool skeleton_iter_adapter(uint32_t index, void* item, void* ctx_ptr) {
    skeleton_iter_ctx* ctx = (skeleton_iter_ctx*)ctx_ptr;
    tc_skeleton* skeleton = (tc_skeleton*)item;

    tc_skeleton_handle h;
    h.index = index;
    h.generation = g_skeleton_pool.generations[index];

    return ctx->callback(h, skeleton, ctx->user_data);
}

void tc_skeleton_foreach(tc_skeleton_iter_fn callback, void* user_data) {
    if (!g_initialized || !callback) return;
    skeleton_iter_ctx ctx = { callback, user_data };
    tc_pool_foreach(&g_skeleton_pool, skeleton_iter_adapter, &ctx);
}

typedef struct {
    tc_skeleton_info* infos;
    size_t count;
} skeleton_info_collector;

static bool collect_skeleton_info(tc_skeleton_handle h, tc_skeleton* skeleton, void* user_data) {
    skeleton_info_collector* collector = (skeleton_info_collector*)user_data;

    tc_skeleton_info* info = &collector->infos[collector->count++];
    info->handle = h;
    strncpy(info->uuid, skeleton->header.uuid, sizeof(info->uuid) - 1);
    info->uuid[sizeof(info->uuid) - 1] = '\0';
    info->name = skeleton->header.name;
    info->ref_count = skeleton->header.ref_count;
    info->version = skeleton->header.version;
    info->bone_count = skeleton->bone_count;
    info->is_loaded = skeleton->header.is_loaded;

    return true;
}

tc_skeleton_info* tc_skeleton_get_all_info(size_t* count) {
    if (!count) return NULL;
    *count = 0;

    if (!g_initialized) return NULL;

    size_t skeleton_count = tc_pool_count(&g_skeleton_pool);
    if (skeleton_count == 0) return NULL;

    tc_skeleton_info* infos = (tc_skeleton_info*)malloc(skeleton_count * sizeof(tc_skeleton_info));
    if (!infos) return NULL;

    skeleton_info_collector collector = { infos, 0 };
    tc_skeleton_foreach(collect_skeleton_info, &collector);

    *count = collector.count;
    return infos;
}
