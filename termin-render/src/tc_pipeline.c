#include <render/tc_pipeline.h>
#include <render/tc_pipeline_pool.h>
#include <render/tc_frame_graph.h>
#include <tc_pipeline_registry.h>
#include <tcbase/tc_log.h>
#ifdef TERMIN_RENDER_ENABLE_TEST_HOOKS
#include "tc_pipeline_test_hooks.h"
#endif
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define MAX_PIPELINE_POOL_SIZE 256
#define PIPELINE_INITIAL_POOL_CAPACITY 16
#define PIPELINE_INITIAL_PASS_CAPACITY 8

typedef struct {
    uint32_t* generations;
    bool* alive;
    tc_pipeline* pipelines;
    uint32_t* free_stack;
    size_t free_count;
    size_t capacity;
    size_t count;
} PipelinePool;

static PipelinePool* g_pipeline_pool = NULL;
static bool g_pipeline_registry_initialized = false;

#ifdef TERMIN_RENDER_ENABLE_TEST_HOOKS
static size_t g_pipeline_storage_allocations_before_failure = SIZE_MAX;

void tc_pipeline_test_fail_storage_allocation_after(size_t successful_allocations) {
    g_pipeline_storage_allocations_before_failure = successful_allocations;
}

void tc_pipeline_test_reset_storage_allocator(void) {
    g_pipeline_storage_allocations_before_failure = SIZE_MAX;
}
#endif

static bool pipeline_storage_allocation_should_fail(void) {
#ifdef TERMIN_RENDER_ENABLE_TEST_HOOKS
    if (g_pipeline_storage_allocations_before_failure == 0) {
        return true;
    }
    if (g_pipeline_storage_allocations_before_failure != SIZE_MAX) {
        --g_pipeline_storage_allocations_before_failure;
    }
#endif
    return false;
}

static void* pipeline_storage_malloc(size_t size) {
    if (pipeline_storage_allocation_should_fail()) return NULL;
    return malloc(size);
}

static void* pipeline_storage_calloc(size_t count, size_t size) {
    if (pipeline_storage_allocation_should_fail()) return NULL;
    return calloc(count, size);
}

static void destroy_owned_pass(tc_pass* pass, tc_pass_deleter deleter) {
    if (!pass) return;
    pass->owner_pipeline = TC_PIPELINE_HANDLE_INVALID;
    pass->deleter = NULL;
    tc_pass_destroy(pass);
    if (deleter) {
        deleter(pass);
    } else {
        tc_log_error("[tc_pipeline] owned pass has no deleter");
    }
}

static void destroy_all_pipeline_passes(tc_pipeline* pipeline) {
    if (!pipeline) return;
    const size_t count = pipeline->pass_count;
    pipeline->pass_count = 0;
    for (size_t i = 0; i < count; ++i) {
        tc_pass* pass = pipeline->passes[i];
        tc_pass_deleter deleter = pipeline->pass_deleters[i];
        pipeline->passes[i] = NULL;
        pipeline->pass_deleters[i] = NULL;
        destroy_owned_pass(pass, deleter);
    }
}

static void destroy_pipeline_slot(tc_pipeline* pipeline) {
    if (!pipeline) return;

    void* render_cache = pipeline->render_cache;
    void (*render_cache_destructor)(void*) = pipeline->render_cache_destructor;
    pipeline->render_cache = NULL;
    pipeline->render_cache_destructor = NULL;
    if (render_cache) {
        if (render_cache_destructor) {
            render_cache_destructor(render_cache);
        } else {
            tc_log_error("[tc_pipeline] render cache has no destructor");
        }
    }

    tc_frame_graph* frame_graph = (tc_frame_graph*)pipeline->cached_frame_graph;
    pipeline->cached_frame_graph = NULL;
    if (frame_graph) {
        tc_frame_graph_destroy(frame_graph);
    }

    destroy_all_pipeline_passes(pipeline);
    free(pipeline->passes);
    free(pipeline->pass_deleters);
    free(pipeline->name);
    memset(pipeline, 0, sizeof(*pipeline));
}

static char* tc_pipeline_strdup(const char* s) {
    if (s == NULL) return NULL;
    size_t len = strlen(s) + 1;
    char* copy = (char*)malloc(len);
    if (copy) memcpy(copy, s, len);
    return copy;
}

