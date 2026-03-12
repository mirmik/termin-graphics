// tc_viewport.c - Viewport implementation using pool with generational indices
#include "render/tc_viewport.h"
#include "render/tc_viewport_pool.h"
#include "core/tc_component.h"
#include <tcbase/tc_log.h>
#include <stdlib.h>
#include <string.h>

#define MAX_VIEWPORTS 256
#define INITIAL_POOL_CAPACITY 16

typedef struct {
    uint32_t* generations;
    bool* alive;
    char** names;
    tc_scene_handle* scenes;
    tc_component** cameras;
    tc_entity_handle* camera_entities;
    float* rects;
    int* pixel_rects;
    int* depths;
    tc_pipeline_handle* pipelines;
    uint64_t* layer_masks;
    bool* enabled;
    char** input_modes;
    bool* block_input_in_editor;
    char** managed_by;
    tc_entity_handle* internal_entities;
    tc_input_manager** input_managers;
    tc_viewport_handle* display_prevs;
    tc_viewport_handle* display_nexts;
    uint32_t* free_stack;
    size_t free_count;
    size_t capacity;
    size_t count;
} ViewportPool;

static ViewportPool* g_pool = NULL;

static char* tc_strdup_local(const char* s) {
    if (s == NULL) return NULL;
    size_t len = strlen(s) + 1;
    char* copy = (char*)malloc(len);
    if (copy) memcpy(copy, s, len);
    return copy;
}

static void tc_strset(char** dest, const char* src) {
    free(*dest);
    *dest = tc_strdup_local(src);
}

void tc_viewport_pool_init(void) {
    if (g_pool) {
        tc_log_warn("[tc_viewport_pool] already initialized");
        return;
    }

    g_pool = (ViewportPool*)calloc(1, sizeof(ViewportPool));
    if (!g_pool) {
        tc_log_error("[tc_viewport_pool] allocation failed");
        return;
    }

    size_t cap = INITIAL_POOL_CAPACITY;

    g_pool->generations = (uint32_t*)calloc(cap, sizeof(uint32_t));
    g_pool->alive = (bool*)calloc(cap, sizeof(bool));
    g_pool->names = (char**)calloc(cap, sizeof(char*));
    g_pool->scenes = (tc_scene_handle*)calloc(cap, sizeof(tc_scene_handle));
    g_pool->cameras = (tc_component**)calloc(cap, sizeof(tc_component*));
    g_pool->camera_entities = (tc_entity_handle*)calloc(cap, sizeof(tc_entity_handle));
    g_pool->rects = (float*)calloc(cap * 4, sizeof(float));
    g_pool->pixel_rects = (int*)calloc(cap * 4, sizeof(int));
    g_pool->depths = (int*)calloc(cap, sizeof(int));
    g_pool->pipelines = (tc_pipeline_handle*)calloc(cap, sizeof(tc_pipeline_handle));
    g_pool->layer_masks = (uint64_t*)calloc(cap, sizeof(uint64_t));
    g_pool->enabled = (bool*)calloc(cap, sizeof(bool));
    g_pool->input_modes = (char**)calloc(cap, sizeof(char*));
    g_pool->block_input_in_editor = (bool*)calloc(cap, sizeof(bool));
    g_pool->managed_by = (char**)calloc(cap, sizeof(char*));
    g_pool->input_managers = (tc_input_manager**)calloc(cap, sizeof(tc_input_manager*));
    g_pool->internal_entities = (tc_entity_handle*)calloc(cap, sizeof(tc_entity_handle));
    g_pool->display_prevs = (tc_viewport_handle*)calloc(cap, sizeof(tc_viewport_handle));
    g_pool->display_nexts = (tc_viewport_handle*)calloc(cap, sizeof(tc_viewport_handle));

    g_pool->free_stack = (uint32_t*)malloc(cap * sizeof(uint32_t));
    for (size_t i = 0; i < cap; i++) {
        g_pool->free_stack[i] = (uint32_t)(cap - 1 - i);
        g_pool->scenes[i] = TC_SCENE_HANDLE_INVALID;
        g_pool->camera_entities[i] = TC_ENTITY_HANDLE_INVALID;
        g_pool->display_prevs[i] = TC_VIEWPORT_HANDLE_INVALID;
        g_pool->display_nexts[i] = TC_VIEWPORT_HANDLE_INVALID;
        g_pool->internal_entities[i] = TC_ENTITY_HANDLE_INVALID;
    }
    g_pool->free_count = cap;
    g_pool->capacity = cap;
    g_pool->count = 0;
}

