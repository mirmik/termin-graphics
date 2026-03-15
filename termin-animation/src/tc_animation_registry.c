#include "resources/tc_animation_registry.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include <tcbase/tc_log.h>
#include <tcbase/tc_pool.h>
#include <tcbase/tc_resource_map.h>
#include <tcbase/tgfx_intern_string.h>
#include <tgfx/tc_registry_utils.h>

static tc_pool g_animation_pool;
static tc_resource_map* g_uuid_to_index = NULL;
static uint64_t g_next_uuid = 1;
static bool g_initialized = false;

static void animation_free_data(tc_animation* animation) {
    if (!animation) return;
    if (animation->channels) {
        for (size_t i = 0; i < animation->channel_count; i++) {
            tc_animation_channel_free(&animation->channels[i]);
        }
        free(animation->channels);
        animation->channels = NULL;
    }
    animation->channel_count = 0;
    animation->duration = 0.0;
}

void tc_animation_init(void) {
    TC_REGISTRY_INIT_GUARD(g_initialized, "tc_animation");

    if (!tc_pool_init(&g_animation_pool, sizeof(tc_animation), 64)) {
        tc_log_error("tc_animation_init: failed to init pool");
        return;
    }

    g_uuid_to_index = tc_resource_map_new(NULL);
    if (!g_uuid_to_index) {
        tc_log_error("tc_animation_init: failed to create uuid map");
        tc_pool_free(&g_animation_pool);
        return;
    }

    g_next_uuid = 1;
    g_initialized = true;
}

void tc_animation_shutdown(void) {
    TC_REGISTRY_SHUTDOWN_GUARD(g_initialized, "tc_animation");

    for (uint32_t i = 0; i < g_animation_pool.capacity; i++) {
        if (g_animation_pool.states[i] == TC_SLOT_OCCUPIED) {
            tc_animation* animation = (tc_animation*)tc_pool_get_unchecked(&g_animation_pool, i);
            animation_free_data(animation);
        }
    }

    tc_pool_free(&g_animation_pool);
    tc_resource_map_free(g_uuid_to_index);
    g_uuid_to_index = NULL;
    g_next_uuid = 1;
    g_initialized = false;
}

tc_animation_handle tc_animation_create(const char* uuid) {
    if (!g_initialized) {
        tc_animation_init();
    }

    char uuid_buf[TC_UUID_SIZE];
    const char* final_uuid;

    if (uuid && uuid[0] != '\0') {
        if (tc_animation_contains(uuid)) {
            tc_log_warn("tc_animation_create: uuid '%s' already exists", uuid);
            return tc_animation_handle_invalid();
        }
        final_uuid = uuid;
    } else {
        tc_generate_prefixed_uuid(uuid_buf, sizeof(uuid_buf), "anim", &g_next_uuid);
        final_uuid = uuid_buf;
    }

    tc_handle h = tc_pool_alloc(&g_animation_pool);
    if (tc_handle_is_invalid(h)) {
        tc_log_error("tc_animation_create: pool alloc failed");
        return tc_animation_handle_invalid();
    }

    tc_animation* animation = (tc_animation*)tc_pool_get(&g_animation_pool, h);
    memset(animation, 0, sizeof(tc_animation));
    strncpy(animation->header.uuid, final_uuid, sizeof(animation->header.uuid) - 1);
    animation->header.uuid[sizeof(animation->header.uuid) - 1] = '\0';
    animation->header.version = 1;
    animation->header.ref_count = 0;
    animation->header.is_loaded = 1;
    animation->tps = 30.0;
    animation->loop = 1;

    if (!tc_resource_map_add(g_uuid_to_index, animation->header.uuid, tc_pack_index(h.index))) {
        tc_log_error("tc_animation_create: failed to add to uuid map");
        tc_pool_free_slot(&g_animation_pool, h);
        return tc_animation_handle_invalid();
    }

    return h;
}

tc_animation_handle tc_animation_find(const char* uuid) {
    if (!g_initialized || !uuid) {
        return tc_animation_handle_invalid();
    }

    void* ptr = tc_resource_map_get(g_uuid_to_index, uuid);
    if (!tc_has_index(ptr)) {
        return tc_animation_handle_invalid();
    }

    uint32_t index = tc_unpack_index(ptr);
    if (index >= g_animation_pool.capacity) {
        return tc_animation_handle_invalid();
    }

    if (g_animation_pool.states[index] != TC_SLOT_OCCUPIED) {
        return tc_animation_handle_invalid();
    }

    tc_animation_handle h;
    h.index = index;
    h.generation = g_animation_pool.generations[index];
    return h;
}

