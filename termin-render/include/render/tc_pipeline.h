#ifndef TC_PIPELINE_H
#define TC_PIPELINE_H

#include <render/tc_pass.h>
#include <render/tc_pipeline_pool.h>
#include <render/tc_render_pipeline.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct tc_pipeline tc_pipeline;

struct tc_pipeline {
    char* name;
    tc_render_pipeline_handle resource;
    tc_pass** passes;
    tc_pass_deleter* pass_deleters;
    size_t pass_count;
    size_t pass_capacity;
    void* cached_frame_graph;
    void* render_cache;
    void (*render_cache_destructor)(void*);
    bool dirty;
};

// Create a mutable runtime-only collection. It has no canonical template;
// tc_pipeline_get_resource() returns an invalid handle.
TC_API tc_pipeline_handle tc_pipeline_create(const char* name);
// Instantiate a mutable execution collection that strongly retains the
// canonical immutable/versioned template for its complete lifetime.
TC_API tc_pipeline_handle tc_pipeline_create_from_resource(tc_render_pipeline_handle resource);
TC_API void tc_pipeline_destroy(tc_pipeline_handle h);
TC_API tc_pipeline* tc_pipeline_get_ptr(tc_pipeline_handle h);
TC_API tc_render_pipeline_handle tc_pipeline_get_resource(tc_pipeline_handle h);
TC_API bool tc_pipeline_adopt_pass(
    tc_pipeline_handle h,
    tc_pass* pass,
    tc_pass_deleter deleter
);
TC_API bool tc_pipeline_adopt_pass_before(
    tc_pipeline_handle h,
    tc_pass* pass,
    tc_pass_deleter deleter,
    tc_pass* before
);
TC_API bool tc_pipeline_move_pass_before(
    tc_pipeline_handle h,
    tc_pass* pass,
    tc_pass* before
);
TC_API void tc_pipeline_remove_pass(tc_pipeline_handle h, tc_pass* pass);
TC_API size_t tc_pipeline_remove_passes_by_name(tc_pipeline_handle h, const char* name);
TC_API tc_pass* tc_pipeline_get_pass(tc_pipeline_handle h, const char* name);
TC_API tc_pass* tc_pipeline_get_pass_at(tc_pipeline_handle h, size_t index);
TC_API bool tc_pipeline_replace_pass_at(
    tc_pipeline_handle h,
    size_t index,
    tc_pass* replacement,
    tc_pass_deleter deleter
);
// Publish a preconstructed replacement without destroying the previous pass.
// No storage allocation or user pass callback occurs in this operation. The
// caller owns the detached previous pass and must destroy it with the returned
// deleter after the complete batch has been published.
TC_API bool tc_pipeline_exchange_pass_at_checked(
    tc_pipeline_handle h,
    size_t index,
    tc_pass* expected,
    tc_pass* replacement,
    tc_pass_deleter replacement_deleter,
    tc_pass_deleter* expected_deleter
);
TC_API size_t tc_pipeline_pass_count(tc_pipeline_handle h);
TC_API const char* tc_pipeline_get_name(tc_pipeline_handle h);
TC_API void tc_pipeline_set_name(tc_pipeline_handle h, const char* name);
TC_API void* tc_pipeline_get_render_cache(tc_pipeline_handle h);
TC_API void tc_pipeline_set_render_cache(tc_pipeline_handle h, void* cache, void (*destructor)(void*));
TC_API bool tc_pipeline_is_dirty(tc_pipeline_handle h);
TC_API void tc_pipeline_mark_dirty(tc_pipeline_handle h);
TC_API void tc_pipeline_clear_dirty(tc_pipeline_handle h);
TC_API struct tc_frame_graph* tc_pipeline_get_frame_graph(tc_pipeline_handle h);

typedef bool (*tc_pipeline_pass_iter_fn)(tc_pipeline_handle h, tc_pass* pass, size_t index, void* user_data);

TC_API void tc_pipeline_foreach(tc_pipeline_handle h, tc_pipeline_pass_iter_fn callback, void* user_data);
TC_API size_t tc_pipeline_collect_specs(tc_pipeline_handle h, void* out_specs, size_t max_count);

#ifdef __cplusplus
}
#endif

#endif