void tc_viewport_pool_shutdown(void) {
    if (!g_pool) {
        tc_log_warn("[tc_viewport_pool] not initialized");
        return;
    }

    for (size_t i = 0; i < g_pool->capacity; i++) {
        if (g_pool->alive[i]) {
            free(g_pool->names[i]);
            free(g_pool->input_modes[i]);
            free(g_pool->managed_by[i]);
        }
    }

    free(g_pool->generations);
    free(g_pool->alive);
    free(g_pool->names);
    free(g_pool->scenes);
    free(g_pool->cameras);
    free(g_pool->camera_entities);
    free(g_pool->rects);
    free(g_pool->pixel_rects);
    free(g_pool->depths);
    free(g_pool->pipelines);
    free(g_pool->layer_masks);
    free(g_pool->enabled);
    free(g_pool->input_modes);
    free(g_pool->block_input_in_editor);
    free(g_pool->managed_by);
    free(g_pool->input_managers);
    free(g_pool->internal_entities);
    free(g_pool->display_prevs);
    free(g_pool->display_nexts);
    free(g_pool->free_stack);
    free(g_pool);
    g_pool = NULL;
}

static void pool_grow(void) {
    size_t old_cap = g_pool->capacity;
    size_t new_cap = old_cap * 2;
    if (new_cap > MAX_VIEWPORTS) new_cap = MAX_VIEWPORTS;
    if (new_cap <= old_cap) {
        tc_log_error("[tc_viewport_pool] max capacity reached");
        return;
    }

    g_pool->generations = realloc(g_pool->generations, new_cap * sizeof(uint32_t));
    g_pool->alive = realloc(g_pool->alive, new_cap * sizeof(bool));
    g_pool->names = realloc(g_pool->names, new_cap * sizeof(char*));
    g_pool->scenes = realloc(g_pool->scenes, new_cap * sizeof(tc_scene_handle));
    g_pool->cameras = realloc(g_pool->cameras, new_cap * sizeof(tc_component*));
    g_pool->camera_entities = realloc(g_pool->camera_entities, new_cap * sizeof(tc_entity_handle));
    g_pool->rects = realloc(g_pool->rects, new_cap * 4 * sizeof(float));
    g_pool->pixel_rects = realloc(g_pool->pixel_rects, new_cap * 4 * sizeof(int));
    g_pool->depths = realloc(g_pool->depths, new_cap * sizeof(int));
    g_pool->pipelines = realloc(g_pool->pipelines, new_cap * sizeof(tc_pipeline_handle));
    g_pool->layer_masks = realloc(g_pool->layer_masks, new_cap * sizeof(uint64_t));
    g_pool->enabled = realloc(g_pool->enabled, new_cap * sizeof(bool));
    g_pool->input_modes = realloc(g_pool->input_modes, new_cap * sizeof(char*));
    g_pool->block_input_in_editor = realloc(g_pool->block_input_in_editor, new_cap * sizeof(bool));
    g_pool->managed_by = realloc(g_pool->managed_by, new_cap * sizeof(char*));
    g_pool->input_managers = realloc(g_pool->input_managers, new_cap * sizeof(tc_input_manager*));
    g_pool->internal_entities = realloc(g_pool->internal_entities, new_cap * sizeof(tc_entity_handle));
    g_pool->display_prevs = realloc(g_pool->display_prevs, new_cap * sizeof(tc_viewport_handle));
    g_pool->display_nexts = realloc(g_pool->display_nexts, new_cap * sizeof(tc_viewport_handle));
    g_pool->free_stack = realloc(g_pool->free_stack, new_cap * sizeof(uint32_t));

    memset(g_pool->generations + old_cap, 0, (new_cap - old_cap) * sizeof(uint32_t));
    memset(g_pool->alive + old_cap, 0, (new_cap - old_cap) * sizeof(bool));
    memset(g_pool->names + old_cap, 0, (new_cap - old_cap) * sizeof(char*));
    memset(g_pool->cameras + old_cap, 0, (new_cap - old_cap) * sizeof(tc_component*));
    memset(g_pool->rects + old_cap * 4, 0, (new_cap - old_cap) * 4 * sizeof(float));
    memset(g_pool->pixel_rects + old_cap * 4, 0, (new_cap - old_cap) * 4 * sizeof(int));
    memset(g_pool->depths + old_cap, 0, (new_cap - old_cap) * sizeof(int));
    for (size_t i = old_cap; i < new_cap; i++) {
        g_pool->pipelines[i] = TC_PIPELINE_HANDLE_INVALID;
    }
    memset(g_pool->layer_masks + old_cap, 0, (new_cap - old_cap) * sizeof(uint64_t));
    memset(g_pool->enabled + old_cap, 0, (new_cap - old_cap) * sizeof(bool));
    memset(g_pool->input_modes + old_cap, 0, (new_cap - old_cap) * sizeof(char*));
    memset(g_pool->block_input_in_editor + old_cap, 0, (new_cap - old_cap) * sizeof(bool));
    memset(g_pool->managed_by + old_cap, 0, (new_cap - old_cap) * sizeof(char*));
    memset(g_pool->input_managers + old_cap, 0, (new_cap - old_cap) * sizeof(tc_input_manager*));

    for (size_t i = old_cap; i < new_cap; i++) {
        g_pool->scenes[i] = TC_SCENE_HANDLE_INVALID;
        g_pool->camera_entities[i] = TC_ENTITY_HANDLE_INVALID;
        g_pool->display_prevs[i] = TC_VIEWPORT_HANDLE_INVALID;
        g_pool->display_nexts[i] = TC_VIEWPORT_HANDLE_INVALID;
        g_pool->internal_entities[i] = TC_ENTITY_HANDLE_INVALID;
    }

    for (size_t i = old_cap; i < new_cap; i++) {
        g_pool->free_stack[g_pool->free_count++] = (uint32_t)(new_cap - 1 - (i - old_cap));
    }

    g_pool->capacity = new_cap;
}