tc_animation_handle tc_animation_find_by_name(const char* name) {
    if (!g_initialized || !name) {
        return tc_animation_handle_invalid();
    }

    for (uint32_t i = 0; i < g_animation_pool.capacity; i++) {
        if (g_animation_pool.states[i] == TC_SLOT_OCCUPIED) {
            tc_animation* animation = (tc_animation*)tc_pool_get_unchecked(&g_animation_pool, i);
            if (animation->header.name && strcmp(animation->header.name, name) == 0) {
                tc_animation_handle h;
                h.index = i;
                h.generation = g_animation_pool.generations[i];
                return h;
            }
        }
    }

    return tc_animation_handle_invalid();
}

tc_animation_handle tc_animation_get_or_create(const char* uuid) {
    if (!uuid || uuid[0] == '\0') {
        tc_log_warn("tc_animation_get_or_create: empty uuid");
        return tc_animation_handle_invalid();
    }

    tc_animation_handle h = tc_animation_find(uuid);
    if (!tc_animation_handle_is_invalid(h)) {
        return h;
    }

    return tc_animation_create(uuid);
}

tc_animation_handle tc_animation_declare(const char* uuid, const char* name) {
    if (!g_initialized) {
        tc_animation_init();
    }

    tc_animation_handle existing = tc_animation_find(uuid);
    if (!tc_animation_handle_is_invalid(existing)) {
        return existing;
    }

    tc_handle h = tc_pool_alloc(&g_animation_pool);
    if (tc_handle_is_invalid(h)) {
        tc_log_error("tc_animation_declare: pool alloc failed");
        return tc_animation_handle_invalid();
    }

    tc_animation* animation = (tc_animation*)tc_pool_get(&g_animation_pool, h);
    memset(animation, 0, sizeof(tc_animation));
    strncpy(animation->header.uuid, uuid, sizeof(animation->header.uuid) - 1);
    animation->header.uuid[sizeof(animation->header.uuid) - 1] = '\0';
    animation->header.version = 0;
    animation->header.ref_count = 0;
    animation->header.is_loaded = 0;
    animation->tps = 30.0;
    animation->loop = 1;

    if (name && name[0] != '\0') {
        animation->header.name = tgfx_intern_string(name);
    }

    if (!tc_resource_map_add(g_uuid_to_index, animation->header.uuid, tc_pack_index(h.index))) {
        tc_log_error("tc_animation_declare: failed to add to uuid map");
        tc_pool_free_slot(&g_animation_pool, h);
        return tc_animation_handle_invalid();
    }

    return h;
}

tc_animation* tc_animation_get(tc_animation_handle h) {
    if (!g_initialized) return NULL;
    return (tc_animation*)tc_pool_get(&g_animation_pool, h);
}

bool tc_animation_is_valid(tc_animation_handle h) {
    if (!g_initialized) return false;
    return tc_pool_is_valid(&g_animation_pool, h);
}

bool tc_animation_destroy(tc_animation_handle h) {
    if (!g_initialized) return false;

    tc_animation* animation = tc_animation_get(h);
    if (!animation) return false;

    tc_resource_map_remove(g_uuid_to_index, animation->header.uuid);
    animation_free_data(animation);
    return tc_pool_free_slot(&g_animation_pool, h);
}

bool tc_animation_contains(const char* uuid) {
    if (!g_initialized || !uuid) return false;
    return tc_resource_map_contains(g_uuid_to_index, uuid);
}

size_t tc_animation_count(void) {
    if (!g_initialized) return 0;
    return tc_pool_count(&g_animation_pool);
}

bool tc_animation_is_loaded(tc_animation_handle h) {
    tc_animation* animation = tc_animation_get(h);
    if (!animation) return false;
    return animation->header.is_loaded != 0;
}

bool tc_animation_ensure_loaded(tc_animation_handle h) {
    tc_animation* animation = tc_animation_get(h);
    if (!animation) return false;

    if (animation->header.is_loaded) return true;
    if (!animation->header.load_callback) {
        tc_log_warn("tc_animation_ensure_loaded: animation '%s' has no load callback", animation->header.uuid);
        return false;
    }

    bool success = animation->header.load_callback(animation, animation->header.load_user_data);
    if (success) {
        animation->header.is_loaded = 1;
    }
    return success;
}

