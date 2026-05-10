// tc_render_target.c - Render target pool implementation
#include "render/tc_render_target.h"
#include "render/tc_render_target_pool.h"
#include "tc_value.h"
#include "core/tc_component.h"
#include "tgfx/resources/tc_texture.h"
#include "tgfx/resources/tc_texture_registry.h"
#include <tcbase/tc_log.h>
#include <stdlib.h>
#include <string.h>

#define MAX_RENDER_TARGETS 128
#define INITIAL_POOL_CAPACITY 8

typedef struct {
    uint32_t* generations;
    bool* alive;
    char** names;
    int* widths;
    int* heights;
    bool* dynamic_resolutions;
    tc_texture_format* color_formats;
    tc_texture_format* depth_formats;
    bool* clear_color_enabled;
    float (*clear_color_values)[4];
    bool* clear_depth_enabled;
    float* clear_depth_values;
    tc_scene_handle* scenes;
    tc_component** cameras;
    tc_entity_handle* camera_entities;
    tc_pipeline_handle* pipelines;
    uint64_t* layer_masks;
    bool* enabled;
    bool* locked;
    // Owned color + depth textures, allocated lazily by
    // tc_render_target_ensure_textures(). Invalid handles before the
    // first ensure call — getters return them as-is.
    tc_texture_handle* color_textures;
    tc_texture_handle* depth_textures;
    // pipeline_params: dict of slot_name → rt_name (NULL if empty)
    tc_value** pipeline_params;
    uint32_t* free_stack;
    size_t free_count;
    size_t capacity;
    size_t count;
} RenderTargetPool;

// Default formats for render-target attachments. Matches the choice in
// docs/plans/2026-04-22-render-target-tc-texture-migration.md (Phase 3).
#define RT_DEFAULT_COLOR_FORMAT TC_TEXTURE_RGBA16F
#define RT_DEFAULT_DEPTH_FORMAT TC_TEXTURE_DEPTH32F
#define RT_DEFAULT_COLOR_USAGE \
    (TC_TEXTURE_USAGE_SAMPLED | TC_TEXTURE_USAGE_COLOR_ATTACHMENT \
     | TC_TEXTURE_USAGE_COPY_SRC | TC_TEXTURE_USAGE_COPY_DST)
#define RT_DEFAULT_DEPTH_USAGE \
    (TC_TEXTURE_USAGE_SAMPLED | TC_TEXTURE_USAGE_DEPTH_ATTACHMENT)

static RenderTargetPool* g_pool = NULL;

static char* rt_strdup(const char* s) {
    if (s == NULL) return NULL;
    size_t len = strlen(s) + 1;
    char* copy = (char*)malloc(len);
    if (copy) memcpy(copy, s, len);
    return copy;
}

static void rt_strset(char** dest, const char* src) {
    free(*dest);
    *dest = rt_strdup(src);
}

