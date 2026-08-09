// tc_render_target.c - Render target pool implementation
#include "render/tc_render_target.h"
#include "core/tc_component.h"
#include "core/tc_scene.h"
#include "render/tc_render_target_pool.h"
#include "tc_value.h"
#include "tgfx/resources/tc_texture.h"
#include "tgfx/resources/tc_texture_registry.h"
#include <stdlib.h>
#include <string.h>
#include <tcbase/tc_log.h>
#include <tcbase/tc_pool.h>

#define MAX_RENDER_TARGET_POOL_SIZE 128
#define RENDER_TARGET_INITIAL_POOL_CAPACITY 8

typedef struct {
    tc_render_target_kind kind;
    char* name;
    int width;
    int height;
    bool dynamic_resolution;
    tc_texture_format color_format;
    tc_texture_encoding color_encoding;
    tc_texture_format depth_format;
    bool clear_color_enabled;
    tc_linear_color clear_linear_color;
    bool clear_depth_enabled;
    float clear_depth_value;
    tc_scene_handle scene;
    tc_entity_handle camera_entity;
    bool camera_resolution_error_reported;
    tc_entity_handle xr_origin_entity;
    bool xr_origin_resolution_error_reported;
    tc_pipeline_handle pipeline;
    uint64_t layer_mask;
    bool enabled;
    bool locked;
    // Owned color + depth textures, allocated lazily by
    // tc_render_target_ensure_textures(). Invalid handles before the
    // first ensure call — getters return them as-is.
    tc_texture_handle color_texture;
    tc_texture_handle depth_texture;
    // pipeline_params: dict of slot_name → rt_name (NULL if empty)
    tc_value* pipeline_params;
} RenderTargetSlot;

// Default formats for render-target attachments. Matches the choice in
// docs/plans/2026-04-22-render-target-tc-texture-migration.md (Phase 3).
#define RT_DEFAULT_COLOR_FORMAT TC_TEXTURE_RGBA16F
#define RT_DEFAULT_DEPTH_FORMAT TC_TEXTURE_DEPTH32F
#define RT_DEFAULT_COLOR_USAGE                                                                                         \
    (TC_TEXTURE_USAGE_SAMPLED | TC_TEXTURE_USAGE_COLOR_ATTACHMENT | TC_TEXTURE_USAGE_COPY_SRC |                        \
     TC_TEXTURE_USAGE_COPY_DST)
#define RT_DEFAULT_DEPTH_USAGE                                                                                         \
    (TC_TEXTURE_USAGE_SAMPLED | TC_TEXTURE_USAGE_DEPTH_ATTACHMENT | TC_TEXTURE_USAGE_COPY_SRC |                        \
     TC_TEXTURE_USAGE_COPY_DST)

static tc_pool g_render_target_pool;
static tc_pool_generation_epoch g_render_target_generation_epoch;
static bool g_render_target_pool_initialized = false;

#define RT_SLOTS ((RenderTargetSlot*)g_render_target_pool.data)

static bool rt_dimensions_valid(int width, int height, const char* operation) {
    if (width > 0 && height > 0 && width <= TC_RENDER_TARGET_MAX_DIMENSION &&
        height <= TC_RENDER_TARGET_MAX_DIMENSION) {
        return true;
    }

    tc_log_error("[tc_render_target] rejected %s dimensions %dx%d; expected 1..%d",
                 operation,
                 width,
                 height,
                 TC_RENDER_TARGET_MAX_DIMENSION);
    return false;
}

static char* rt_strdup(const char* s) {
    if (s == NULL)
        return NULL;
    size_t len = strlen(s) + 1;
    char* copy = (char*)malloc(len);
    if (copy)
        memcpy(copy, s, len);
    return copy;
}

static void rt_strset(char** dest, const char* src) {
    free(*dest);
    *dest = rt_strdup(src);
}

static void rt_slot_set_defaults(RenderTargetSlot* slot) {
    if (!slot)
        return;
    memset(slot, 0, sizeof(*slot));
    slot->kind = TC_RENDER_TARGET_TEXTURE_2D;
    slot->width = 512;
    slot->height = 512;
    slot->color_format = RT_DEFAULT_COLOR_FORMAT;
    slot->color_encoding = TC_TEXTURE_ENCODING_LINEAR;
    slot->depth_format = RT_DEFAULT_DEPTH_FORMAT;
    slot->clear_linear_color.a = 1.0f;
    slot->clear_depth_value = 1.0f;
    slot->scene = TC_SCENE_HANDLE_INVALID;
    slot->camera_entity = TC_ENTITY_HANDLE_INVALID;
    slot->xr_origin_entity = TC_ENTITY_HANDLE_INVALID;
    slot->pipeline = TC_PIPELINE_HANDLE_INVALID;
    slot->layer_mask = 0xFFFFFFFFFFFFFFFFULL;
    slot->enabled = true;
    slot->color_texture = tc_texture_handle_invalid();
    slot->depth_texture = tc_texture_handle_invalid();
}

