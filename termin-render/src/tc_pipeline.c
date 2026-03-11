#include <render/tc_pipeline.h>
#include <render/tc_pipeline_pool.h>
#include <render/tc_frame_graph.h>
#include <tc_pipeline_registry.h>
#include <tcbase/tc_log.h>
#include <stdlib.h>
#include <string.h>

#define MAX_PIPELINES 256
#define INITIAL_POOL_CAPACITY 16
#define INITIAL_PASS_CAPACITY 8

typedef struct {
    uint32_t* generations;
    bool* alive;
    tc_pipeline* pipelines;
    uint32_t* free_stack;
    size_t free_count;
    size_t capacity;
    size_t count;
} PipelinePool;

static PipelinePool* g_pool = NULL;
static bool g_pipeline_registry_initialized = false;

static char* tc_strdup(const char* s) {
    if (s == NULL) return NULL;
    size_t len = strlen(s) + 1;
    char* copy = (char*)malloc(len);
    if (copy) memcpy(copy, s, len);
    return copy;
}

static void tc_strset(char** dest, const char* src) {
    free(*dest);
    *dest = tc_strdup(src);
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
    if (g_pool) {
        tc_log_warn("[tc_pipeline_pool] already initialized");
        return;
    }

    g_pool = (PipelinePool*)calloc(1, sizeof(PipelinePool));
    if (!g_pool) {
        tc_log_error("[tc_pipeline_pool] allocation failed");
        return;
    }

    size_t cap = INITIAL_POOL_CAPACITY;
    g_pool->generations = (uint32_t*)calloc(cap, sizeof(uint32_t));
    g_pool->alive = (bool*)calloc(cap, sizeof(bool));
    g_pool->pipelines = (tc_pipeline*)calloc(cap, sizeof(tc_pipeline));
    g_pool->free_stack = (uint32_t*)malloc(cap * sizeof(uint32_t));
    for (size_t i = 0; i < cap; i++) {
        g_pool->free_stack[i] = (uint32_t)(cap - 1 - i);
    }
    g_pool->free_count = cap;
    g_pool->capacity = cap;
    g_pool->count = 0;

    tc_pipeline_registry_init();
}

void tc_pipeline_pool_shutdown(void) {
    if (!g_pool) {
        tc_log_warn("[tc_pipeline_pool] not initialized");
        return;
    }

    for (size_t i = 0; i < g_pool->capacity; i++) {
        if (g_pool->alive[i]) {
            tc_pipeline* p = &g_pool->pipelines[i];
            if (p->cached_frame_graph) {
                tc_frame_graph_destroy((tc_frame_graph*)p->cached_frame_graph);
                p->cached_frame_graph = NULL;
            }
            for (size_t j = 0; j < p->pass_count; j++) {
                tc_pass* pass = p->passes[j];
                if (pass) {
                    pass->owner_pipeline = TC_PIPELINE_HANDLE_INVALID;
                    tc_pass_release(pass);
                }
            }
            free(p->passes);
            free(p->name);
        }
    }

    free(g_pool->generations);
    free(g_pool->alive);
    free(g_pool->pipelines);
    free(g_pool->free_stack);
    free(g_pool);
    g_pool = NULL;

    tc_pipeline_registry_shutdown();
}

static void pool_grow(void) {
    size_t old_cap = g_pool->capacity;
    size_t new_cap = old_cap * 2;
    if (new_cap > MAX_PIPELINES) new_cap = MAX_PIPELINES;
    if (new_cap <= old_cap) {
        tc_log_error("[tc_pipeline_pool] max capacity reached");
        return;
    }

    g_pool->generations = realloc(g_pool->generations, new_cap * sizeof(uint32_t));
    g_pool->alive = realloc(g_pool->alive, new_cap * sizeof(bool));
    g_pool->pipelines = realloc(g_pool->pipelines, new_cap * sizeof(tc_pipeline));
    g_pool->free_stack = realloc(g_pool->free_stack, new_cap * sizeof(uint32_t));

    memset(g_pool->generations + old_cap, 0, (new_cap - old_cap) * sizeof(uint32_t));
    memset(g_pool->alive + old_cap, 0, (new_cap - old_cap) * sizeof(bool));
    memset(g_pool->pipelines + old_cap, 0, (new_cap - old_cap) * sizeof(tc_pipeline));

    for (size_t i = old_cap; i < new_cap; i++) {
        g_pool->free_stack[g_pool->free_count++] = (uint32_t)(new_cap - 1 - (i - old_cap));
    }

    g_pool->capacity = new_cap;
}