void tc_render_target_pool_init(void) {
    if (g_pool) {
        tc_log_warn("[tc_render_target_pool] already initialized");
        return;
    }

    g_pool = (RenderTargetPool*)calloc(1, sizeof(RenderTargetPool));
    if (!g_pool) {
        tc_log_error("[tc_render_target_pool] allocation failed");
        return;
    }

    size_t cap = INITIAL_POOL_CAPACITY;

    g_pool->generations = (uint32_t*)calloc(cap, sizeof(uint32_t));
    g_pool->alive = (bool*)calloc(cap, sizeof(bool));
    g_pool->names = (char**)calloc(cap, sizeof(char*));
    g_pool->widths = (int*)calloc(cap, sizeof(int));
    g_pool->heights = (int*)calloc(cap, sizeof(int));
    g_pool->dynamic_resolutions = (bool*)calloc(cap, sizeof(bool));
    g_pool->color_formats = (tc_texture_format*)calloc(cap, sizeof(tc_texture_format));
    g_pool->depth_formats = (tc_texture_format*)calloc(cap, sizeof(tc_texture_format));
    g_pool->clear_color_enabled = (bool*)calloc(cap, sizeof(bool));
    g_pool->clear_color_values = (float (*)[4])calloc(cap, sizeof(float[4]));
    g_pool->clear_depth_enabled = (bool*)calloc(cap, sizeof(bool));
    g_pool->clear_depth_values = (float*)calloc(cap, sizeof(float));
    g_pool->scenes = (tc_scene_handle*)calloc(cap, sizeof(tc_scene_handle));
    g_pool->cameras = (tc_component**)calloc(cap, sizeof(tc_component*));
    g_pool->camera_entities = (tc_entity_handle*)calloc(cap, sizeof(tc_entity_handle));
    g_pool->pipelines = (tc_pipeline_handle*)calloc(cap, sizeof(tc_pipeline_handle));
    g_pool->layer_masks = (uint64_t*)calloc(cap, sizeof(uint64_t));
    g_pool->enabled = (bool*)calloc(cap, sizeof(bool));
    g_pool->locked = (bool*)calloc(cap, sizeof(bool));
    g_pool->color_textures = (tc_texture_handle*)calloc(cap, sizeof(tc_texture_handle));
    g_pool->depth_textures = (tc_texture_handle*)calloc(cap, sizeof(tc_texture_handle));
    g_pool->pipeline_params = (tc_value**)calloc(cap, sizeof(tc_value*));

    g_pool->free_stack = (uint32_t*)malloc(cap * sizeof(uint32_t));
    for (size_t i = 0; i < cap; i++) {
        g_pool->free_stack[i] = (uint32_t)(cap - 1 - i);
        g_pool->scenes[i] = TC_SCENE_HANDLE_INVALID;
        g_pool->camera_entities[i] = TC_ENTITY_HANDLE_INVALID;
        g_pool->pipelines[i] = TC_PIPELINE_HANDLE_INVALID;
        g_pool->color_formats[i] = RT_DEFAULT_COLOR_FORMAT;
        g_pool->depth_formats[i] = RT_DEFAULT_DEPTH_FORMAT;
        g_pool->clear_color_values[i][3] = 1.0f;
        g_pool->clear_depth_values[i] = 1.0f;
        g_pool->color_textures[i] = tc_texture_handle_invalid();
        g_pool->depth_textures[i] = tc_texture_handle_invalid();
    }
    g_pool->free_count = cap;
    g_pool->capacity = cap;
    g_pool->count = 0;
}

void tc_render_target_pool_shutdown(void) {
    if (!g_pool) {
        tc_log_warn("[tc_render_target_pool] not initialized");
        return;
    }

    for (size_t i = 0; i < g_pool->capacity; i++) {
        if (g_pool->alive[i]) {
            free(g_pool->names[i]);
            if (g_pool->pipeline_params[i]) {
                tc_value_free(g_pool->pipeline_params[i]);
                free(g_pool->pipeline_params[i]);
                g_pool->pipeline_params[i] = NULL;
            }
            if (!tc_texture_handle_is_invalid(g_pool->color_textures[i])) {
                tc_texture_destroy(g_pool->color_textures[i]);
            }
            if (!tc_texture_handle_is_invalid(g_pool->depth_textures[i])) {
                tc_texture_destroy(g_pool->depth_textures[i]);
            }
        }
    }

    free(g_pool->generations);
    free(g_pool->alive);
    free(g_pool->names);
    free(g_pool->widths);
    free(g_pool->heights);
    free(g_pool->dynamic_resolutions);
    free(g_pool->color_formats);
    free(g_pool->depth_formats);
    free(g_pool->clear_color_enabled);
    free(g_pool->clear_color_values);
    free(g_pool->clear_depth_enabled);
    free(g_pool->clear_depth_values);
    free(g_pool->scenes);
    free(g_pool->cameras);
    free(g_pool->camera_entities);
    free(g_pool->pipelines);
    free(g_pool->layer_masks);
    free(g_pool->enabled);
    free(g_pool->locked);
    free(g_pool->color_textures);
    free(g_pool->depth_textures);
    free(g_pool->pipeline_params);
    free(g_pool->free_stack);
    free(g_pool);
    g_pool = NULL;
}