static void rt_slot_release_owned(RenderTargetSlot* slot) {
    if (!slot)
        return;
    free(slot->name);
    slot->name = NULL;
    if (slot->pipeline_params) {
        tc_value_free(slot->pipeline_params);
        free(slot->pipeline_params);
        slot->pipeline_params = NULL;
    }
    if (!tc_texture_handle_is_invalid(slot->color_texture)) {
        tc_texture_destroy(slot->color_texture);
        slot->color_texture = tc_texture_handle_invalid();
    }
    if (!tc_texture_handle_is_invalid(slot->depth_texture)) {
        tc_texture_destroy(slot->depth_texture);
        slot->depth_texture = tc_texture_handle_invalid();
    }
}

void tc_render_target_pool_init(void) {
    if (g_render_target_pool_initialized) {
        tc_log_warn("[tc_render_target_pool] already initialized");
        return;
    }
    const tc_pool_config config = {
        .max_capacity = MAX_RENDER_TARGET_POOL_SIZE,
        .initial_generation = 0u,
        .allocate_low_indices_first = true,
        .name = "tc_render_target_pool",
        .generation_epoch = &g_render_target_generation_epoch,
    };
    if (!tc_pool_init_ex(
            &g_render_target_pool, sizeof(RenderTargetSlot), RENDER_TARGET_INITIAL_POOL_CAPACITY, &config)) {
        tc_log_error("[tc_render_target_pool] slot allocation failed");
        return;
    }

    for (uint32_t i = 0; i < g_render_target_pool.capacity; ++i) {
        rt_slot_set_defaults(&RT_SLOTS[i]);
    }
    g_render_target_pool_initialized = true;
}

void tc_render_target_pool_shutdown(void) {
    if (!g_render_target_pool_initialized) {
        tc_log_warn("[tc_render_target_pool] not initialized");
        return;
    }

    for (uint32_t i = 0; i < g_render_target_pool.capacity; ++i) {
        if (g_render_target_pool.states[i] == TC_SLOT_OCCUPIED) {
            rt_slot_release_owned(&RT_SLOTS[i]);
        }
    }

    tc_pool_free(&g_render_target_pool);
    g_render_target_pool_initialized = false;
}

static inline bool render_target_handle_alive(tc_render_target_handle h) {
    const tc_handle pool_handle = {h.index, h.generation};
    return g_render_target_pool_initialized && tc_pool_is_valid(&g_render_target_pool, pool_handle);
}

static bool rt_format_is_depth(tc_texture_format format) {
    return format == TC_TEXTURE_DEPTH24 || format == TC_TEXTURE_DEPTH32F;
}

static bool rt_color_contract_valid(tc_texture_format format, tc_texture_encoding encoding) {
    if (encoding == TC_TEXTURE_ENCODING_LINEAR)
        return !rt_format_is_depth(format);
    if (encoding == TC_TEXTURE_ENCODING_SRGB)
        return format == TC_TEXTURE_RGBA8;
    return false;
}

bool tc_render_target_format_from_string(const char* name, tc_texture_format* out_format) {
    if (!name || !out_format)
        return false;
    if (strcmp(name, "rgba8") == 0) {
        *out_format = TC_TEXTURE_RGBA8;
        return true;
    }
    if (strcmp(name, "rgb8") == 0) {
        *out_format = TC_TEXTURE_RGB8;
        return true;
    }
    if (strcmp(name, "rg8") == 0) {
        *out_format = TC_TEXTURE_RG8;
        return true;
    }
    if (strcmp(name, "r8") == 0) {
        *out_format = TC_TEXTURE_R8;
        return true;
    }
    if (strcmp(name, "rgba16f") == 0) {
        *out_format = TC_TEXTURE_RGBA16F;
        return true;
    }
    if (strcmp(name, "rgb16f") == 0) {
        *out_format = TC_TEXTURE_RGB16F;
        return true;
    }
    if (strcmp(name, "r16f") == 0) {
        *out_format = TC_TEXTURE_R16F;
        return true;
    }
    if (strcmp(name, "r32f") == 0) {
        *out_format = TC_TEXTURE_R32F;
        return true;
    }
    if (strcmp(name, "depth24") == 0) {
        *out_format = TC_TEXTURE_DEPTH24;
        return true;
    }
    if (strcmp(name, "depth32f") == 0) {
        *out_format = TC_TEXTURE_DEPTH32F;
        return true;
    }
    return false;
}