static inline bool handle_alive(tc_pipeline_handle h) {
    if (!g_pool) return false;
    if (h.index >= g_pool->capacity) return false;
    return g_pool->alive[h.index] && g_pool->generations[h.index] == h.generation;
}

bool tc_pipeline_pool_alive(tc_pipeline_handle h) {
    return handle_alive(h);
}

tc_pipeline_handle tc_pipeline_pool_alloc(const char* name) {
    if (!g_pool) {
        tc_pipeline_pool_init();
    }
    if (g_pool->free_count == 0) {
        pool_grow();
        if (g_pool->free_count == 0) {
            tc_log_error("[tc_pipeline_pool] no free slots");
            return TC_PIPELINE_HANDLE_INVALID;
        }
    }

    uint32_t idx = g_pool->free_stack[--g_pool->free_count];
    uint32_t gen = g_pool->generations[idx];

    g_pool->alive[idx] = true;
    tc_pipeline* p = &g_pool->pipelines[idx];
    p->name = name ? tc_strdup(name) : tc_strdup("default");
    p->passes = NULL;
    p->pass_count = 0;
    p->pass_capacity = 0;
    p->cpp_owner = NULL;
    p->py_wrapper = NULL;
    p->cached_frame_graph = NULL;
    p->dirty = true;

    g_pool->count++;

    tc_pipeline_handle h = { idx, gen };
    return h;
}

tc_pipeline_handle tc_pipeline_create(const char* name) {
    return tc_pipeline_pool_alloc(name);
}

void tc_pipeline_pool_free(tc_pipeline_handle h) {
    if (!handle_alive(h)) return;

    uint32_t idx = h.index;
    tc_pipeline* p = &g_pool->pipelines[idx];

    if (p->cached_frame_graph) {
        tc_frame_graph_destroy((tc_frame_graph*)p->cached_frame_graph);
        p->cached_frame_graph = NULL;
    }

    for (size_t i = 0; i < p->pass_count; i++) {
        tc_pass* pass = p->passes[i];
        if (pass) {
            pass->owner_pipeline = TC_PIPELINE_HANDLE_INVALID;
            tc_pass_release(pass);
        }
    }

    free(p->passes);
    free(p->name);
    memset(p, 0, sizeof(tc_pipeline));

    g_pool->alive[idx] = false;
    g_pool->generations[idx]++;
    g_pool->free_stack[g_pool->free_count++] = idx;
    g_pool->count--;
}

void tc_pipeline_destroy(tc_pipeline_handle h) {
    tc_pipeline_pool_free(h);
}

size_t tc_pipeline_pool_count(void) {
    return g_pool ? g_pool->count : 0;
}

void tc_pipeline_pool_foreach(tc_pipeline_pool_iter_fn callback, void* user_data) {
    if (!g_pool || !callback) return;
    for (size_t i = 0; i < g_pool->capacity; i++) {
        if (g_pool->alive[i]) {
            tc_pipeline_handle h = { (uint32_t)i, g_pool->generations[i] };
            if (!callback(h, user_data)) {
                break;
            }
        }
    }
}

tc_pipeline* tc_pipeline_get_ptr(tc_pipeline_handle h) {
    if (!handle_alive(h)) return NULL;
    return &g_pool->pipelines[h.index];
}

const char* tc_pipeline_get_name(tc_pipeline_handle h) {
    if (!handle_alive(h)) return NULL;
    return g_pool->pipelines[h.index].name;
}

void tc_pipeline_set_name(tc_pipeline_handle h, const char* name) {
    if (!handle_alive(h)) return;
    tc_strset(&g_pool->pipelines[h.index].name, name);
}

void* tc_pipeline_get_cpp_owner(tc_pipeline_handle h) {
    if (!handle_alive(h)) return NULL;
    return g_pool->pipelines[h.index].cpp_owner;
}