static void pool_grow(void) {
    size_t old_cap = g_pool->capacity;
    size_t new_cap = old_cap * 2;
    if (new_cap > MAX_RENDER_TARGETS) new_cap = MAX_RENDER_TARGETS;
    if (new_cap <= old_cap) {
        tc_log_error("[tc_render_target_pool] max capacity reached");
        return;
    }

    g_pool->generations = realloc(g_pool->generations, new_cap * sizeof(uint32_t));
    g_pool->alive = realloc(g_pool->alive, new_cap * sizeof(bool));
    g_pool->names = realloc(g_pool->names, new_cap * sizeof(char*));
    g_pool->widths = realloc(g_pool->widths, new_cap * sizeof(int));
    g_pool->heights = realloc(g_pool->heights, new_cap * sizeof(int));
    g_pool->dynamic_resolutions = realloc(g_pool->dynamic_resolutions, new_cap * sizeof(bool));
    g_pool->color_formats = realloc(g_pool->color_formats, new_cap * sizeof(tc_texture_format));
    g_pool->depth_formats = realloc(g_pool->depth_formats, new_cap * sizeof(tc_texture_format));
    g_pool->clear_color_enabled = realloc(g_pool->clear_color_enabled, new_cap * sizeof(bool));
    g_pool->clear_color_values = realloc(g_pool->clear_color_values, new_cap * sizeof(float[4]));
    g_pool->clear_depth_enabled = realloc(g_pool->clear_depth_enabled, new_cap * sizeof(bool));
    g_pool->clear_depth_values = realloc(g_pool->clear_depth_values, new_cap * sizeof(float));
    g_pool->scenes = realloc(g_pool->scenes, new_cap * sizeof(tc_scene_handle));
    g_pool->cameras = realloc(g_pool->cameras, new_cap * sizeof(tc_component*));
    g_pool->camera_entities = realloc(g_pool->camera_entities, new_cap * sizeof(tc_entity_handle));
    g_pool->pipelines = realloc(g_pool->pipelines, new_cap * sizeof(tc_pipeline_handle));
    g_pool->layer_masks = realloc(g_pool->layer_masks, new_cap * sizeof(uint64_t));
    g_pool->enabled = realloc(g_pool->enabled, new_cap * sizeof(bool));
    g_pool->locked = realloc(g_pool->locked, new_cap * sizeof(bool));
    g_pool->color_textures = realloc(g_pool->color_textures, new_cap * sizeof(tc_texture_handle));
    g_pool->depth_textures = realloc(g_pool->depth_textures, new_cap * sizeof(tc_texture_handle));
    g_pool->pipeline_params = realloc(g_pool->pipeline_params, new_cap * sizeof(tc_value*));
    g_pool->free_stack = realloc(g_pool->free_stack, new_cap * sizeof(uint32_t));

    memset(g_pool->generations + old_cap, 0, (new_cap - old_cap) * sizeof(uint32_t));
    memset(g_pool->alive + old_cap, 0, (new_cap - old_cap) * sizeof(bool));
    memset(g_pool->names + old_cap, 0, (new_cap - old_cap) * sizeof(char*));
    memset(g_pool->widths + old_cap, 0, (new_cap - old_cap) * sizeof(int));
    memset(g_pool->heights + old_cap, 0, (new_cap - old_cap) * sizeof(int));
    memset(g_pool->dynamic_resolutions + old_cap, 0, (new_cap - old_cap) * sizeof(bool));
    memset(g_pool->color_formats + old_cap, 0, (new_cap - old_cap) * sizeof(tc_texture_format));
    memset(g_pool->depth_formats + old_cap, 0, (new_cap - old_cap) * sizeof(tc_texture_format));
    memset(g_pool->clear_color_enabled + old_cap, 0, (new_cap - old_cap) * sizeof(bool));
    memset(g_pool->clear_color_values + old_cap, 0, (new_cap - old_cap) * sizeof(float[4]));
    memset(g_pool->clear_depth_enabled + old_cap, 0, (new_cap - old_cap) * sizeof(bool));
    memset(g_pool->clear_depth_values + old_cap, 0, (new_cap - old_cap) * sizeof(float));
    memset(g_pool->cameras + old_cap, 0, (new_cap - old_cap) * sizeof(tc_component*));
    memset(g_pool->layer_masks + old_cap, 0, (new_cap - old_cap) * sizeof(uint64_t));
    memset(g_pool->enabled + old_cap, 0, (new_cap - old_cap) * sizeof(bool));
    memset(g_pool->locked + old_cap, 0, (new_cap - old_cap) * sizeof(bool));
    memset(g_pool->pipeline_params + old_cap, 0, (new_cap - old_cap) * sizeof(tc_value*));

    for (size_t i = old_cap; i < new_cap; i++) {
        g_pool->scenes[i] = TC_SCENE_HANDLE_INVALID;
        g_pool->camera_entities[i] = TC_ENTITY_HANDLE_INVALID;
        g_pool->pipelines[i] = TC_PIPELINE_HANDLE_INVALID;
        g_pool->color_formats[i] = RT_DEFAULT_COLOR_FORMAT;
        g_pool->depth_formats[i] = RT_DEFAULT_DEPTH_FORMAT;
        g_pool->clear_color_values[i][3] = 1.0f;
        g_pool->clear_depth_values[i] = 1.0f;
        g_pool->color_textures[i] = tc_texture_handle_invalid();
        g_pool->depth_textures[i] = tc_texture_handle_invalid();
    }

    for (size_t i = old_cap; i < new_cap; i++) {
        g_pool->free_stack[g_pool->free_count++] = (uint32_t)(new_cap - 1 - (i - old_cap));
    }

    g_pool->capacity = new_cap;
}