const char* tc_render_target_format_to_string(tc_texture_format format) {
    switch (format) {
    case TC_TEXTURE_RGBA8:
        return "rgba8";
    case TC_TEXTURE_RGB8:
        return "rgb8";
    case TC_TEXTURE_RG8:
        return "rg8";
    case TC_TEXTURE_R8:
        return "r8";
    case TC_TEXTURE_RGBA16F:
        return "rgba16f";
    case TC_TEXTURE_RGB16F:
        return "rgb16f";
    case TC_TEXTURE_R16F:
        return "r16f";
    case TC_TEXTURE_R32F:
        return "r32f";
    case TC_TEXTURE_DEPTH24:
        return "depth24";
    case TC_TEXTURE_DEPTH32F:
        return "depth32f";
    }
    return "unknown";
}

bool tc_render_target_pool_alive(tc_render_target_handle h) {
    return render_target_handle_alive(h);
}

bool tc_render_target_alive(tc_render_target_handle h) {
    return render_target_handle_alive(h);
}

bool tc_render_target_kind_from_string(const char* name, tc_render_target_kind* out_kind) {
    if (!name || !out_kind)
        return false;
    if (strcmp(name, "texture_2d") == 0) {
        *out_kind = TC_RENDER_TARGET_TEXTURE_2D;
        return true;
    }
    if (strcmp(name, "xr_stereo") == 0) {
        *out_kind = TC_RENDER_TARGET_XR_STEREO;
        return true;
    }
    return false;
}

const char* tc_render_target_kind_to_string(tc_render_target_kind kind) {
    switch (kind) {
    case TC_RENDER_TARGET_TEXTURE_2D:
        return "texture_2d";
    case TC_RENDER_TARGET_XR_STEREO:
        return "xr_stereo";
    }
    return "texture_2d";
}

tc_render_target_handle tc_render_target_pool_alloc(const char* name) {
    if (!g_render_target_pool_initialized) {
        tc_render_target_pool_init();
    }
    if (!g_render_target_pool_initialized) {
        return TC_RENDER_TARGET_HANDLE_INVALID;
    }

    tc_handle pool_handle = tc_pool_alloc(&g_render_target_pool);
    if (tc_handle_is_invalid(pool_handle)) {
        tc_log_error("[tc_render_target_pool] no free slots");
        return TC_RENDER_TARGET_HANDLE_INVALID;
    }

    RenderTargetSlot* slot = &RT_SLOTS[pool_handle.index];
    rt_slot_set_defaults(slot);
    slot->name = rt_strdup(name);

    tc_render_target_handle h = {
        pool_handle.index,
        pool_handle.generation,
    };
    return h;
}

void tc_render_target_pool_free(tc_render_target_handle h) {
    if (!render_target_handle_alive(h))
        return;
    if (RT_SLOTS[h.index].locked) {
        tc_log_error("[tc_render_target] attempt to free locked render target '%s' "
                     "(index=%u, gen=%u) — the owning viewport must unlock it first",
                     RT_SLOTS[h.index].name ? RT_SLOTS[h.index].name : "(unnamed)",
                     h.index,
                     h.generation);
        return;
    }

    RenderTargetSlot* slot = &RT_SLOTS[h.index];
    rt_slot_release_owned(slot);
    rt_slot_set_defaults(slot);
    const tc_handle pool_handle = {h.index, h.generation};
    tc_pool_free_slot(&g_render_target_pool, pool_handle);
}

void tc_render_target_pool_foreach(tc_render_target_pool_iter_fn callback, void* user_data) {
    if (!g_render_target_pool_initialized || !callback)
        return;
    for (uint32_t i = 0; i < g_render_target_pool.capacity; ++i) {
        if (g_render_target_pool.states[i] == TC_SLOT_OCCUPIED) {
            tc_render_target_handle h = {
                i,
                g_render_target_pool.generations[i],
            };
            if (!callback(h, user_data)) {
                break;
            }
        }
    }
}

size_t tc_render_target_pool_count(void) {
    return g_render_target_pool_initialized ? tc_pool_count(&g_render_target_pool) : 0u;
}

tc_render_target_handle tc_render_target_new(const char* name) {
    return tc_render_target_pool_alloc(name);
}

void tc_render_target_free(tc_render_target_handle h) {
    tc_render_target_pool_free(h);
}

void tc_render_target_set_kind(tc_render_target_handle h, tc_render_target_kind kind) {
    if (!render_target_handle_alive(h))
        return;
    if (kind != TC_RENDER_TARGET_TEXTURE_2D && kind != TC_RENDER_TARGET_XR_STEREO) {
        tc_log_error("[tc_render_target] rejected unknown render target kind: %d", (int)kind);
        return;
    }
    if (RT_SLOTS[h.index].kind == kind)
        return;
    RT_SLOTS[h.index].kind = kind;
    if (kind == TC_RENDER_TARGET_XR_STEREO) {
        if (!tc_texture_handle_is_invalid(RT_SLOTS[h.index].color_texture)) {
            tc_texture_destroy(RT_SLOTS[h.index].color_texture);
            RT_SLOTS[h.index].color_texture = tc_texture_handle_invalid();
        }
        if (!tc_texture_handle_is_invalid(RT_SLOTS[h.index].depth_texture)) {
            tc_texture_destroy(RT_SLOTS[h.index].depth_texture);
            RT_SLOTS[h.index].depth_texture = tc_texture_handle_invalid();
        }
    }
}