static void tc_pipeline_strset(char** dest, const char* src) {
    free(*dest);
    *dest = tc_pipeline_strdup(src);
}

void tc_pipeline_registry_init(void) {
    if (g_pipeline_registry_initialized) return;
    g_pipeline_registry_initialized = true;
}

void tc_pipeline_registry_shutdown(void) {
    if (!g_pipeline_registry_initialized) return;
    g_pipeline_registry_initialized = false;
}

void tc_pipeline_pool_init(void) {
    if (g_pipeline_pool) {
        tc_log_warn("[tc_pipeline_pool] already initialized");
        return;
    }

    PipelinePool* pool = (PipelinePool*)pipeline_storage_calloc(1, sizeof(PipelinePool));
    if (!pool) {
        tc_log_error("[tc_pipeline_pool] allocation failed");
        return;
    }

    size_t cap = PIPELINE_INITIAL_POOL_CAPACITY;
    pool->generations = (uint32_t*)pipeline_storage_calloc(cap, sizeof(uint32_t));
    pool->alive = (bool*)pipeline_storage_calloc(cap, sizeof(bool));
    pool->pipelines = (tc_pipeline*)pipeline_storage_calloc(cap, sizeof(tc_pipeline));
    pool->free_stack = (uint32_t*)pipeline_storage_malloc(cap * sizeof(uint32_t));
    if (!pool->generations || !pool->alive || !pool->pipelines || !pool->free_stack) {
        tc_log_error("[tc_pipeline_pool] storage allocation failed");
        free(pool->generations);
        free(pool->alive);
        free(pool->pipelines);
        free(pool->free_stack);
        free(pool);
        return;
    }
    for (size_t i = 0; i < cap; i++) {
        pool->free_stack[i] = (uint32_t)(cap - 1 - i);
    }
    pool->free_count = cap;
    pool->capacity = cap;
    pool->count = 0;

    g_pipeline_pool = pool;
    tc_pipeline_registry_init();
}

void tc_pipeline_pool_shutdown(void) {
    if (!g_pipeline_pool) {
        return;
    }

    for (size_t i = 0; i < g_pipeline_pool->capacity; i++) {
        if (g_pipeline_pool->alive[i]) {
            destroy_pipeline_slot(&g_pipeline_pool->pipelines[i]);
        }
    }

    free(g_pipeline_pool->generations);
    free(g_pipeline_pool->alive);
    free(g_pipeline_pool->pipelines);
    free(g_pipeline_pool->free_stack);
    free(g_pipeline_pool);
    g_pipeline_pool = NULL;

    tc_pipeline_registry_shutdown();
}

static bool pipeline_pool_grow(void) {
    size_t old_cap = g_pipeline_pool->capacity;
    size_t new_cap = old_cap * 2;
    if (new_cap > MAX_PIPELINE_POOL_SIZE) new_cap = MAX_PIPELINE_POOL_SIZE;
    if (new_cap <= old_cap) {
        tc_log_error("[tc_pipeline_pool] max capacity reached");
        return false;
    }

    uint32_t* generations = (uint32_t*)pipeline_storage_calloc(new_cap, sizeof(uint32_t));
    bool* alive = (bool*)pipeline_storage_calloc(new_cap, sizeof(bool));
    tc_pipeline* pipelines = (tc_pipeline*)pipeline_storage_calloc(new_cap, sizeof(tc_pipeline));
    uint32_t* free_stack = (uint32_t*)pipeline_storage_malloc(new_cap * sizeof(uint32_t));
    if (!generations || !alive || !pipelines || !free_stack) {
        tc_log_error("[tc_pipeline_pool] grow allocation failed");
        free(generations);
        free(alive);
        free(pipelines);
        free(free_stack);
        return false;
    }

    memcpy(generations, g_pipeline_pool->generations, old_cap * sizeof(uint32_t));
    memcpy(alive, g_pipeline_pool->alive, old_cap * sizeof(bool));
    memcpy(pipelines, g_pipeline_pool->pipelines, old_cap * sizeof(tc_pipeline));
    memcpy(free_stack, g_pipeline_pool->free_stack,
           g_pipeline_pool->free_count * sizeof(uint32_t));

    size_t free_count = g_pipeline_pool->free_count;
    for (size_t i = old_cap; i < new_cap; i++) {
        free_stack[free_count++] = (uint32_t)(new_cap - 1 - (i - old_cap));
    }

    free(g_pipeline_pool->generations);
    free(g_pipeline_pool->alive);
    free(g_pipeline_pool->pipelines);
    free(g_pipeline_pool->free_stack);
    g_pipeline_pool->generations = generations;
    g_pipeline_pool->alive = alive;
    g_pipeline_pool->pipelines = pipelines;
    g_pipeline_pool->free_stack = free_stack;
    g_pipeline_pool->free_count = free_count;
    g_pipeline_pool->capacity = new_cap;
    return true;
}