static inline bool handle_alive(tc_render_target_handle h) {
    if (!g_pool) return false;
    if (h.index >= g_pool->capacity) return false;
    return g_pool->alive[h.index] && g_pool->generations[h.index] == h.generation;
}

static bool rt_format_is_depth(tc_texture_format format) {
    return format == TC_TEXTURE_DEPTH24 || format == TC_TEXTURE_DEPTH32F;
}

bool tc_render_target_format_from_string(const char* name, tc_texture_format* out_format) {
    if (!name || !out_format) return false;
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
        case TC_TEXTURE_RGBA8: return "rgba8";
        case TC_TEXTURE_RGB8: return "rgb8";
        case TC_TEXTURE_RG8: return "rg8";
        case TC_TEXTURE_R8: return "r8";
        case TC_TEXTURE_RGBA16F: return "rgba16f";
        case TC_TEXTURE_RGB16F: return "rgb16f";
        case TC_TEXTURE_DEPTH24: return "depth24";
        case TC_TEXTURE_DEPTH32F: return "depth32f";
    }
    return "unknown";
}

bool tc_render_target_pool_alive(tc_render_target_handle h) {
    return handle_alive(h);
}

bool tc_render_target_alive(tc_render_target_handle h) {
    return handle_alive(h);
}

tc_render_target_handle tc_render_target_pool_alloc(const char* name) {
    if (!g_pool) {
        tc_render_target_pool_init();
    }

    if (g_pool->free_count == 0) {
        pool_grow();
        if (g_pool->free_count == 0) {
            tc_log_error("[tc_render_target_pool] no free slots");
            return TC_RENDER_TARGET_HANDLE_INVALID;
        }
    }

    uint32_t idx = g_pool->free_stack[--g_pool->free_count];
    uint32_t gen = g_pool->generations[idx];

    g_pool->alive[idx] = true;
    g_pool->names[idx] = rt_strdup(name);
    g_pool->widths[idx] = 512;
    g_pool->heights[idx] = 512;
    g_pool->dynamic_resolutions[idx] = false;
    g_pool->color_formats[idx] = RT_DEFAULT_COLOR_FORMAT;
    g_pool->depth_formats[idx] = RT_DEFAULT_DEPTH_FORMAT;
    g_pool->clear_color_enabled[idx] = false;
    g_pool->clear_color_values[idx][0] = 0.0f;
    g_pool->clear_color_values[idx][1] = 0.0f;
    g_pool->clear_color_values[idx][2] = 0.0f;
    g_pool->clear_color_values[idx][3] = 1.0f;
    g_pool->clear_depth_enabled[idx] = false;
    g_pool->clear_depth_values[idx] = 1.0f;
    g_pool->scenes[idx] = TC_SCENE_HANDLE_INVALID;
    g_pool->cameras[idx] = NULL;
    g_pool->camera_entities[idx] = TC_ENTITY_HANDLE_INVALID;
    g_pool->pipelines[idx] = TC_PIPELINE_HANDLE_INVALID;
    g_pool->layer_masks[idx] = 0xFFFFFFFFFFFFFFFFULL;
    g_pool->enabled[idx] = true;
    g_pool->locked[idx] = false;
    g_pool->color_textures[idx] = tc_texture_handle_invalid();
    g_pool->depth_textures[idx] = tc_texture_handle_invalid();
    g_pool->count++;

    tc_render_target_handle h = { idx, gen };
    return h;
}