tc_render_target_kind tc_render_target_get_kind(tc_render_target_handle h) {
    if (!render_target_handle_alive(h))
        return TC_RENDER_TARGET_TEXTURE_2D;
    return RT_SLOTS[h.index].kind;
}

void tc_render_target_set_name(tc_render_target_handle h, const char* name) {
    if (!render_target_handle_alive(h))
        return;
    rt_strset(&RT_SLOTS[h.index].name, name);
}

const char* tc_render_target_get_name(tc_render_target_handle h) {
    if (!render_target_handle_alive(h))
        return NULL;
    return RT_SLOTS[h.index].name;
}

// Resize an already-allocated tc_texture in place. Bumps `header.version`
// so the bridge re-creates the GPU image on the next wrap call. No-op if
// the handle is invalid (textures haven't been ensured yet).
static void rt_resize_owned_texture(tc_texture_handle h, uint32_t width, uint32_t height) {
    if (tc_texture_handle_is_invalid(h))
        return;
    tc_texture* tex = tc_texture_get(h);
    if (!tex)
        return;
    tc_texture_set_size_format(tex, width, height, (tc_texture_format)tex->format);
}

static void rt_reformat_owned_texture(tc_texture_handle h, uint32_t width, uint32_t height, tc_texture_format format) {
    if (tc_texture_handle_is_invalid(h))
        return;
    tc_texture* tex = tc_texture_get(h);
    if (!tex)
        return;
    tc_texture_set_size_format(tex, width, height, format);
}

void tc_render_target_set_width(tc_render_target_handle h, int width) {
    if (!render_target_handle_alive(h))
        return;
    const int height = RT_SLOTS[h.index].height;
    if (!rt_dimensions_valid(width, height, "width update"))
        return;
    if (RT_SLOTS[h.index].width == width)
        return; // No-op on same size — skips
                // a needless version bump that
                // would force a per-frame
                // GPU image re-create.
    RT_SLOTS[h.index].width = width;
    rt_resize_owned_texture(RT_SLOTS[h.index].color_texture, (uint32_t)width, (uint32_t)RT_SLOTS[h.index].height);
    rt_resize_owned_texture(RT_SLOTS[h.index].depth_texture, (uint32_t)width, (uint32_t)RT_SLOTS[h.index].height);
}

int tc_render_target_get_width(tc_render_target_handle h) {
    if (!render_target_handle_alive(h))
        return 0;
    return RT_SLOTS[h.index].width;
}

void tc_render_target_set_height(tc_render_target_handle h, int height) {
    if (!render_target_handle_alive(h))
        return;
    const int width = RT_SLOTS[h.index].width;
    if (!rt_dimensions_valid(width, height, "height update"))
        return;
    if (RT_SLOTS[h.index].height == height)
        return; // No-op on same size.
    RT_SLOTS[h.index].height = height;
    rt_resize_owned_texture(RT_SLOTS[h.index].color_texture, (uint32_t)RT_SLOTS[h.index].width, (uint32_t)height);
    rt_resize_owned_texture(RT_SLOTS[h.index].depth_texture, (uint32_t)RT_SLOTS[h.index].width, (uint32_t)height);
}

int tc_render_target_get_height(tc_render_target_handle h) {
    if (!render_target_handle_alive(h))
        return 0;
    return RT_SLOTS[h.index].height;
}

void tc_render_target_set_dynamic_resolution(tc_render_target_handle h, bool dynamic_resolution) {
    if (!render_target_handle_alive(h))
        return;
    RT_SLOTS[h.index].dynamic_resolution = dynamic_resolution;
}

bool tc_render_target_get_dynamic_resolution(tc_render_target_handle h) {
    if (!render_target_handle_alive(h))
        return false;
    return RT_SLOTS[h.index].dynamic_resolution;
}

