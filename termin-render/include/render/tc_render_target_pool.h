// tc_render_target_pool.h - Pool-based handle for render targets
#ifndef TC_RENDER_TARGET_POOL_H
#define TC_RENDER_TARGET_POOL_H

#include <tc_types.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t index;
    uint32_t generation;
} tc_render_target_handle;

#ifdef __cplusplus
    #define TC_RENDER_TARGET_HANDLE_INVALID (tc_render_target_handle{0xFFFFFFFF, 0})
#else
    #define TC_RENDER_TARGET_HANDLE_INVALID ((tc_render_target_handle){0xFFFFFFFF, 0})
#endif

static inline bool tc_render_target_handle_valid(tc_render_target_handle h) {
    return h.index != 0xFFFFFFFF;
}

static inline bool tc_render_target_handle_eq(tc_render_target_handle a, tc_render_target_handle b) {
    return a.index == b.index && a.generation == b.generation;
}

TC_API void tc_render_target_pool_init(void);
TC_API void tc_render_target_pool_shutdown(void);
TC_API tc_render_target_handle tc_render_target_pool_alloc(const char* name);
TC_API void tc_render_target_pool_free(tc_render_target_handle h);
TC_API bool tc_render_target_pool_alive(tc_render_target_handle h);
TC_API size_t tc_render_target_pool_count(void);

typedef bool (*tc_render_target_pool_iter_fn)(tc_render_target_handle h, void* user_data);

TC_API void tc_render_target_pool_foreach(tc_render_target_pool_iter_fn callback, void* user_data);

#ifdef __cplusplus
}
#endif

#endif