void tc_render_target_pool_free(tc_render_target_handle h) {
    if (!handle_alive(h)) return;
    uint32_t idx = h.index;

    if (g_pool->locked[idx]) {
        tc_log_error(
            "[tc_render_target] attempt to free locked render target '%s' "
            "(index=%u, gen=%u) — the owning viewport must unlock it first",
            g_pool->names[idx] ? g_pool->names[idx] : "(unnamed)",
            h.index, h.generation
        );
        return;
    }

    free(g_pool->names[idx]);
    if (g_pool->pipeline_params[idx]) {
        tc_value_free(g_pool->pipeline_params[idx]);
        free(g_pool->pipeline_params[idx]);
        g_pool->pipeline_params[idx] = NULL;
    }
    if (!tc_texture_handle_is_invalid(g_pool->color_textures[idx])) {
        tc_texture_destroy(g_pool->color_textures[idx]);
        g_pool->color_textures[idx] = tc_texture_handle_invalid();
    }
    if (!tc_texture_handle_is_invalid(g_pool->depth_textures[idx])) {
        tc_texture_destroy(g_pool->depth_textures[idx]);
        g_pool->depth_textures[idx] = tc_texture_handle_invalid();
    }

    g_pool->alive[idx] = false;
    g_pool->generations[idx]++;
    g_pool->free_stack[g_pool->free_count++] = idx;
    g_pool->count--;
}

void tc_render_target_pool_foreach(tc_render_target_pool_iter_fn callback, void* user_data) {
    if (!g_pool || !callback) return;
    for (uint32_t i = 0; i < g_pool->capacity; i++) {
        if (g_pool->alive[i]) {
            tc_render_target_handle h = { i, g_pool->generations[i] };
            if (!callback(h, user_data)) {
                break;
            }
        }
    }
}

size_t tc_render_target_pool_count(void) {
    return g_pool ? g_pool->count : 0;
}

tc_render_target_handle tc_render_target_new(const char* name) {
    return tc_render_target_pool_alloc(name);
}

void tc_render_target_free(tc_render_target_handle h) {
    tc_render_target_pool_free(h);
}

void tc_render_target_set_name(tc_render_target_handle h, const char* name) {
    if (!handle_alive(h)) return;
    rt_strset(&g_pool->names[h.index], name);
}

const char* tc_render_target_get_name(tc_render_target_handle h) {
    if (!handle_alive(h)) return NULL;
    return g_pool->names[h.index];
}