void tc_animation_add_ref(tc_animation* animation) {
    if (animation) {
        animation->header.ref_count++;
    }
}

bool tc_animation_release(tc_animation* animation) {
    if (!animation || animation->header.ref_count == 0) return false;

    animation->header.ref_count--;
    if (animation->header.ref_count == 0) {
        tc_animation_handle h = tc_animation_find(animation->header.uuid);
        if (!tc_animation_handle_is_invalid(h)) {
            tc_animation_destroy(h);
            return true;
        }
    }
    return false;
}

tc_animation_channel* tc_animation_alloc_channels(tc_animation* anim, size_t count) {
    if (!anim) return NULL;

    animation_free_data(anim);
    if (count == 0) return NULL;

    anim->channels = (tc_animation_channel*)calloc(count, sizeof(tc_animation_channel));
    if (!anim->channels) {
        tc_log_error("tc_animation_alloc_channels: allocation failed");
        return NULL;
    }

    anim->channel_count = count;
    for (size_t i = 0; i < count; i++) {
        tc_animation_channel_init(&anim->channels[i]);
    }

    anim->header.is_loaded = 1;
    anim->header.version++;
    return anim->channels;
}

tc_animation_channel* tc_animation_get_channel(tc_animation* anim, size_t index) {
    if (!anim || index >= anim->channel_count) return NULL;
    return &anim->channels[index];
}

int tc_animation_find_channel(const tc_animation* anim, const char* target_name) {
    if (!anim || !target_name || !anim->channels) return -1;

    for (size_t i = 0; i < anim->channel_count; i++) {
        if (strcmp(anim->channels[i].target_name, target_name) == 0) {
            return (int)i;
        }
    }
    return -1;
}

tc_keyframe_vec3* tc_animation_channel_alloc_translation(tc_animation_channel* ch, size_t count) {
    if (!ch) return NULL;
    if (ch->translation_keys) {
        free(ch->translation_keys);
        ch->translation_keys = NULL;
    }
    ch->translation_count = 0;
    if (count == 0) return NULL;
    ch->translation_keys = (tc_keyframe_vec3*)calloc(count, sizeof(tc_keyframe_vec3));
    if (!ch->translation_keys) return NULL;
    ch->translation_count = count;
    return ch->translation_keys;
}

tc_keyframe_quat* tc_animation_channel_alloc_rotation(tc_animation_channel* ch, size_t count) {
    if (!ch) return NULL;
    if (ch->rotation_keys) {
        free(ch->rotation_keys);
        ch->rotation_keys = NULL;
    }
    ch->rotation_count = 0;
    if (count == 0) return NULL;
    ch->rotation_keys = (tc_keyframe_quat*)calloc(count, sizeof(tc_keyframe_quat));
    if (!ch->rotation_keys) return NULL;
    ch->rotation_count = count;
    return ch->rotation_keys;
}

tc_keyframe_scalar* tc_animation_channel_alloc_scale(tc_animation_channel* ch, size_t count) {
    if (!ch) return NULL;
    if (ch->scale_keys) {
        free(ch->scale_keys);
        ch->scale_keys = NULL;
    }
    ch->scale_count = 0;
    if (count == 0) return NULL;
    ch->scale_keys = (tc_keyframe_scalar*)calloc(count, sizeof(tc_keyframe_scalar));
    if (!ch->scale_keys) return NULL;
    ch->scale_count = count;
    return ch->scale_keys;
}

void tc_animation_recompute_duration(tc_animation* anim) {
    if (!anim) return;

    double max_ticks = 0.0;
    for (size_t i = 0; i < anim->channel_count; i++) {
        if (anim->channels[i].duration > max_ticks) {
            max_ticks = anim->channels[i].duration;
        }
    }
    anim->duration = (anim->tps > 0.0) ? max_ticks / anim->tps : 0.0;
}

typedef struct {
    tc_animation_iter_fn callback;
    void* user_data;
} animation_iter_ctx;

static bool animation_iter_adapter(uint32_t index, void* item, void* ctx_ptr) {
    animation_iter_ctx* ctx = (animation_iter_ctx*)ctx_ptr;
    tc_animation* animation = (tc_animation*)item;

    tc_animation_handle h;
    h.index = index;
    h.generation = g_animation_pool.generations[index];

    return ctx->callback(h, animation, ctx->user_data);
}

