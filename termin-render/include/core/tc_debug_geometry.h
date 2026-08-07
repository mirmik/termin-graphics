#ifndef TC_DEBUG_GEOMETRY_H
#define TC_DEBUG_GEOMETRY_H

#include "core/tc_scene_pool.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint64_t tc_debug_geometry_type_id;

#define TC_DEBUG_GEOMETRY_TYPE_INVALID UINT64_C(0)

typedef struct tc_debug_geometry_type_desc {
    tc_debug_geometry_type_id type_id;
    const char* stable_id;
    const char* display_name;
    const char* category;
    bool default_enabled;
} tc_debug_geometry_type_desc;

typedef enum tc_debug_geometry_primitive_kind {
    TC_DEBUG_GEOMETRY_LINE = 1,
    TC_DEBUG_GEOMETRY_WIRE_SPHERE = 2,
    TC_DEBUG_GEOMETRY_WIRE_BOX = 3,
    TC_DEBUG_GEOMETRY_WIRE_CAPSULE = 4,
} tc_debug_geometry_primitive_kind;

typedef struct tc_debug_geometry_primitive {
    tc_debug_geometry_type_id type_id;
    tc_debug_geometry_primitive_kind kind;
    bool depth_test;
    uint16_t segments;
    float color[4];
    union {
        struct {
            float start[3];
            float end[3];
        } line;
        struct {
            float center[3];
            float radius;
        } sphere;
        struct {
            float center[3];
            float half_axis_x[3];
            float half_axis_y[3];
            float half_axis_z[3];
        } box;
        struct {
            float start[3];
            float end[3];
            float radius;
        } capsule;
    } data;
} tc_debug_geometry_primitive;

typedef struct tc_debug_geometry_drawer {
    tc_scene_handle scene;
    tc_debug_geometry_type_id type_id;
} tc_debug_geometry_drawer;

typedef struct tc_render_prepare_context tc_render_prepare_context;

TC_API tc_debug_geometry_type_id tc_debug_geometry_type_register(const char* stable_id,
                                                                 const char* display_name,
                                                                 const char* category,
                                                                 bool default_enabled);
TC_API bool tc_debug_geometry_type_unregister(tc_debug_geometry_type_id type_id);
TC_API bool tc_debug_geometry_type_registered(tc_debug_geometry_type_id type_id);
TC_API tc_debug_geometry_type_id tc_debug_geometry_type_find(const char* stable_id);
TC_API size_t tc_debug_geometry_type_count(void);
TC_API bool tc_debug_geometry_type_at(size_t index, tc_debug_geometry_type_desc* out_desc);
TC_API void tc_debug_geometry_registry_clear(void);

TC_API bool tc_scene_debug_geometry_enabled(tc_scene_handle scene, tc_debug_geometry_type_id type_id);
TC_API bool tc_scene_debug_geometry_set_enabled(tc_scene_handle scene, tc_debug_geometry_type_id type_id, bool enabled);

TC_API bool tc_render_prepare_context_debug_geometry(const tc_render_prepare_context* context,
                                                     tc_debug_geometry_type_id type_id,
                                                     tc_debug_geometry_drawer* out_drawer);
TC_API bool tc_debug_geometry_drawer_valid(const tc_debug_geometry_drawer* drawer);
TC_API bool tc_debug_geometry_drawer_line(const tc_debug_geometry_drawer* drawer,
                                          const float start[3],
                                          const float end[3],
                                          const float color[4],
                                          bool depth_test);
TC_API bool tc_debug_geometry_drawer_wire_sphere(const tc_debug_geometry_drawer* drawer,
                                                 const float center[3],
                                                 float radius,
                                                 const float color[4],
                                                 uint16_t segments,
                                                 bool depth_test);
TC_API bool tc_debug_geometry_drawer_wire_box(const tc_debug_geometry_drawer* drawer,
                                              const float center[3],
                                              const float half_axis_x[3],
                                              const float half_axis_y[3],
                                              const float half_axis_z[3],
                                              const float color[4],
                                              bool depth_test);
TC_API bool tc_debug_geometry_drawer_wire_capsule(const tc_debug_geometry_drawer* drawer,
                                                  const float start[3],
                                                  const float end[3],
                                                  float radius,
                                                  const float color[4],
                                                  uint16_t segments,
                                                  bool depth_test);

TC_API size_t tc_scene_debug_geometry_primitive_count(tc_scene_handle scene);
TC_API const tc_debug_geometry_primitive* tc_scene_debug_geometry_primitive_at(tc_scene_handle scene, size_t index);

// Collection is owned by render_mount and brackets render-lifecycle prepare.
TC_API void tc_scene_debug_geometry_begin_collection(tc_scene_handle scene);
TC_API void tc_scene_debug_geometry_end_collection(tc_scene_handle scene);
TC_API void tc_scene_debug_geometry_clear(tc_scene_handle scene);

#ifdef __cplusplus
}
#endif

#endif