void tc_pipeline_set_cpp_owner(tc_pipeline_handle h, void* owner) {
    if (!handle_alive(h)) return;
    g_pool->pipelines[h.index].cpp_owner = owner;
}

void* tc_pipeline_get_py_wrapper(tc_pipeline_handle h) {
    if (!handle_alive(h)) return NULL;
    return g_pool->pipelines[h.index].py_wrapper;
}

void tc_pipeline_set_py_wrapper(tc_pipeline_handle h, void* wrapper) {
    if (!handle_alive(h)) return;
    g_pool->pipelines[h.index].py_wrapper = wrapper;
}

static void pipeline_ensure_capacity(tc_pipeline* p) {
    if (p->pass_count >= p->pass_capacity) {
        size_t new_capacity = p->pass_capacity == 0 ? INITIAL_PASS_CAPACITY : p->pass_capacity * 2;
        p->passes = (tc_pass**)realloc(p->passes, new_capacity * sizeof(tc_pass*));
        p->pass_capacity = new_capacity;
    }
}

void tc_pipeline_add_pass(tc_pipeline_handle h, tc_pass* pass) {
    if (!handle_alive(h) || !pass) return;
    tc_pipeline* p = &g_pool->pipelines[h.index];

    if (tc_pipeline_handle_valid(pass->owner_pipeline)) {
        if (tc_pipeline_handle_eq(pass->owner_pipeline, h)) {
            tc_log(TC_LOG_WARN, "tc_pipeline_add_pass: pass '%s' is already in this pipeline",
                   pass->pass_name ? pass->pass_name : "(unnamed)");
            return;
        } else {
            tc_log(TC_LOG_WARN, "tc_pipeline_add_pass: pass '%s' is already in another pipeline",
                   pass->pass_name ? pass->pass_name : "(unnamed)");
            tc_pipeline_remove_pass(pass->owner_pipeline, pass);
        }
    }

    pipeline_ensure_capacity(p);
    tc_pass_retain(pass);
    pass->owner_pipeline = h;
    p->passes[p->pass_count++] = pass;
    p->dirty = true;
}

void tc_pipeline_insert_pass_before(tc_pipeline_handle h, tc_pass* pass, tc_pass* before) {
    if (!handle_alive(h) || !pass) return;
    tc_pipeline* p = &g_pool->pipelines[h.index];

    pipeline_ensure_capacity(p);
    tc_pass_retain(pass);
    pass->owner_pipeline = h;

    if (!before) {
        memmove(&p->passes[1], &p->passes[0], p->pass_count * sizeof(tc_pass*));
        p->passes[0] = pass;
        p->pass_count++;
        p->dirty = true;
        return;
    }

    size_t insert_idx = p->pass_count;
    for (size_t i = 0; i < p->pass_count; i++) {
        if (p->passes[i] == before) {
            insert_idx = i;
            break;
        }
    }

    memmove(&p->passes[insert_idx + 1], &p->passes[insert_idx],
            (p->pass_count - insert_idx) * sizeof(tc_pass*));
    p->passes[insert_idx] = pass;
    p->pass_count++;
    p->dirty = true;
}

void tc_pipeline_remove_pass(tc_pipeline_handle h, tc_pass* pass) {
    if (!handle_alive(h) || !pass) return;
    tc_pipeline* p = &g_pool->pipelines[h.index];

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
    p->pass_count--;
    pass->owner_pipeline = TC_PIPELINE_HANDLE_INVALID;
    tc_pass_release(pass);
    p->dirty = true;
}

size_t tc_pipeline_remove_passes_by_name(tc_pipeline_handle h, const char* name) {
    if (!handle_alive(h) || !name) return 0;
    tc_pipeline* p = &g_pool->pipelines[h.index];
    size_t removed_count = 0;

    for (size_t i = p->pass_count; i > 0; i--) {
        tc_pass* pass = p->passes[i - 1];
        if (pass && pass->pass_name && strcmp(pass->pass_name, name) == 0) {
            size_t idx = i - 1;
            memmove(&p->passes[idx], &p->passes[idx + 1],
                    (p->pass_count - idx - 1) * sizeof(tc_pass*));
            p->pass_count--;
            pass->owner_pipeline = TC_PIPELINE_HANDLE_INVALID;
            tc_pass_release(pass);
            removed_count++;
        }
    }

    if (removed_count > 0) {
        p->dirty = true;
    }

    return removed_count;
}

