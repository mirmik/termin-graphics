// tc_render_target.h - Render target: scene + camera + pipeline → FBO
#ifndef TC_RENDER_TARGET_H
#define TC_RENDER_TARGET_H

#include <tc_types.h>
#include "tc_value.h"
#include "render/tc_render_target_pool.h"
#include "render/tc_pipeline_pool.h"
#include "core/tc_entity_pool.h"
#include "core/tc_scene_pool.h"
#include "tgfx/resources/tc_texture.h"

#ifdef __cplusplus
extern "C" {
#endif

// Render targets are backed by 2D GPU textures. Keep the public C boundary
// within the maximum 2D extent supported by our portable render-target
// contract; individual backends still enforce their device-specific limit.
#define TC_RENDER_TARGET_MAX_DIMENSION 16384

typedef enum tc_render_target_kind {
    TC_RENDER_TARGET_TEXTURE_2D = 0,
    TC_RENDER_TARGET_XR_STEREO = 1,
} tc_render_target_kind;

TC_API tc_render_target_handle tc_render_target_new(const char* name);
TC_API void tc_render_target_free(tc_render_target_handle h);
TC_API bool tc_render_target_alive(tc_render_target_handle h);

TC_API bool tc_render_target_kind_from_string(const char* name, tc_render_target_kind* out_kind);
TC_API const char* tc_render_target_kind_to_string(tc_render_target_kind kind);
TC_API void tc_render_target_set_kind(tc_render_target_handle h, tc_render_target_kind kind);
TC_API tc_render_target_kind tc_render_target_get_kind(tc_render_target_handle h);

TC_API void tc_render_target_set_name(tc_render_target_handle h, const char* name);
TC_API const char* tc_render_target_get_name(tc_render_target_handle h);

TC_API void tc_render_target_set_width(tc_render_target_handle h, int width);
TC_API int tc_render_target_get_width(tc_render_target_handle h);

TC_API void tc_render_target_set_height(tc_render_target_handle h, int height);
TC_API int tc_render_target_get_height(tc_render_target_handle h);

TC_API void tc_render_target_set_dynamic_resolution(tc_render_target_handle h, bool dynamic_resolution);
TC_API bool tc_render_target_get_dynamic_resolution(tc_render_target_handle h);

TC_API bool tc_render_target_format_from_string(const char* name, tc_texture_format* out_format);
TC_API const char* tc_render_target_format_to_string(tc_texture_format format);

TC_API void tc_render_target_set_color_format(tc_render_target_handle h, tc_texture_format format);
TC_API tc_texture_format tc_render_target_get_color_format(tc_render_target_handle h);

TC_API void tc_render_target_set_depth_format(tc_render_target_handle h, tc_texture_format format);
TC_API tc_texture_format tc_render_target_get_depth_format(tc_render_target_handle h);

TC_API void tc_render_target_set_clear_color_enabled(tc_render_target_handle h, bool enabled);
TC_API bool tc_render_target_get_clear_color_enabled(tc_render_target_handle h);
TC_API void tc_render_target_set_clear_color_value(tc_render_target_handle h, float r, float g, float b, float a);
TC_API void tc_render_target_get_clear_color_value(tc_render_target_handle h, float out_rgba[4]);

TC_API void tc_render_target_set_clear_depth_enabled(tc_render_target_handle h, bool enabled);
TC_API bool tc_render_target_get_clear_depth_enabled(tc_render_target_handle h);
TC_API void tc_render_target_set_clear_depth_value(tc_render_target_handle h, float value);
TC_API float tc_render_target_get_clear_depth_value(tc_render_target_handle h);

// --- Owned tc_textures -----------------------------------------------------
//
// Render targets own a color + depth tc_texture pair. Both are GPU-only
// (tc_texture_storage_kind == GPU_FIRST) — no CPU pixel blob; the GPU
// image is allocated by IRenderDevice::ensure_tc_texture when the bridge
// first wraps the texture, and is rebuilt on size/format change because
// `set_width / set_height` bump the tc_texture's `header.version`.
//
// Defaults:
//   color: RGBA16F, usage = SAMPLED | COLOR_ATTACHMENT | COPY_SRC | COPY_DST
//   depth: DEPTH32F, usage = SAMPLED | DEPTH_ATTACHMENT
//
// Textures are created lazily in `tc_render_target_ensure_textures` —
// before that call `get_color_texture` / `get_depth_texture` return
// invalid handles. Most call-sites can just call `ensure_textures` once
// after configuring the render target and then use the getters.
TC_API void tc_render_target_ensure_textures(tc_render_target_handle h);
TC_API tc_texture_handle tc_render_target_get_color_texture(tc_render_target_handle h);
TC_API tc_texture_handle tc_render_target_get_depth_texture(tc_render_target_handle h);

TC_API void tc_render_target_set_scene(tc_render_target_handle h, tc_scene_handle scene);
TC_API tc_scene_handle tc_render_target_get_scene(tc_render_target_handle h);

// Component pointers passed to setters are validated but never retained or
// stored. The target stores the owning entity handle and resolves the current
// component from that entity on every getter call. Components may belong to
// the target scene or to a registered scene-less pool (editor/runtime helper
// entities); components owned by another scene are rejected. Changing scene
// clears both component bindings.
TC_API void tc_render_target_set_camera(tc_render_target_handle h, tc_component* camera);
TC_API tc_component* tc_render_target_get_camera(tc_render_target_handle h);
TC_API tc_entity_handle tc_render_target_get_camera_entity(tc_render_target_handle h);

TC_API void tc_render_target_set_xr_origin(tc_render_target_handle h, tc_component* xr_origin);
TC_API tc_component* tc_render_target_get_xr_origin(tc_render_target_handle h);
TC_API tc_entity_handle tc_render_target_get_xr_origin_entity(tc_render_target_handle h);

TC_API void tc_render_target_set_pipeline(tc_render_target_handle h, tc_pipeline_handle pipeline);
TC_API tc_pipeline_handle tc_render_target_get_pipeline(tc_render_target_handle h);

TC_API void tc_render_target_set_layer_mask(tc_render_target_handle h, uint64_t mask);
TC_API uint64_t tc_render_target_get_layer_mask(tc_render_target_handle h);

TC_API void tc_render_target_set_enabled(tc_render_target_handle h, bool enabled);
TC_API bool tc_render_target_get_enabled(tc_render_target_handle h);

TC_API void tc_render_target_set_locked(tc_render_target_handle h, bool locked);
TC_API bool tc_render_target_get_locked(tc_render_target_handle h);

// pipeline_params: dict of slot_name → rt_name (tc_value type TC_VALUE_DICT)
// Returns NULL if no params set. Caller does NOT own the returned pointer.
TC_API const tc_value* tc_render_target_get_pipeline_params(tc_render_target_handle h);
// Copies the dict. Pass NULL to clear.
TC_API void tc_render_target_set_pipeline_params(tc_render_target_handle h, const tc_value* dict);

#ifdef __cplusplus
}
#endif

#endif