static inline bool pipeline_handle_alive(tc_pipeline_handle h) {
    if (!g_pipeline_pool) return false;
    if (h.index >= g_pipeline_pool->capacity) return false;
    return g_pipeline_pool->alive[h.index] && g_pipeline_pool->generations[h.index] == h.generation;
}

bool tc_pipeline_pool_alive(tc_pipeline_handle h) {
    return pipeline_handle_alive(h);
}

tc_pipeline_handle tc_pipeline_pool_alloc(const char* name) {
    if (!g_pipeline_pool) {
        tc_pipeline_pool_init();
        if (!g_pipeline_pool) {
            tc_log_error("[tc_pipeline_pool] unavailable after initialization failure");
            return TC_PIPELINE_HANDLE_INVALID;
        }
    }
    if (g_pipeline_pool->free_count == 0) {
        if (!pipeline_pool_grow()) {
            tc_log_error("[tc_pipeline_pool] no free slots");
            return TC_PIPELINE_HANDLE_INVALID;
        }
    }

    uint32_t idx = g_pipeline_pool->free_stack[--g_pipeline_pool->free_count];
    uint32_t gen = g_pipeline_pool->generations[idx];

    g_pipeline_pool->alive[idx] = true;
    tc_pipeline* p = &g_pipeline_pool->pipelines[idx];
    p->name = name ? tc_pipeline_strdup(name) : tc_pipeline_strdup("default");
    p->passes = NULL;
    p->pass_deleters = NULL;
    p->pass_count = 0;
    p->pass_capacity = 0;
    p->cached_frame_graph = NULL;
    p->render_cache = NULL;
    p->render_cache_destructor = NULL;
    p->dirty = true;

    g_pipeline_pool->count++;

    tc_pipeline_handle h = { idx, gen };
    return h;
}

tc_pipeline_handle tc_pipeline_create(const char* name) {
    return tc_pipeline_pool_alloc(name);
}

void tc_pipeline_pool_free(tc_pipeline_handle h) {
    if (!pipeline_handle_alive(h)) return;

    uint32_t idx = h.index;
    tc_pipeline* p = &g_pipeline_pool->pipelines[idx];

    destroy_pipeline_slot(p);

    g_pipeline_pool->alive[idx] = false;
    g_pipeline_pool->generations[idx]++;
    g_pipeline_pool->free_stack[g_pipeline_pool->free_count++] = idx;
    g_pipeline_pool->count--;
}

void tc_pipeline_destroy(tc_pipeline_handle h) {
    tc_pipeline_pool_free(h);
}

size_t tc_pipeline_pool_count(void) {
    return g_pipeline_pool ? g_pipeline_pool->count : 0;
}

void tc_pipeline_pool_foreach(tc_pipeline_pool_iter_fn callback, void* user_data) {
    if (!g_pipeline_pool || !callback) return;
    for (size_t i = 0; i < g_pipeline_pool->capacity; i++) {
        if (g_pipeline_pool->alive[i]) {
            tc_pipeline_handle h = { (uint32_t)i, g_pipeline_pool->generations[i] };
            if (!callback(h, user_data)) {
                break;
            }
        }
    }
}

tc_pipeline* tc_pipeline_get_ptr(tc_pipeline_handle h) {
    if (!pipeline_handle_alive(h)) return NULL;
    return &g_pipeline_pool->pipelines[h.index];
}

const char* tc_pipeline_get_name(tc_pipeline_handle h) {
    if (!pipeline_handle_alive(h)) return NULL;
    return g_pipeline_pool->pipelines[h.index].name;
}

void tc_pipeline_set_name(tc_pipeline_handle h, const char* name) {
    if (!pipeline_handle_alive(h)) return;
    tc_pipeline_strset(&g_pipeline_pool->pipelines[h.index].name, name);
}