void tc_animation_foreach(tc_animation_iter_fn callback, void* user_data) {
    if (!g_initialized || !callback) return;
    animation_iter_ctx ctx = { callback, user_data };
    tc_pool_foreach(&g_animation_pool, animation_iter_adapter, &ctx);
}

static size_t find_keyframe_index_vec3(const tc_keyframe_vec3* keys, size_t count, double t) {
    if (count == 0) return 0;
    if (t <= keys[0].time) return 0;
    if (t >= keys[count - 1].time) return count - 1;

    size_t lo = 0;
    size_t hi = count - 1;
    while (lo + 1 < hi) {
        size_t mid = (lo + hi) / 2;
        if (keys[mid].time <= t) lo = mid;
        else hi = mid;
    }
    return lo;
}

static size_t find_keyframe_index_quat(const tc_keyframe_quat* keys, size_t count, double t) {
    if (count == 0) return 0;
    if (t <= keys[0].time) return 0;
    if (t >= keys[count - 1].time) return count - 1;

    size_t lo = 0;
    size_t hi = count - 1;
    while (lo + 1 < hi) {
        size_t mid = (lo + hi) / 2;
        if (keys[mid].time <= t) lo = mid;
        else hi = mid;
    }
    return lo;
}

static size_t find_keyframe_index_scalar(const tc_keyframe_scalar* keys, size_t count, double t) {
    if (count == 0) return 0;
    if (t <= keys[0].time) return 0;
    if (t >= keys[count - 1].time) return count - 1;

    size_t lo = 0;
    size_t hi = count - 1;
    while (lo + 1 < hi) {
        size_t mid = (lo + hi) / 2;
        if (keys[mid].time <= t) lo = mid;
        else hi = mid;
    }
    return lo;
}

static void quat_slerp(const double* a, const double* b, double t, double* out) {
    double dot = a[0]*b[0] + a[1]*b[1] + a[2]*b[2] + a[3]*b[3];

    double b_adj[4];
    if (dot < 0.0) {
        b_adj[0] = -b[0];
        b_adj[1] = -b[1];
        b_adj[2] = -b[2];
        b_adj[3] = -b[3];
        dot = -dot;
    } else {
        b_adj[0] = b[0];
        b_adj[1] = b[1];
        b_adj[2] = b[2];
        b_adj[3] = b[3];
    }

    if (dot > 0.9995) {
        out[0] = a[0] + t * (b_adj[0] - a[0]);
        out[1] = a[1] + t * (b_adj[1] - a[1]);
        out[2] = a[2] + t * (b_adj[2] - a[2]);
        out[3] = a[3] + t * (b_adj[3] - a[3]);
        double len = sqrt(out[0]*out[0] + out[1]*out[1] + out[2]*out[2] + out[3]*out[3]);
        if (len > 0.0) {
            out[0] /= len;
            out[1] /= len;
            out[2] /= len;
            out[3] /= len;
        }
        return;
    }

    double theta_0 = acos(dot);
    double theta = theta_0 * t;
    double sin_theta = sin(theta);
    double sin_theta_0 = sin(theta_0);

    double s0 = cos(theta) - dot * sin_theta / sin_theta_0;
    double s1 = sin_theta / sin_theta_0;

    out[0] = s0 * a[0] + s1 * b_adj[0];
    out[1] = s0 * a[1] + s1 * b_adj[1];
    out[2] = s0 * a[2] + s1 * b_adj[2];
    out[3] = s0 * a[3] + s1 * b_adj[3];
}