void tc_render_target_set_color_format(tc_render_target_handle h, tc_texture_format format) {
    if (!render_target_handle_alive(h))
        return;
    if (rt_format_is_depth(format)) {
        tc_log_error("[tc_render_target] rejected depth format '%s' for color attachment",
                     tc_render_target_format_to_string(format));
        return;
    }
    if (!rt_color_contract_valid(format, RT_SLOTS[h.index].color_encoding)) {
        tc_log_error("[tc_render_target] rejected color format '%s' for %s encoding",
                     tc_render_target_format_to_string(format),
                     RT_SLOTS[h.index].color_encoding == TC_TEXTURE_ENCODING_SRGB ? "sRGB" : "unknown");
        return;
    }
    if (RT_SLOTS[h.index].color_format == format)
        return;
    RT_SLOTS[h.index].color_format = format;
    rt_reformat_owned_texture(
        RT_SLOTS[h.index].color_texture, (uint32_t)RT_SLOTS[h.index].width, (uint32_t)RT_SLOTS[h.index].height, format);
}

void tc_render_target_set_color_encoding(tc_render_target_handle h, tc_texture_encoding encoding) {
    if (!render_target_handle_alive(h))
        return;
    RenderTargetSlot* slot = &RT_SLOTS[h.index];
    if (!rt_color_contract_valid(slot->color_format, encoding)) {
        tc_log_error("[tc_render_target] rejected color encoding %d for format '%s'",
                     (int)encoding,
                     tc_render_target_format_to_string(slot->color_format));
        return;
    }
    if (slot->color_encoding == encoding)
        return;
    slot->color_encoding = encoding;
    if (!tc_texture_handle_is_invalid(slot->color_texture)) {
        tc_texture* texture = tc_texture_get(slot->color_texture);
        if (texture && !tc_texture_set_encoding(texture, encoding)) {
            tc_log_error("[tc_render_target] failed to apply color encoding to owned texture");
        }
    }
}

tc_texture_encoding tc_render_target_get_color_encoding(tc_render_target_handle h) {
    if (!render_target_handle_alive(h))
        return TC_TEXTURE_ENCODING_LINEAR;
    return RT_SLOTS[h.index].color_encoding;
}

bool tc_render_target_color_encoding_from_string(const char* name, tc_texture_encoding* out_encoding) {
    if (!name || !out_encoding)
        return false;
    if (strcmp(name, "linear") == 0) {
        *out_encoding = TC_TEXTURE_ENCODING_LINEAR;
        return true;
    }
    if (strcmp(name, "srgb") == 0) {
        *out_encoding = TC_TEXTURE_ENCODING_SRGB;
        return true;
    }
    return false;
}

const char* tc_render_target_color_encoding_to_string(tc_texture_encoding encoding) {
    switch (encoding) {
    case TC_TEXTURE_ENCODING_LINEAR:
        return "linear";
    case TC_TEXTURE_ENCODING_SRGB:
        return "srgb";
    }
    return "unknown";
}

tc_texture_format tc_render_target_get_color_format(tc_render_target_handle h) {
    if (!render_target_handle_alive(h))
        return RT_DEFAULT_COLOR_FORMAT;
    return RT_SLOTS[h.index].color_format;
}

void tc_render_target_set_depth_format(tc_render_target_handle h, tc_texture_format format) {
    if (!render_target_handle_alive(h))
        return;
    if (!rt_format_is_depth(format)) {
        tc_log_error("[tc_render_target] rejected color format '%s' for depth attachment",
                     tc_render_target_format_to_string(format));
        return;
    }
    if (RT_SLOTS[h.index].depth_format == format)
        return;
    RT_SLOTS[h.index].depth_format = format;
    rt_reformat_owned_texture(
        RT_SLOTS[h.index].depth_texture, (uint32_t)RT_SLOTS[h.index].width, (uint32_t)RT_SLOTS[h.index].height, format);
}

tc_texture_format tc_render_target_get_depth_format(tc_render_target_handle h) {
    if (!render_target_handle_alive(h))
        return RT_DEFAULT_DEPTH_FORMAT;
    return RT_SLOTS[h.index].depth_format;
}

void tc_render_target_set_clear_color_enabled(tc_render_target_handle h, bool enabled) {
    if (!render_target_handle_alive(h))
        return;
    RT_SLOTS[h.index].clear_color_enabled = enabled;
}

bool tc_render_target_get_clear_color_enabled(tc_render_target_handle h) {
    if (!render_target_handle_alive(h))
        return false;
    return RT_SLOTS[h.index].clear_color_enabled;
}

void tc_render_target_set_clear_linear_color(tc_render_target_handle h, tc_linear_color value) {
    if (!render_target_handle_alive(h))
        return;
    RT_SLOTS[h.index].clear_linear_color = value;
}

void tc_render_target_get_clear_linear_color(tc_render_target_handle h, tc_linear_color* out_value) {
    if (!out_value)
        return;
    *out_value = (tc_linear_color){0.0f, 0.0f, 0.0f, 1.0f};
    if (!render_target_handle_alive(h))
        return;
    *out_value = RT_SLOTS[h.index].clear_linear_color;
}

void tc_render_target_set_clear_depth_enabled(tc_render_target_handle h, bool enabled) {
    if (!render_target_handle_alive(h))
        return;
    RT_SLOTS[h.index].clear_depth_enabled = enabled;
}

