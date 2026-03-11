#ifndef TC_PIPELINE_H
#define TC_PIPELINE_H

#include <render/tc_pass.h>
#include <render/tc_pipeline_pool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct tc_pipeline tc_pipeline;

struct tc_pipeline {
    char* name;
    tc_pass** passes;
    size_t pass_count;
    size_t pass_capacity;
    void* cpp_owner;
    void* py_wrapper;
    void* cached_frame_graph;
    bool dirty;
};

TC_API tc_pipeline_handle tc_pipeline_create(const char* name);
TC_API void tc_pipeline_destroy(tc_pipeline_handle h);
TC_API tc_pipeline* tc_pipeline_get_ptr(tc_pipeline_handle h);
TC_API void tc_pipeline_add_pass(tc_pipeline_handle h, tc_pass* pass);
TC_API void tc_pipeline_insert_pass_before(tc_pipeline_handle h, tc_pass* pass, tc_pass* before);
TC_API void tc_pipeline_remove_pass(tc_pipeline_handle h, tc_pass* pass);
TC_API size_t tc_pipeline_remove_passes_by_name(tc_pipeline_handle h, const char* name);
TC_API tc_pass* tc_pipeline_get_pass(tc_pipeline_handle h, const char* name);
TC_API tc_pass* tc_pipeline_get_pass_at(tc_pipeline_handle h, size_t index);
TC_API size_t tc_pipeline_pass_count(tc_pipeline_handle h);
TC_API const char* tc_pipeline_get_name(tc_pipeline_handle h);
TC_API void tc_pipeline_set_name(tc_pipeline_handle h, const char* name);
TC_API void* tc_pipeline_get_cpp_owner(tc_pipeline_handle h);
TC_API void tc_pipeline_set_cpp_owner(tc_pipeline_handle h, void* owner);
TC_API void* tc_pipeline_get_py_wrapper(tc_pipeline_handle h);
TC_API void tc_pipeline_set_py_wrapper(tc_pipeline_handle h, void* wrapper);
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