// Resize an already-allocated tc_texture in place. Bumps `header.version`
// so the bridge re-creates the GPU image on the next wrap call. No-op if
// the handle is invalid (textures haven't been ensured yet).
static void rt_resize_owned_texture(
    tc_texture_handle h, uint32_t width, uint32_t height
) {
    if (tc_texture_handle_is_invalid(h)) return;
    tc_texture* tex = tc_texture_get(h);
    if (!tex) return;
    tc_texture_set_size_format(
        tex, width, height,
        (tc_texture_format)tex->format
    );
}

static void rt_reformat_owned_texture(
    tc_texture_handle h, uint32_t width, uint32_t height, tc_texture_format format
) {
    if (tc_texture_handle_is_invalid(h)) return;
    tc_texture* tex = tc_texture_get(h);
    if (!tex) return;
    tc_texture_set_size_format(tex, width, height, format);
}

void tc_render_target_set_width(tc_render_target_handle h, int width) {
    if (!handle_alive(h)) return;
    if (g_pool->widths[h.index] == width) return;  // No-op on same size — skips
                                                    // a needless version bump that
                                                    // would force a per-frame
                                                    // GPU image re-create.
    g_pool->widths[h.index] = width;
    rt_resize_owned_texture(g_pool->color_textures[h.index],
                            (uint32_t)width, (uint32_t)g_pool->heights[h.index]);
    rt_resize_owned_texture(g_pool->depth_textures[h.index],
                            (uint32_t)width, (uint32_t)g_pool->heights[h.index]);
}

int tc_render_target_get_width(tc_render_target_handle h) {
    if (!handle_alive(h)) return 0;
    return g_pool->widths[h.index];
}

void tc_render_target_set_height(tc_render_target_handle h, int height) {
    if (!handle_alive(h)) return;
    if (g_pool->heights[h.index] == height) return;  // No-op on same size.
    g_pool->heights[h.index] = height;
    rt_resize_owned_texture(g_pool->color_textures[h.index],
                            (uint32_t)g_pool->widths[h.index], (uint32_t)height);
    rt_resize_owned_texture(g_pool->depth_textures[h.index],
                            (uint32_t)g_pool->widths[h.index], (uint32_t)height);
}

int tc_render_target_get_height(tc_render_target_handle h) {
    if (!handle_alive(h)) return 0;
    return g_pool->heights[h.index];
}

void tc_render_target_set_dynamic_resolution(tc_render_target_handle h, bool dynamic_resolution) {
    if (!handle_alive(h)) return;
    g_pool->dynamic_resolutions[h.index] = dynamic_resolution;
}

bool tc_render_target_get_dynamic_resolution(tc_render_target_handle h) {
    if (!handle_alive(h)) return false;
    return g_pool->dynamic_resolutions[h.index];
}

void tc_render_target_set_color_format(tc_render_target_handle h, tc_texture_format format) {
    if (!handle_alive(h)) return;
    if (rt_format_is_depth(format)) {
        tc_log_error(
            "[tc_render_target] rejected depth format '%s' for color attachment",
            tc_render_target_format_to_string(format)
        );
        return;
    }
    if (g_pool->color_formats[h.index] == format) return;
    g_pool->color_formats[h.index] = format;
    rt_reformat_owned_texture(
        g_pool->color_textures[h.index],
        (uint32_t)g_pool->widths[h.index],
        (uint32_t)g_pool->heights[h.index],
        format
    );
}

tc_texture_format tc_render_target_get_color_format(tc_render_target_handle h) {
    if (!handle_alive(h)) return RT_DEFAULT_COLOR_FORMAT;
    return g_pool->color_formats[h.index];
}