void* tc_pipeline_get_render_cache(tc_pipeline_handle h) {
    if (!pipeline_handle_alive(h)) return NULL;
    return g_pipeline_pool->pipelines[h.index].render_cache;
}

void tc_pipeline_set_render_cache(tc_pipeline_handle h, void* cache, void (*destructor)(void*)) {
    if (!pipeline_handle_alive(h)) return;
    tc_pipeline* p = &g_pipeline_pool->pipelines[h.index];
    if (p->render_cache && p->render_cache_destructor) {
        p->render_cache_destructor(p->render_cache);
    }
    p->render_cache = cache;
    p->render_cache_destructor = destructor;
}

static bool pipeline_ensure_capacity(tc_pipeline* p) {
    if (p->pass_count >= p->pass_capacity) {
        size_t new_capacity = p->pass_capacity == 0 ? PIPELINE_INITIAL_PASS_CAPACITY : p->pass_capacity * 2;
        tc_pass** passes = (tc_pass**)realloc(p->passes, new_capacity * sizeof(tc_pass*));
        if (!passes) {
            tc_log_error("[tc_pipeline] failed to grow pass storage");
            return false;
        }
        p->passes = passes;
        tc_pass_deleter* deleters = (tc_pass_deleter*)realloc(
            p->pass_deleters,
            new_capacity * sizeof(tc_pass_deleter)
        );
        if (!deleters) {
            tc_log_error("[tc_pipeline] failed to grow pass deleter storage");
            return false;
        }
        p->pass_deleters = deleters;
        p->pass_capacity = new_capacity;
    }
    return true;
}

bool tc_pipeline_adopt_pass(
    tc_pipeline_handle h,
    tc_pass* pass,
    tc_pass_deleter deleter
) {
    if (!pipeline_handle_alive(h) || !pass || !deleter) {
        tc_log_error("[tc_pipeline] pass adoption requires a live pipeline, pass and deleter");
        return false;
    }
    tc_pipeline* p = &g_pipeline_pool->pipelines[h.index];

    if (tc_pipeline_handle_valid(pass->owner_pipeline)) {
        if (tc_pipeline_handle_eq(pass->owner_pipeline, h)) {
            tc_log(TC_LOG_ERROR, "tc_pipeline_adopt_pass: pass '%s' is already in this pipeline",
                   pass->pass_name ? pass->pass_name : "(unnamed)");
            return false;
        } else {
            tc_log(TC_LOG_ERROR, "tc_pipeline_adopt_pass: pass '%s' is already in another pipeline",
                   pass->pass_name ? pass->pass_name : "(unnamed)");
            return false;
        }
    }

    if (!pipeline_ensure_capacity(p)) return false;
    pass->owner_pipeline = h;
    pass->deleter = deleter;
    p->passes[p->pass_count++] = pass;
    p->pass_deleters[p->pass_count - 1] = deleter;
    p->dirty = true;
    return true;
}

bool tc_pipeline_adopt_pass_before(
    tc_pipeline_handle h,
    tc_pass* pass,
    tc_pass_deleter deleter,
    tc_pass* before
) {
    if (!pipeline_handle_alive(h) || !pass || !deleter) {
        tc_log_error("[tc_pipeline] pass adoption requires a live pipeline, pass and deleter");
        return false;
    }
    if (tc_pipeline_handle_valid(pass->owner_pipeline)) {
        tc_log_error("[tc_pipeline] cannot adopt a pass that already belongs to a pipeline");
        return false;
    }
    tc_pipeline* p = &g_pipeline_pool->pipelines[h.index];

    size_t insert_idx = 0;
    if (before) {
        insert_idx = p->pass_count;
        for (size_t i = 0; i < p->pass_count; i++) {
            if (p->passes[i] == before) {
                insert_idx = i;
                break;
            }
        }
        if (insert_idx == p->pass_count) {
            tc_log_error("[tc_pipeline] adoption target does not belong to the pipeline");
            return false;
        }
    }

    if (!pipeline_ensure_capacity(p)) return false;
    pass->owner_pipeline = h;
    pass->deleter = deleter;

    if (!before) {
        memmove(&p->passes[1], &p->passes[0], p->pass_count * sizeof(tc_pass*));
        memmove(&p->pass_deleters[1], &p->pass_deleters[0],
                p->pass_count * sizeof(tc_pass_deleter));
        p->passes[0] = pass;
        p->pass_deleters[0] = deleter;
        p->pass_count++;
        p->dirty = true;
        return true;
    }

    memmove(&p->passes[insert_idx + 1], &p->passes[insert_idx],
            (p->pass_count - insert_idx) * sizeof(tc_pass*));
    memmove(&p->pass_deleters[insert_idx + 1], &p->pass_deleters[insert_idx],
            (p->pass_count - insert_idx) * sizeof(tc_pass_deleter));
    p->passes[insert_idx] = pass;
    p->pass_deleters[insert_idx] = deleter;
    p->pass_count++;
    p->dirty = true;
    return true;
}