static inline bool handle_alive(tc_viewport_handle h) {
    if (!g_pool) return false;
    if (h.index >= g_pool->capacity) return false;
    return g_pool->alive[h.index] && g_pool->generations[h.index] == h.generation;
}

bool tc_viewport_pool_alive(tc_viewport_handle h) {
    return handle_alive(h);
}

bool tc_viewport_alive(tc_viewport_handle h) {
    return handle_alive(h);
}

tc_viewport_handle tc_viewport_pool_alloc(const char* name) {
    if (!g_pool) {
        tc_viewport_pool_init();
    }

    if (g_pool->free_count == 0) {
        pool_grow();
        if (g_pool->free_count == 0) {
            tc_log_error("[tc_viewport_pool] no free slots");
            return TC_VIEWPORT_HANDLE_INVALID;
        }
    }

    uint32_t idx = g_pool->free_stack[--g_pool->free_count];
    uint32_t gen = g_pool->generations[idx];

    g_pool->alive[idx] = true;
    g_pool->names[idx] = tc_strdup_local(name);
    g_pool->scenes[idx] = TC_SCENE_HANDLE_INVALID;
    g_pool->cameras[idx] = NULL;
    g_pool->camera_entities[idx] = TC_ENTITY_HANDLE_INVALID;
    g_pool->rects[idx * 4 + 0] = 0.0f;
    g_pool->rects[idx * 4 + 1] = 0.0f;
    g_pool->rects[idx * 4 + 2] = 1.0f;
    g_pool->rects[idx * 4 + 3] = 1.0f;
    g_pool->pixel_rects[idx * 4 + 0] = 0;
    g_pool->pixel_rects[idx * 4 + 1] = 0;
    g_pool->pixel_rects[idx * 4 + 2] = 1;
    g_pool->pixel_rects[idx * 4 + 3] = 1;
    g_pool->depths[idx] = 0;
    g_pool->pipelines[idx] = TC_PIPELINE_HANDLE_INVALID;
    g_pool->layer_masks[idx] = 0xFFFFFFFFFFFFFFFFULL;
    g_pool->enabled[idx] = true;
    g_pool->input_modes[idx] = tc_strdup_local("simple");
    g_pool->block_input_in_editor[idx] = false;
    g_pool->managed_by[idx] = NULL;
    g_pool->input_managers[idx] = NULL;
    g_pool->internal_entities[idx] = TC_ENTITY_HANDLE_INVALID;
    g_pool->display_prevs[idx] = TC_VIEWPORT_HANDLE_INVALID;
    g_pool->display_nexts[idx] = TC_VIEWPORT_HANDLE_INVALID;
    g_pool->count++;

    tc_viewport_handle h = { idx, gen };
    return h;
}