bool tc_render_target_get_clear_depth_enabled(tc_render_target_handle h) {
    if (!render_target_handle_alive(h))
        return false;
    return RT_SLOTS[h.index].clear_depth_enabled;
}

void tc_render_target_set_clear_depth_value(tc_render_target_handle h, float value) {
    if (!render_target_handle_alive(h))
        return;
    RT_SLOTS[h.index].clear_depth_value = value;
}

float tc_render_target_get_clear_depth_value(tc_render_target_handle h) {
    if (!render_target_handle_alive(h))
        return 1.0f;
    return RT_SLOTS[h.index].clear_depth_value;
}

// --- Owned textures --------------------------------------------------------

void tc_render_target_ensure_textures(tc_render_target_handle h) {
    if (!render_target_handle_alive(h))
        return;
    uint32_t idx = h.index;
    if (RT_SLOTS[idx].kind != TC_RENDER_TARGET_TEXTURE_2D) {
        tc_log_warn("[tc_render_target] ensure_textures skipped for non-texture render target '%s'",
                    RT_SLOTS[idx].name ? RT_SLOTS[idx].name : "(unnamed)");
        return;
    }

    if (!rt_dimensions_valid(RT_SLOTS[idx].width, RT_SLOTS[idx].height, "texture allocation")) {
        return;
    }

    const uint32_t w = (uint32_t)RT_SLOTS[idx].width;
    const uint32_t height_ = (uint32_t)RT_SLOTS[idx].height;
    const tc_texture_format color_format = RT_SLOTS[idx].color_format;
    const tc_texture_encoding color_encoding = RT_SLOTS[idx].color_encoding;
    const tc_texture_format depth_format = RT_SLOTS[idx].depth_format;

    // Owned through tc_texture_destroy on pool_free — no add_ref dance,
    // the render target is the single owner. External code that wants
    // longer-than-RT lifetime of these handles must take its own ref.
    if (tc_texture_handle_is_invalid(RT_SLOTS[idx].color_texture)) {
        tc_texture_handle ch = tc_texture_create(NULL);
        tc_texture* tex = tc_texture_get(ch);
        if (tex) {
            tc_texture_set_storage_kind(tex, TC_TEXTURE_STORAGE_GPU_FIRST);
            tc_texture_set_usage(tex, RT_DEFAULT_COLOR_USAGE);
            if (!tc_texture_set_encoding(tex, color_encoding)) {
                tc_log_error("[tc_render_target] failed to initialize color texture encoding");
            }
            tc_texture_set_size_format(tex, w, height_, color_format);
            RT_SLOTS[idx].color_texture = ch;
        } else {
            tc_log_error("[tc_render_target] failed to create color texture");
        }
    } else {
        tc_texture* tex = tc_texture_get(RT_SLOTS[idx].color_texture);
        if (tex && (tex->width != w || tex->height != height_ || tex->format != color_format ||
                    tex->encoding != color_encoding)) {
            tc_texture_set_storage_kind(tex, TC_TEXTURE_STORAGE_GPU_FIRST);
            tc_texture_set_usage(tex, RT_DEFAULT_COLOR_USAGE);
            if (!tc_texture_set_encoding(tex, color_encoding)) {
                tc_log_error("[tc_render_target] failed to synchronize color texture encoding");
            }
            tc_texture_set_size_format(tex, w, height_, color_format);
        }
    }

    if (tc_texture_handle_is_invalid(RT_SLOTS[idx].depth_texture)) {
        tc_texture_handle dh = tc_texture_create(NULL);
        tc_texture* tex = tc_texture_get(dh);
        if (tex) {
            tc_texture_set_storage_kind(tex, TC_TEXTURE_STORAGE_GPU_FIRST);
            tc_texture_set_usage(tex, RT_DEFAULT_DEPTH_USAGE);
            tc_texture_set_size_format(tex, w, height_, depth_format);
            RT_SLOTS[idx].depth_texture = dh;
        } else {
            tc_log_error("[tc_render_target] failed to create depth texture");
        }
    } else {
        tc_texture* tex = tc_texture_get(RT_SLOTS[idx].depth_texture);
        if (tex && (tex->width != w || tex->height != height_ || tex->format != depth_format)) {
            tc_texture_set_storage_kind(tex, TC_TEXTURE_STORAGE_GPU_FIRST);
            tc_texture_set_usage(tex, RT_DEFAULT_DEPTH_USAGE);
            tc_texture_set_size_format(tex, w, height_, depth_format);
        }
    }
}

tc_texture_handle tc_render_target_get_color_texture(tc_render_target_handle h) {
    if (!render_target_handle_alive(h))
        return tc_texture_handle_invalid();
    return RT_SLOTS[h.index].color_texture;
}