tc_pass* tc_pipeline_get_pass(tc_pipeline_handle h, const char* name) {
    if (!handle_alive(h) || !name) return NULL;
    tc_pipeline* p = &g_pool->pipelines[h.index];
    for (size_t i = 0; i < p->pass_count; i++) {
        tc_pass* pass = p->passes[i];
        if (pass && pass->pass_name && strcmp(pass->pass_name, name) == 0) {
            return pass;
        }
    }
    return NULL;
}

tc_pass* tc_pipeline_get_pass_at(tc_pipeline_handle h, size_t index) {
    if (!handle_alive(h)) return NULL;
    tc_pipeline* p = &g_pool->pipelines[h.index];
    if (index >= p->pass_count) return NULL;
    return p->passes[index];
}

size_t tc_pipeline_pass_count(tc_pipeline_handle h) {
    if (!handle_alive(h)) return 0;
    return g_pool->pipelines[h.index].pass_count;
}

void tc_pipeline_foreach(tc_pipeline_handle h, tc_pipeline_pass_iter_fn callback, void* user_data) {
    if (!handle_alive(h) || !callback) return;
    tc_pipeline* p = &g_pool->pipelines[h.index];
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
    if (!g_pool) return TC_PIPELINE_HANDLE_INVALID;
    size_t current = 0;
    for (size_t i = 0; i < g_pool->capacity; i++) {
        if (g_pool->alive[i]) {
            if (current == index) {
                tc_pipeline_handle h = { (uint32_t)i, g_pool->generations[i] };
                return h;
            }
            current++;
        }
    }
    return TC_PIPELINE_HANDLE_INVALID;
}

tc_pipeline_handle tc_pipeline_registry_find_by_name(const char* name) {
    if (!name || !g_pool) return TC_PIPELINE_HANDLE_INVALID;
    for (size_t i = 0; i < g_pool->capacity; i++) {
        if (g_pool->alive[i]) {
            tc_pipeline* p = &g_pool->pipelines[i];
            if (p->name && strcmp(p->name, name) == 0) {
                tc_pipeline_handle h = { (uint32_t)i, g_pool->generations[i] };
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
    for (size_t i = 0; i < g_pool->capacity && idx < pipeline_count; i++) {
        if (g_pool->alive[i]) {
            tc_pipeline* p = &g_pool->pipelines[i];
            tc_pipeline_handle h = { (uint32_t)i, g_pool->generations[i] };
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
    if (!g_pool) return NULL;

    size_t total_passes = 0;
    for (size_t i = 0; i < g_pool->capacity; i++) {
        if (g_pool->alive[i]) {
            total_passes += g_pool->pipelines[i].pass_count;
        }
    }
    if (total_passes == 0) return NULL;

    tc_pass_info* infos = (tc_pass_info*)malloc(total_passes * sizeof(tc_pass_info));
    if (!infos) return NULL;

    size_t idx = 0;
    for (size_t i = 0; i < g_pool->capacity; i++) {
        if (g_pool->alive[i]) {
            tc_pipeline* p = &g_pool->pipelines[i];
            tc_pipeline_handle h = { (uint32_t)i, g_pool->generations[i] };
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
    if (!handle_alive(h)) return true;
    return g_pool->pipelines[h.index].dirty;
}

void tc_pipeline_mark_dirty(tc_pipeline_handle h) {
    if (!handle_alive(h)) return;
    g_pool->pipelines[h.index].dirty = true;
}

void tc_pipeline_clear_dirty(tc_pipeline_handle h) {
    if (!handle_alive(h)) return;
    g_pool->pipelines[h.index].dirty = false;
}

tc_frame_graph* tc_pipeline_get_frame_graph(tc_pipeline_handle h) {
    if (!handle_alive(h)) return NULL;
    tc_pipeline* p = &g_pool->pipelines[h.index];

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