tc_viewport_handle tc_viewport_new(const char* name, tc_scene_handle scene, tc_component* camera) {
    tc_viewport_handle h = tc_viewport_pool_alloc(name);
    if (tc_viewport_handle_valid(h)) {
        g_pool->scenes[h.index] = scene;
        g_pool->cameras[h.index] = camera;
        g_pool->camera_entities[h.index] = camera ? camera->owner : TC_ENTITY_HANDLE_INVALID;
    }
    return h;
}

void tc_viewport_pool_free(tc_viewport_handle h) {
    if (!handle_alive(h)) return;

    uint32_t idx = h.index;
    free(g_pool->names[idx]);
    free(g_pool->input_modes[idx]);
    free(g_pool->managed_by[idx]);
    g_pool->names[idx] = NULL;
    g_pool->input_modes[idx] = NULL;
    g_pool->managed_by[idx] = NULL;
    g_pool->alive[idx] = false;
    g_pool->generations[idx]++;
    g_pool->free_stack[g_pool->free_count++] = idx;
    g_pool->count--;
}

void tc_viewport_free(tc_viewport_handle h) {
    tc_viewport_pool_free(h);
}

void tc_viewport_set_name(tc_viewport_handle h, const char* name) {
    if (!handle_alive(h)) return;
    tc_strset(&g_pool->names[h.index], name);
}

const char* tc_viewport_get_name(tc_viewport_handle h) {
    if (!handle_alive(h)) return NULL;
    return g_pool->names[h.index];
}

void tc_viewport_set_rect(tc_viewport_handle h, float x, float y, float w, float height) {
    if (!handle_alive(h)) return;
    g_pool->rects[h.index * 4 + 0] = x;
    g_pool->rects[h.index * 4 + 1] = y;
    g_pool->rects[h.index * 4 + 2] = w;
    g_pool->rects[h.index * 4 + 3] = height;
}

void tc_viewport_get_rect(tc_viewport_handle h, float* x, float* y, float* w, float* height) {
    if (!handle_alive(h)) return;
    if (x) *x = g_pool->rects[h.index * 4 + 0];
    if (y) *y = g_pool->rects[h.index * 4 + 1];
    if (w) *w = g_pool->rects[h.index * 4 + 2];
    if (height) *height = g_pool->rects[h.index * 4 + 3];
}

