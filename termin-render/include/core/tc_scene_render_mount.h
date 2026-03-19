// tc_scene_render_mount.h - Render mount extension (scene pipelines + viewport configs)
#ifndef TC_SCENE_RENDER_MOUNT_H
#define TC_SCENE_RENDER_MOUNT_H

#include "core/tc_scene_pool.h"
#include "core/tc_scene_extension.h"
#include "core/tc_scene_pipeline_template.h"
#include "tc_viewport_config.h"
#include "tc_render_target_config.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct tc_scene_render_mount {
    tc_spt_handle* pipeline_templates;
    size_t pipeline_template_count;
    size_t pipeline_template_capacity;

    tc_viewport_config* viewport_configs;
    size_t viewport_config_count;
    size_t viewport_config_capacity;

    tc_render_target_config* render_target_configs;
    size_t render_target_config_count;
    size_t render_target_config_capacity;
} tc_scene_render_mount;

// Register builtin render-mount extension type in scene-extension registry.
// Safe to call multiple times.
TC_API void tc_scene_render_mount_extension_init(void);

// Access render-mount extension instance from scene.
TC_API tc_scene_render_mount* tc_scene_render_mount_get(tc_scene_handle scene);

// Ensure render-mount extension is attached to scene.
TC_API bool tc_scene_render_mount_ensure(tc_scene_handle scene);

// Render-mount scene API (moved from tc_scene.h)
TC_API void tc_scene_add_viewport_config(tc_scene_handle h, const tc_viewport_config* config);
TC_API void tc_scene_remove_viewport_config(tc_scene_handle h, size_t index);
TC_API void tc_scene_clear_viewport_configs(tc_scene_handle h);
TC_API size_t tc_scene_viewport_config_count(tc_scene_handle h);
TC_API tc_viewport_config* tc_scene_viewport_config_at(tc_scene_handle h, size_t index);

TC_API void tc_scene_add_render_target_config(tc_scene_handle h, const tc_render_target_config* config);
TC_API void tc_scene_remove_render_target_config(tc_scene_handle h, size_t index);
TC_API void tc_scene_clear_render_target_configs(tc_scene_handle h);
TC_API size_t tc_scene_render_target_config_count(tc_scene_handle h);
TC_API tc_render_target_config* tc_scene_render_target_config_at(tc_scene_handle h, size_t index);

TC_API void tc_scene_add_pipeline_template(tc_scene_handle h, tc_spt_handle spt);
TC_API void tc_scene_remove_pipeline_template(tc_scene_handle h, tc_spt_handle spt);
TC_API void tc_scene_clear_pipeline_templates(tc_scene_handle h);
TC_API size_t tc_scene_pipeline_template_count(tc_scene_handle h);
TC_API tc_spt_handle tc_scene_pipeline_template_at(tc_scene_handle h, size_t index);

#ifdef __cplusplus
}
#endif

#endif // TC_SCENE_RENDER_MOUNT_H