void tc_render_target_set_depth_format(tc_render_target_handle h, tc_texture_format format) {
    if (!handle_alive(h)) return;
    if (!rt_format_is_depth(format)) {
        tc_log_error(
            "[tc_render_target] rejected color format '%s' for depth attachment",
            tc_render_target_format_to_string(format)
        );
        return;
    }
    if (g_pool->depth_formats[h.index] == format) return;
    g_pool->depth_formats[h.index] = format;
    rt_reformat_owned_texture(
        g_pool->depth_textures[h.index],
        (uint32_t)g_pool->widths[h.index],
        (uint32_t)g_pool->heights[h.index],
        format
    );
}

tc_texture_format tc_render_target_get_depth_format(tc_render_target_handle h) {
    if (!handle_alive(h)) return RT_DEFAULT_DEPTH_FORMAT;
    return g_pool->depth_formats[h.index];
}

void tc_render_target_set_clear_color_enabled(tc_render_target_handle h, bool enabled) {
    if (!handle_alive(h)) return;
    g_pool->clear_color_enabled[h.index] = enabled;
}

bool tc_render_target_get_clear_color_enabled(tc_render_target_handle h) {
    if (!handle_alive(h)) return false;
    return g_pool->clear_color_enabled[h.index];
}

void tc_render_target_set_clear_color_value(tc_render_target_handle h, float r, float g, float b, float a) {
    if (!handle_alive(h)) return;
    g_pool->clear_color_values[h.index][0] = r;
    g_pool->clear_color_values[h.index][1] = g;
    g_pool->clear_color_values[h.index][2] = b;
    g_pool->clear_color_values[h.index][3] = a;
}

void tc_render_target_get_clear_color_value(tc_render_target_handle h, float out_rgba[4]) {
    if (!out_rgba) return;
    out_rgba[0] = 0.0f;
    out_rgba[1] = 0.0f;
    out_rgba[2] = 0.0f;
    out_rgba[3] = 1.0f;
    if (!handle_alive(h)) return;
    memcpy(out_rgba, g_pool->clear_color_values[h.index], sizeof(float[4]));
}

void tc_render_target_set_clear_depth_enabled(tc_render_target_handle h, bool enabled) {
    if (!handle_alive(h)) return;
    g_pool->clear_depth_enabled[h.index] = enabled;
}

bool tc_render_target_get_clear_depth_enabled(tc_render_target_handle h) {
    if (!handle_alive(h)) return false;
    return g_pool->clear_depth_enabled[h.index];
}

void tc_render_target_set_clear_depth_value(tc_render_target_handle h, float value) {
    if (!handle_alive(h)) return;
    g_pool->clear_depth_values[h.index] = value;
}

float tc_render_target_get_clear_depth_value(tc_render_target_handle h) {
    if (!handle_alive(h)) return 1.0f;
    return g_pool->clear_depth_values[h.index];
}

// --- Owned textures --------------------------------------------------------

void tc_render_target_ensure_textures(tc_render_target_handle h) {
    if (!handle_alive(h)) return;
    uint32_t idx = h.index;

    const uint32_t w = (uint32_t)g_pool->widths[idx];
    const uint32_t height_ = (uint32_t)g_pool->heights[idx];

    // Owned through tc_texture_destroy on pool_free — no add_ref dance,
    // the render target is the single owner. External code that wants
    // longer-than-RT lifetime of these handles must take its own ref.
    if (tc_texture_handle_is_invalid(g_pool->color_textures[idx])) {
        tc_texture_handle ch = tc_texture_create(NULL);
        tc_texture* tex = tc_texture_get(ch);
        if (tex) {
            tc_texture_set_storage_kind(tex, TC_TEXTURE_STORAGE_GPU_FIRST);
            tc_texture_set_usage(tex, RT_DEFAULT_COLOR_USAGE);
            tc_texture_set_size_format(tex, w, height_, g_pool->color_formats[idx]);
            g_pool->color_textures[idx] = ch;
        } else {
            tc_log_error("[tc_render_target] failed to create color texture");
        }
    }

    if (tc_texture_handle_is_invalid(g_pool->depth_textures[idx])) {
        tc_texture_handle dh = tc_texture_create(NULL);
        tc_texture* tex = tc_texture_get(dh);
        if (tex) {
            tc_texture_set_storage_kind(tex, TC_TEXTURE_STORAGE_GPU_FIRST);
            tc_texture_set_usage(tex, RT_DEFAULT_DEPTH_USAGE);
            tc_texture_set_size_format(tex, w, height_, g_pool->depth_formats[idx]);
            g_pool->depth_textures[idx] = dh;
        } else {
            tc_log_error("[tc_render_target] failed to create depth texture");
        }
    }
}