void tc_viewport_set_pixel_rect(tc_viewport_handle h, int px, int py, int pw, int ph) {
    if (!handle_alive(h)) return;
    g_pool->pixel_rects[h.index * 4 + 0] = px;
    g_pool->pixel_rects[h.index * 4 + 1] = py;
    g_pool->pixel_rects[h.index * 4 + 2] = pw;
    g_pool->pixel_rects[h.index * 4 + 3] = ph;
}

void tc_viewport_get_pixel_rect(tc_viewport_handle h, int* px, int* py, int* pw, int* ph) {
    if (!handle_alive(h)) return;
    if (px) *px = g_pool->pixel_rects[h.index * 4 + 0];
    if (py) *py = g_pool->pixel_rects[h.index * 4 + 1];
    if (pw) *pw = g_pool->pixel_rects[h.index * 4 + 2];
    if (ph) *ph = g_pool->pixel_rects[h.index * 4 + 3];
}

void tc_viewport_set_depth(tc_viewport_handle h, int depth) {
    if (!handle_alive(h)) return;
    g_pool->depths[h.index] = depth;
}

int tc_viewport_get_depth(tc_viewport_handle h) {
    if (!handle_alive(h)) return 0;
    return g_pool->depths[h.index];
}

void tc_viewport_set_pipeline(tc_viewport_handle h, tc_pipeline_handle pipeline) {
    if (!handle_alive(h)) return;
    g_pool->pipelines[h.index] = pipeline;
}

tc_pipeline_handle tc_viewport_get_pipeline(tc_viewport_handle h) {
    if (!handle_alive(h)) return TC_PIPELINE_HANDLE_INVALID;
    return g_pool->pipelines[h.index];
}

void tc_viewport_set_layer_mask(tc_viewport_handle h, uint64_t mask) {
    if (!handle_alive(h)) return;
    g_pool->layer_masks[h.index] = mask;
}

uint64_t tc_viewport_get_layer_mask(tc_viewport_handle h) {
    if (!handle_alive(h)) return 0;
    return g_pool->layer_masks[h.index];
}

void tc_viewport_set_enabled(tc_viewport_handle h, bool enabled) {
    if (!handle_alive(h)) return;
    g_pool->enabled[h.index] = enabled;
}

bool tc_viewport_get_enabled(tc_viewport_handle h) {
    if (!handle_alive(h)) return false;
    return g_pool->enabled[h.index];
}

void tc_viewport_set_scene(tc_viewport_handle h, tc_scene_handle scene) {
    if (!handle_alive(h)) return;
    g_pool->scenes[h.index] = scene;
}

tc_scene_handle tc_viewport_get_scene(tc_viewport_handle h) {
    if (!handle_alive(h)) return TC_SCENE_HANDLE_INVALID;
    return g_pool->scenes[h.index];
}

void tc_viewport_set_camera(tc_viewport_handle h, tc_component* camera) {
    if (!handle_alive(h)) return;
    g_pool->cameras[h.index] = camera;
    g_pool->camera_entities[h.index] = camera ? camera->owner : TC_ENTITY_HANDLE_INVALID;
}

tc_component* tc_viewport_get_camera(tc_viewport_handle h) {
    if (!handle_alive(h)) return NULL;

    tc_component* camera = g_pool->cameras[h.index];
    if (!camera) return NULL;

    tc_entity_handle owner = g_pool->camera_entities[h.index];
    if (!tc_entity_handle_valid(owner)) {
        g_pool->cameras[h.index] = NULL;
        g_pool->camera_entities[h.index] = TC_ENTITY_HANDLE_INVALID;
        return NULL;
    }

    return camera;
}