tc_texture_handle tc_render_target_get_depth_texture(tc_render_target_handle h) {
    if (!render_target_handle_alive(h))
        return tc_texture_handle_invalid();
    return RT_SLOTS[h.index].depth_texture;
}

void tc_render_target_set_scene(tc_render_target_handle h, tc_scene_handle scene) {
    if (!render_target_handle_alive(h))
        return;
    RenderTargetSlot* slot = &RT_SLOTS[h.index];
    if (!tc_scene_handle_eq(slot->scene, scene)) {
        slot->camera_entity = TC_ENTITY_HANDLE_INVALID;
        slot->camera_resolution_error_reported = false;
        slot->xr_origin_entity = TC_ENTITY_HANDLE_INVALID;
        slot->xr_origin_resolution_error_reported = false;
    }
    slot->scene = scene;
}

tc_scene_handle tc_render_target_get_scene(tc_render_target_handle h) {
    if (!render_target_handle_alive(h))
        return TC_SCENE_HANDLE_INVALID;
    return RT_SLOTS[h.index].scene;
}

static bool rt_entity_binding_is_set(tc_entity_handle entity) {
    return tc_entity_pool_handle_valid(entity.pool) && tc_entity_id_valid(entity.id);
}

static tc_component* rt_resolve_component(RenderTargetSlot* slot,
                                          tc_entity_handle entity,
                                          const char* expected_type,
                                          const char* role,
                                          bool* error_reported) {
    if (!slot || !rt_entity_binding_is_set(entity))
        return NULL;

    const char* target_name = slot->name ? slot->name : "(unnamed)";
    if (!tc_scene_alive(slot->scene)) {
        if (!*error_reported) {
            tc_log_error("[tc_render_target] target '%s' cannot resolve %s: scene handle is stale", target_name, role);
            *error_reported = true;
        }
        return NULL;
    }
    if (!tc_entity_handle_valid(entity)) {
        if (!*error_reported) {
            tc_log_error("[tc_render_target] target '%s' cannot resolve %s: entity handle is stale", target_name, role);
            *error_reported = true;
        }
        return NULL;
    }

    tc_entity_pool* pool = tc_entity_pool_registry_get(entity.pool);
    tc_scene_handle owner_scene = tc_entity_pool_get_scene(pool);
    if (!pool || (tc_scene_handle_valid(owner_scene) && !tc_scene_handle_eq(owner_scene, slot->scene))) {
        if (!*error_reported) {
            tc_log_error(
                "[tc_render_target] target '%s' cannot resolve %s: entity belongs to another scene", target_name, role);
            *error_reported = true;
        }
        return NULL;
    }

    const size_t component_count = tc_entity_pool_component_count(pool, entity.id);
    for (size_t index = 0; index < component_count; ++index) {
        tc_component* component = tc_entity_pool_component_at(pool, entity.id, index);
        if (!component)
            continue;
        const char* type_name = tc_component_type_name(component);
        if (tc_component_registry_is_a(type_name, expected_type)) {
            *error_reported = false;
            return component;
        }
    }

    if (!*error_reported) {
        tc_log_error(
            "[tc_render_target] target '%s' cannot resolve %s: entity has no %s", target_name, role, expected_type);
        *error_reported = true;
    }
    return NULL;
}

static bool rt_validate_component_binding(const RenderTargetSlot* slot,
                                          tc_component* component,
                                          const char* expected_type,
                                          const char* role) {
    if (!slot || !component)
        return false;
    const char* target_name = slot->name ? slot->name : "(unnamed)";
    const char* type_name = tc_component_type_name(component);
    if (!tc_component_registry_is_a(type_name, expected_type)) {
        tc_log_error("[tc_render_target] target '%s' rejected %s component of type '%s'; expected %s",
                     target_name,
                     role,
                     type_name ? type_name : "(unknown)",
                     expected_type);
        return false;
    }
    if (!tc_scene_alive(slot->scene)) {
        tc_log_error("[tc_render_target] target '%s' rejected %s: scene handle is invalid", target_name, role);
        return false;
    }
    if (!tc_entity_handle_valid(component->owner)) {
        tc_log_error("[tc_render_target] target '%s' rejected %s: component owner is stale", target_name, role);
        return false;
    }
    tc_entity_pool* pool = tc_entity_pool_registry_get(component->owner.pool);
    tc_scene_handle owner_scene = tc_entity_pool_get_scene(pool);
    if (!pool || (tc_scene_handle_valid(owner_scene) && !tc_scene_handle_eq(owner_scene, slot->scene))) {
        tc_log_error(
            "[tc_render_target] target '%s' rejected %s: component belongs to another scene", target_name, role);
        return false;
    }
    return true;
}