bool tc_pipeline_move_pass_before(
    tc_pipeline_handle h,
    tc_pass* pass,
    tc_pass* before
) {
    if (!pipeline_handle_alive(h) || !pass) return false;
    tc_pipeline* pipeline = &g_pipeline_pool->pipelines[h.index];
    size_t source = pipeline->pass_count;
    for (size_t i = 0; i < pipeline->pass_count; ++i) {
        if (pipeline->passes[i] == pass) {
            source = i;
            break;
        }
    }
    if (source == pipeline->pass_count) {
        tc_log_error("[tc_pipeline] cannot move a pass not owned by the pipeline");
        return false;
    }
    if (before == pass) return true;

    tc_pass_deleter deleter = pipeline->pass_deleters[source];
    memmove(&pipeline->passes[source], &pipeline->passes[source + 1],
            (pipeline->pass_count - source - 1) * sizeof(tc_pass*));
    memmove(&pipeline->pass_deleters[source], &pipeline->pass_deleters[source + 1],
            (pipeline->pass_count - source - 1) * sizeof(tc_pass_deleter));
    pipeline->pass_count--;

    size_t destination = pipeline->pass_count;
    if (before) {
        for (size_t i = 0; i < pipeline->pass_count; ++i) {
            if (pipeline->passes[i] == before) {
                destination = i;
                break;
            }
        }
        if (destination == pipeline->pass_count) {
            tc_log_error("[tc_pipeline] move target does not belong to the pipeline");
            memmove(&pipeline->passes[source + 1], &pipeline->passes[source],
                    (pipeline->pass_count - source) * sizeof(tc_pass*));
            memmove(&pipeline->pass_deleters[source + 1], &pipeline->pass_deleters[source],
                    (pipeline->pass_count - source) * sizeof(tc_pass_deleter));
            pipeline->passes[source] = pass;
            pipeline->pass_deleters[source] = deleter;
            pipeline->pass_count++;
            return false;
        }
    }

    memmove(&pipeline->passes[destination + 1], &pipeline->passes[destination],
            (pipeline->pass_count - destination) * sizeof(tc_pass*));
    memmove(&pipeline->pass_deleters[destination + 1], &pipeline->pass_deleters[destination],
            (pipeline->pass_count - destination) * sizeof(tc_pass_deleter));
    pipeline->passes[destination] = pass;
    pipeline->pass_deleters[destination] = deleter;
    pipeline->pass_count++;
    pipeline->dirty = true;
    return true;
}

void tc_pipeline_remove_pass(tc_pipeline_handle h, tc_pass* pass) {
    if (!pipeline_handle_alive(h) || !pass) return;
    tc_pipeline* p = &g_pipeline_pool->pipelines[h.index];

    size_t idx = p->pass_count;
    for (size_t i = 0; i < p->pass_count; i++) {
        if (p->passes[i] == pass) {
            idx = i;
            break;
        }
    }
    if (idx >= p->pass_count) return;

    memmove(&p->passes[idx], &p->passes[idx + 1],
            (p->pass_count - idx - 1) * sizeof(tc_pass*));
    tc_pass_deleter deleter = p->pass_deleters[idx];
    memmove(&p->pass_deleters[idx], &p->pass_deleters[idx + 1],
            (p->pass_count - idx - 1) * sizeof(tc_pass_deleter));
    p->pass_count--;
    p->dirty = true;
    destroy_owned_pass(pass, deleter);
}