tc_entity_handle tc_viewport_get_camera_entity(tc_viewport_handle h) {
    if (!handle_alive(h)) return TC_ENTITY_HANDLE_INVALID;
    return g_pool->camera_entities[h.index];
}

void tc_viewport_set_input_mode(tc_viewport_handle h, const char* mode) {
    if (!handle_alive(h)) return;
    tc_strset(&g_pool->input_modes[h.index], mode);
}

const char* tc_viewport_get_input_mode(tc_viewport_handle h) {
    if (!handle_alive(h)) return NULL;
    return g_pool->input_modes[h.index];
}

void tc_viewport_set_managed_by(tc_viewport_handle h, const char* pipeline_name) {
    if (!handle_alive(h)) return;
    tc_strset(&g_pool->managed_by[h.index], pipeline_name);
}

const char* tc_viewport_get_managed_by(tc_viewport_handle h) {
    if (!handle_alive(h)) return NULL;
    return g_pool->managed_by[h.index];
}

void tc_viewport_set_block_input_in_editor(tc_viewport_handle h, bool block) {
    if (!handle_alive(h)) return;
    g_pool->block_input_in_editor[h.index] = block;
}

bool tc_viewport_get_block_input_in_editor(tc_viewport_handle h) {
    if (!handle_alive(h)) return false;
    return g_pool->block_input_in_editor[h.index];
}

void tc_viewport_set_input_manager(tc_viewport_handle h, tc_input_manager* manager) {
    if (!handle_alive(h)) return;
    g_pool->input_managers[h.index] = manager;
}

tc_input_manager* tc_viewport_get_input_manager(tc_viewport_handle h) {
    if (!handle_alive(h)) return NULL;
    return g_pool->input_managers[h.index];
}

void tc_viewport_update_pixel_rect(tc_viewport_handle h, int display_width, int display_height) {
    if (!handle_alive(h)) return;

    float x = g_pool->rects[h.index * 4 + 0];
    float y = g_pool->rects[h.index * 4 + 1];
    float w = g_pool->rects[h.index * 4 + 2];
    float height = g_pool->rects[h.index * 4 + 3];

    g_pool->pixel_rects[h.index * 4 + 0] = (int)(x * display_width);
    g_pool->pixel_rects[h.index * 4 + 1] = (int)(y * display_height);
    g_pool->pixel_rects[h.index * 4 + 2] = (int)(w * display_width);
    g_pool->pixel_rects[h.index * 4 + 3] = (int)(height * display_height);
}

void tc_viewport_set_internal_entities(tc_viewport_handle h, tc_entity_handle ent) {
    if (!handle_alive(h)) return;
    g_pool->internal_entities[h.index] = ent;
}

tc_entity_handle tc_viewport_get_internal_entities(tc_viewport_handle h) {
    if (!handle_alive(h)) return TC_ENTITY_HANDLE_INVALID;
    return g_pool->internal_entities[h.index];
}

bool tc_viewport_has_internal_entities(tc_viewport_handle h) {
    if (!handle_alive(h)) return false;
    return tc_entity_handle_valid(g_pool->internal_entities[h.index]);
}

tc_viewport_handle tc_viewport_get_display_next(tc_viewport_handle h) {
    if (!handle_alive(h)) return TC_VIEWPORT_HANDLE_INVALID;
    return g_pool->display_nexts[h.index];
}

tc_viewport_handle tc_viewport_get_display_prev(tc_viewport_handle h) {
    if (!handle_alive(h)) return TC_VIEWPORT_HANDLE_INVALID;
    return g_pool->display_prevs[h.index];
}

void tc_viewport_set_display_next(tc_viewport_handle h, tc_viewport_handle next) {
    if (!handle_alive(h)) return;
    g_pool->display_nexts[h.index] = next;
}

void tc_viewport_set_display_prev(tc_viewport_handle h, tc_viewport_handle prev) {
    if (!handle_alive(h)) return;
    g_pool->display_prevs[h.index] = prev;
}