void tc_render_target_set_camera(tc_render_target_handle h, tc_component* camera) {
    if (!render_target_handle_alive(h))
        return;
    RenderTargetSlot* slot = &RT_SLOTS[h.index];
    if (!camera) {
        slot->camera_entity = TC_ENTITY_HANDLE_INVALID;
        slot->camera_resolution_error_reported = false;
        return;
    }
    if (rt_validate_component_binding(slot, camera, "CameraComponent", "camera")) {
        slot->camera_entity = camera->owner;
        slot->camera_resolution_error_reported = false;
    }
}

tc_component* tc_render_target_get_camera(tc_render_target_handle h) {
    if (!render_target_handle_alive(h))
        return NULL;
    RenderTargetSlot* slot = &RT_SLOTS[h.index];
    return rt_resolve_component(
        slot, slot->camera_entity, "CameraComponent", "camera", &slot->camera_resolution_error_reported);
}

tc_entity_handle tc_render_target_get_camera_entity(tc_render_target_handle h) {
    if (!render_target_handle_alive(h))
        return TC_ENTITY_HANDLE_INVALID;
    return RT_SLOTS[h.index].camera_entity;
}

void tc_render_target_set_xr_origin(tc_render_target_handle h, tc_component* xr_origin) {
    if (!render_target_handle_alive(h))
        return;
    RenderTargetSlot* slot = &RT_SLOTS[h.index];
    if (!xr_origin) {
        slot->xr_origin_entity = TC_ENTITY_HANDLE_INVALID;
        slot->xr_origin_resolution_error_reported = false;
        return;
    }
    if (rt_validate_component_binding(slot, xr_origin, "XrOriginComponent", "XR origin")) {
        slot->xr_origin_entity = xr_origin->owner;
        slot->xr_origin_resolution_error_reported = false;
    }
}

tc_component* tc_render_target_get_xr_origin(tc_render_target_handle h) {
    if (!render_target_handle_alive(h))
        return NULL;
    RenderTargetSlot* slot = &RT_SLOTS[h.index];
    return rt_resolve_component(
        slot, slot->xr_origin_entity, "XrOriginComponent", "XR origin", &slot->xr_origin_resolution_error_reported);
}

tc_entity_handle tc_render_target_get_xr_origin_entity(tc_render_target_handle h) {
    if (!render_target_handle_alive(h))
        return TC_ENTITY_HANDLE_INVALID;
    return RT_SLOTS[h.index].xr_origin_entity;
}

void tc_render_target_set_pipeline(tc_render_target_handle h, tc_pipeline_handle pipeline) {
    if (!render_target_handle_alive(h))
        return;
    RT_SLOTS[h.index].pipeline = pipeline;
}

tc_pipeline_handle tc_render_target_get_pipeline(tc_render_target_handle h) {
    if (!render_target_handle_alive(h))
        return TC_PIPELINE_HANDLE_INVALID;
    return RT_SLOTS[h.index].pipeline;
}

void tc_render_target_set_layer_mask(tc_render_target_handle h, uint64_t mask) {
    if (!render_target_handle_alive(h))
        return;
    RT_SLOTS[h.index].layer_mask = mask;
}

uint64_t tc_render_target_get_layer_mask(tc_render_target_handle h) {
    if (!render_target_handle_alive(h))
        return 0;
    return RT_SLOTS[h.index].layer_mask;
}

void tc_render_target_set_enabled(tc_render_target_handle h, bool enabled) {
    if (!render_target_handle_alive(h))
        return;
    RT_SLOTS[h.index].enabled = enabled;
}

bool tc_render_target_get_enabled(tc_render_target_handle h) {
    if (!render_target_handle_alive(h))
        return false;
    return RT_SLOTS[h.index].enabled;
}

void tc_render_target_set_locked(tc_render_target_handle h, bool locked) {
    if (!render_target_handle_alive(h))
        return;
    RT_SLOTS[h.index].locked = locked;
}

bool tc_render_target_get_locked(tc_render_target_handle h) {
    if (!render_target_handle_alive(h))
        return false;
    return RT_SLOTS[h.index].locked;
}

const tc_value* tc_render_target_get_pipeline_params(tc_render_target_handle h) {
    if (!render_target_handle_alive(h))
        return NULL;
    return RT_SLOTS[h.index].pipeline_params;
}

void tc_render_target_set_pipeline_params(tc_render_target_handle h, const tc_value* dict) {
    if (!render_target_handle_alive(h))
        return;
    tc_value** slot = &RT_SLOTS[h.index].pipeline_params;
    if (*slot) {
        tc_value_free(*slot);
        free(*slot);
        *slot = NULL;
    }
    if (dict && dict->type == TC_VALUE_DICT && dict->data.dict.count > 0) {
        *slot = (tc_value*)malloc(sizeof(tc_value));
        **slot = tc_value_copy(dict);
    }
}