size_t tc_pipeline_remove_passes_by_name(tc_pipeline_handle h, const char* name) {
    if (!pipeline_handle_alive(h) || !name) return 0;
    tc_pipeline* p = &g_pipeline_pool->pipelines[h.index];
    size_t removed_count = 0;

    for (size_t i = p->pass_count; i > 0; i--) {
        tc_pass* pass = p->passes[i - 1];
        if (pass && pass->pass_name && strcmp(pass->pass_name, name) == 0) {
            size_t idx = i - 1;
            memmove(&p->passes[idx], &p->passes[idx + 1],
                    (p->pass_count - idx - 1) * sizeof(tc_pass*));
            tc_pass_deleter deleter = p->pass_deleters[idx];
            memmove(&p->pass_deleters[idx], &p->pass_deleters[idx + 1],
                    (p->pass_count - idx - 1) * sizeof(tc_pass_deleter));
            p->pass_count--;
            destroy_owned_pass(pass, deleter);
            removed_count++;
        }
    }

    if (removed_count > 0) {
        p->dirty = true;
    }

    return removed_count;
}

tc_pass* tc_pipeline_get_pass(tc_pipeline_handle h, const char* name) {
    if (!pipeline_handle_alive(h) || !name) return NULL;
    tc_pipeline* p = &g_pipeline_pool->pipelines[h.index];
    for (size_t i = 0; i < p->pass_count; i++) {
        tc_pass* pass = p->passes[i];
        if (pass && pass->pass_name && strcmp(pass->pass_name, name) == 0) {
            return pass;
        }
    }
    return NULL;
}

tc_pass* tc_pipeline_get_pass_at(tc_pipeline_handle h, size_t index) {
    if (!pipeline_handle_alive(h)) return NULL;
    tc_pipeline* p = &g_pipeline_pool->pipelines[h.index];
    if (index >= p->pass_count) return NULL;
    return p->passes[index];
}

bool tc_pipeline_replace_pass_at(
    tc_pipeline_handle h,
    size_t index,
    tc_pass* replacement,
    tc_pass_deleter deleter
) {
    if (!pipeline_handle_alive(h) || !replacement || !deleter) return false;
    tc_pipeline* pipeline = &g_pipeline_pool->pipelines[h.index];
    if (index >= pipeline->pass_count) return false;
    if (pipeline->passes[index] == replacement) return false;
    if (tc_pipeline_handle_valid(replacement->owner_pipeline)) {
        tc_log(TC_LOG_ERROR, "tc_pipeline_replace_pass_at: replacement already belongs to a pipeline");
        return false;
    }

    tc_pass* previous = pipeline->passes[index];
    tc_pass_deleter previous_deleter = pipeline->pass_deleters[index];
    replacement->owner_pipeline = h;
    replacement->deleter = deleter;
    pipeline->passes[index] = replacement;
    pipeline->pass_deleters[index] = deleter;
    pipeline->dirty = true;
    destroy_owned_pass(previous, previous_deleter);
    return true;
}

size_t tc_pipeline_pass_count(tc_pipeline_handle h) {
    if (!pipeline_handle_alive(h)) return 0;
    return g_pipeline_pool->pipelines[h.index].pass_count;
}

void tc_pipeline_foreach(tc_pipeline_handle h, tc_pipeline_pass_iter_fn callback, void* user_data) {
    if (!pipeline_handle_alive(h) || !callback) return;
    tc_pipeline* p = &g_pipeline_pool->pipelines[h.index];
    for (size_t i = 0; i < p->pass_count; i++) {
        if (!callback(h, p->passes[i], i, user_data)) {
            break;
        }
    }
}

size_t tc_pipeline_collect_specs(tc_pipeline_handle h, void* out_specs, size_t max_count) {
    (void)h;
    (void)out_specs;
    (void)max_count;
    return 0;
}

size_t tc_pipeline_registry_count(void) {
    return tc_pipeline_pool_count();
}

tc_pipeline_handle tc_pipeline_registry_get_at(size_t index) {
    if (!g_pipeline_pool) return TC_PIPELINE_HANDLE_INVALID;
    size_t current = 0;
    for (size_t i = 0; i < g_pipeline_pool->capacity; i++) {
        if (g_pipeline_pool->alive[i]) {
            if (current == index) {
                tc_pipeline_handle h = { (uint32_t)i, g_pipeline_pool->generations[i] };
                return h;
            }
            current++;
        }
    }
    return TC_PIPELINE_HANDLE_INVALID;
}