void tc_animation_channel_sample(const tc_animation_channel* ch, double t_ticks, tc_channel_sample* out) {
    tc_channel_sample_init(out);
    if (!ch) return;

    if (ch->translation_keys && ch->translation_count > 0) {
        out->has_translation = 1;
        size_t idx = find_keyframe_index_vec3(ch->translation_keys, ch->translation_count, t_ticks);
        if (idx >= ch->translation_count - 1 || t_ticks <= ch->translation_keys[0].time) {
            const tc_keyframe_vec3* k = &ch->translation_keys[idx];
            out->translation[0] = k->value[0];
            out->translation[1] = k->value[1];
            out->translation[2] = k->value[2];
        } else {
            const tc_keyframe_vec3* k1 = &ch->translation_keys[idx];
            const tc_keyframe_vec3* k2 = &ch->translation_keys[idx + 1];
            double dt = k2->time - k1->time;
            double alpha = (dt > 0.0) ? (t_ticks - k1->time) / dt : 0.0;
            out->translation[0] = k1->value[0] * (1.0 - alpha) + k2->value[0] * alpha;
            out->translation[1] = k1->value[1] * (1.0 - alpha) + k2->value[1] * alpha;
            out->translation[2] = k1->value[2] * (1.0 - alpha) + k2->value[2] * alpha;
        }
    }

    if (ch->rotation_keys && ch->rotation_count > 0) {
        out->has_rotation = 1;
        size_t idx = find_keyframe_index_quat(ch->rotation_keys, ch->rotation_count, t_ticks);
        if (idx >= ch->rotation_count - 1 || t_ticks <= ch->rotation_keys[0].time) {
            const tc_keyframe_quat* k = &ch->rotation_keys[idx];
            out->rotation[0] = k->value[0];
            out->rotation[1] = k->value[1];
            out->rotation[2] = k->value[2];
            out->rotation[3] = k->value[3];
        } else {
            const tc_keyframe_quat* k1 = &ch->rotation_keys[idx];
            const tc_keyframe_quat* k2 = &ch->rotation_keys[idx + 1];
            double dt = k2->time - k1->time;
            double alpha = (dt > 0.0) ? (t_ticks - k1->time) / dt : 0.0;
            quat_slerp(k1->value, k2->value, alpha, out->rotation);
        }
    }

    if (ch->scale_keys && ch->scale_count > 0) {
        out->has_scale = 1;
        size_t idx = find_keyframe_index_scalar(ch->scale_keys, ch->scale_count, t_ticks);
        if (idx >= ch->scale_count - 1 || t_ticks <= ch->scale_keys[0].time) {
            out->scale = ch->scale_keys[idx].value;
        } else {
            const tc_keyframe_scalar* k1 = &ch->scale_keys[idx];
            const tc_keyframe_scalar* k2 = &ch->scale_keys[idx + 1];
            double dt = k2->time - k1->time;
            double alpha = (dt > 0.0) ? (t_ticks - k1->time) / dt : 0.0;
            out->scale = k1->value * (1.0 - alpha) + k2->value * alpha;
        }
    }
}

size_t tc_animation_sample(const tc_animation* anim, double t_seconds, tc_channel_sample* out_samples) {
    if (!anim || !out_samples || anim->channel_count == 0) return 0;

    if (anim->loop && anim->duration > 0.0) {
        t_seconds = fmod(t_seconds, anim->duration);
        if (t_seconds < 0.0) t_seconds += anim->duration;
    }

    double t_ticks = t_seconds * anim->tps;
    for (size_t i = 0; i < anim->channel_count; i++) {
        tc_animation_channel_sample(&anim->channels[i], t_ticks, &out_samples[i]);
    }
    return anim->channel_count;
}

typedef struct {
    tc_animation_info* infos;
    size_t count;
} animation_info_collector;

static bool collect_animation_info(tc_animation_handle h, tc_animation* animation, void* user_data) {
    animation_info_collector* collector = (animation_info_collector*)user_data;

    tc_animation_info* info = &collector->infos[collector->count++];
    info->handle = h;
    strncpy(info->uuid, animation->header.uuid, sizeof(info->uuid) - 1);
    info->uuid[sizeof(info->uuid) - 1] = '\0';
    info->name = animation->header.name;
    info->ref_count = animation->header.ref_count;
    info->version = animation->header.version;
    info->duration = animation->duration;
    info->channel_count = animation->channel_count;
    info->is_loaded = animation->header.is_loaded;
    info->loop = animation->loop;

    return true;
}

tc_animation_info* tc_animation_get_all_info(size_t* count) {
    if (!count) return NULL;
    *count = 0;

    if (!g_initialized) return NULL;

    size_t animation_count = tc_pool_count(&g_animation_pool);
    if (animation_count == 0) return NULL;

    tc_animation_info* infos = (tc_animation_info*)malloc(animation_count * sizeof(tc_animation_info));
    if (!infos) return NULL;

    animation_info_collector collector = { infos, 0 };
    tc_animation_foreach(collect_animation_info, &collector);

    *count = collector.count;
    return infos;
}
