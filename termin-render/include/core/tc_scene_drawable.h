#ifndef TC_SCENE_DRAWABLE_H
#define TC_SCENE_DRAWABLE_H

#include "core/tc_scene.h"

#ifdef __cplusplus
extern "C" {
#endif

TC_API void tc_scene_foreach_drawable(
    tc_scene_handle h,
    tc_component_iter_fn callback,
    void* user_data,
    int filter_flags,
    uint64_t layer_mask
);

#ifdef __cplusplus
}
#endif

#endif