tc_pipeline_handle tc_pipeline_registry_find_by_name(const char* name) {
    if (!name || !g_pipeline_pool) return TC_PIPELINE_HANDLE_INVALID;
    for (size_t i = 0; i < g_pipeline_pool->capacity; i++) {
        if (g_pipeline_pool->alive[i]) {
            tc_pipeline* p = &g_pipeline_pool->pipelines[i];
            if (p->name && strcmp(p->name, name) == 0) {
                tc_pipeline_handle h = { (uint32_t)i, g_pipeline_pool->generations[i] };
                return h;
            }
        }
    }
    return TC_PIPELINE_HANDLE_INVALID;
}

tc_pipeline_info* tc_pipeline_registry_get_all_info(size_t* count) {
    if (!count) return NULL;
    *count = 0;

    size_t pipeline_count = tc_pipeline_pool_count();
    if (pipeline_count == 0) return NULL;

    tc_pipeline_info* infos = (tc_pipeline_info*)malloc(pipeline_count * sizeof(tc_pipeline_info));
    if (!infos) return NULL;

    size_t idx = 0;
    for (size_t i = 0; i < g_pipeline_pool->capacity && idx < pipeline_count; i++) {
        if (g_pipeline_pool->alive[i]) {
            tc_pipeline* p = &g_pipeline_pool->pipelines[i];
            tc_pipeline_handle h = { (uint32_t)i, g_pipeline_pool->generations[i] };
            infos[idx].handle = h;
            infos[idx].name = p->name;
            infos[idx].pass_count = p->pass_count;
            idx++;
        }
    }

    *count = idx;
    return infos;
}

tc_pass_info* tc_pass_registry_get_all_instance_info(size_t* count) {
    if (!count) return NULL;
    *count = 0;
    if (!g_pipeline_pool) return NULL;

    size_t total_passes = 0;
    for (size_t i = 0; i < g_pipeline_pool->capacity; i++) {
        if (g_pipeline_pool->alive[i]) {
            total_passes += g_pipeline_pool->pipelines[i].pass_count;
        }
    }
    if (total_passes == 0) return NULL;

    tc_pass_info* infos = (tc_pass_info*)malloc(total_passes * sizeof(tc_pass_info));
    if (!infos) return NULL;

    size_t idx = 0;
    for (size_t i = 0; i < g_pipeline_pool->capacity; i++) {
        if (g_pipeline_pool->alive[i]) {
            tc_pipeline* p = &g_pipeline_pool->pipelines[i];
            tc_pipeline_handle h = { (uint32_t)i, g_pipeline_pool->generations[i] };
            for (size_t j = 0; j < p->pass_count; j++) {
                tc_pass* pass = p->passes[j];
                if (pass) {
                    infos[idx].ptr = pass;
                    infos[idx].pass_name = pass->pass_name;
                    infos[idx].type_name = tc_pass_type_name(pass);
                    infos[idx].pipeline_handle = h;
                    infos[idx].pipeline_name = p->name;
                    infos[idx].enabled = pass->enabled;
                    infos[idx].passthrough = pass->passthrough;
                    infos[idx].is_inplace = tc_pass_is_inplace(pass);
                    infos[idx].kind = (int)pass->kind;
                    idx++;
                }
            }
        }
    }

    *count = idx;
    return infos;
}

bool tc_pipeline_is_dirty(tc_pipeline_handle h) {
    if (!pipeline_handle_alive(h)) return true;
    return g_pipeline_pool->pipelines[h.index].dirty;
}

void tc_pipeline_mark_dirty(tc_pipeline_handle h) {
    if (!pipeline_handle_alive(h)) return;
    g_pipeline_pool->pipelines[h.index].dirty = true;
}

void tc_pipeline_clear_dirty(tc_pipeline_handle h) {
    if (!pipeline_handle_alive(h)) return;
    g_pipeline_pool->pipelines[h.index].dirty = false;
}

tc_frame_graph* tc_pipeline_get_frame_graph(tc_pipeline_handle h) {
    if (!pipeline_handle_alive(h)) return NULL;
    tc_pipeline* p = &g_pipeline_pool->pipelines[h.index];

    if (!p->dirty && p->cached_frame_graph) {
        return (tc_frame_graph*)p->cached_frame_graph;
    }

    if (p->cached_frame_graph) {
        tc_frame_graph_destroy((tc_frame_graph*)p->cached_frame_graph);
        p->cached_frame_graph = NULL;
    }

    p->cached_frame_graph = tc_frame_graph_build(h);
    p->dirty = false;
    return (tc_frame_graph*)p->cached_frame_graph;
}