tc_texture_handle tc_render_target_get_color_texture(tc_render_target_handle h) {
    if (!handle_alive(h)) return tc_texture_handle_invalid();
    return g_pool->color_textures[h.index];
}

tc_texture_handle tc_render_target_get_depth_texture(tc_render_target_handle h) {
    if (!handle_alive(h)) return tc_texture_handle_invalid();
    return g_pool->depth_textures[h.index];
}

void tc_render_target_set_scene(tc_render_target_handle h, tc_scene_handle scene) {
    if (!handle_alive(h)) return;
    g_pool->scenes[h.index] = scene;
}

tc_scene_handle tc_render_target_get_scene(tc_render_target_handle h) {
    if (!handle_alive(h)) return TC_SCENE_HANDLE_INVALID;
    return g_pool->scenes[h.index];
}

void tc_render_target_set_camera(tc_render_target_handle h, tc_component* camera) {
    if (!handle_alive(h)) return;
    g_pool->cameras[h.index] = camera;
    g_pool->camera_entities[h.index] = camera ? camera->owner : TC_ENTITY_HANDLE_INVALID;
}

tc_component* tc_render_target_get_camera(tc_render_target_handle h) {
    if (!handle_alive(h)) return NULL;
    return g_pool->cameras[h.index];
}

tc_entity_handle tc_render_target_get_camera_entity(tc_render_target_handle h) {
    if (!handle_alive(h)) return TC_ENTITY_HANDLE_INVALID;
    return g_pool->camera_entities[h.index];
}

void tc_render_target_set_pipeline(tc_render_target_handle h, tc_pipeline_handle pipeline) {
    if (!handle_alive(h)) return;
    g_pool->pipelines[h.index] = pipeline;
}

tc_pipeline_handle tc_render_target_get_pipeline(tc_render_target_handle h) {
    if (!handle_alive(h)) return TC_PIPELINE_HANDLE_INVALID;
    return g_pool->pipelines[h.index];
}

void tc_render_target_set_layer_mask(tc_render_target_handle h, uint64_t mask) {
    if (!handle_alive(h)) return;
    g_pool->layer_masks[h.index] = mask;
}

uint64_t tc_render_target_get_layer_mask(tc_render_target_handle h) {
    if (!handle_alive(h)) return 0;
    return g_pool->layer_masks[h.index];
}

void tc_render_target_set_enabled(tc_render_target_handle h, bool enabled) {
    if (!handle_alive(h)) return;
    g_pool->enabled[h.index] = enabled;
}

bool tc_render_target_get_enabled(tc_render_target_handle h) {
    if (!handle_alive(h)) return false;
    return g_pool->enabled[h.index];
}

void tc_render_target_set_locked(tc_render_target_handle h, bool locked) {
    if (!handle_alive(h)) return;
    g_pool->locked[h.index] = locked;
}

bool tc_render_target_get_locked(tc_render_target_handle h) {
    if (!handle_alive(h)) return false;
    return g_pool->locked[h.index];
}

const tc_value* tc_render_target_get_pipeline_params(tc_render_target_handle h) {
    if (!handle_alive(h)) return NULL;
    return g_pool->pipeline_params[h.index];
}

void tc_render_target_set_pipeline_params(tc_render_target_handle h, const tc_value* dict) {
    if (!handle_alive(h)) return;
    tc_value** slot = &g_pool->pipeline_params[h.index];
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
