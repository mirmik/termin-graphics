#ifndef TCPLOT_RETAINED_SCENE_RENDERER2D_H
#define TCPLOT_RETAINED_SCENE_RENDERER2D_H

#include <stdint.h>

#include <termin_visual_scene/tc_visual_scene.h>

#include "tcplot/tcplot_api.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct tc_retained_scene_renderer2d tc_retained_scene_renderer2d;

typedef struct tc_retained_scene_renderer2d_timings {
    double paint_ms;
    double freeze_ms;
    /* CPU time spent recording/submitting draw commands, not GPU wall time. */
    double gpu_submit_ms;
    double total_ms;
} tc_retained_scene_renderer2d_timings;

/*
 * Creates an offscreen renderer for a retained visual scene. Both borrowed
 * arguments must outlive the renderer. gpu_host is a tcplot::GpuHost* kept
 * opaque here so this header remains usable from a C ABI consumer.
 */
TCPLOT_API tc_retained_scene_renderer2d* tc_retained_scene_renderer2d_create(void* gpu_host,
                                                                             tc_visual_scene_handle scene);

TCPLOT_API void tc_retained_scene_renderer2d_destroy(tc_retained_scene_renderer2d* renderer);

TCPLOT_API void tc_retained_scene_renderer2d_set_clear_color(
    tc_retained_scene_renderer2d* renderer, float r, float g, float b, float a);

TCPLOT_API int tc_retained_scene_renderer2d_set_msaa_samples(tc_retained_scene_renderer2d* renderer, int samples);
TCPLOT_API int tc_retained_scene_renderer2d_msaa_samples(const tc_retained_scene_renderer2d* renderer);

/* Returns the tgfx texture handle id, or zero after a logged failure. */
TCPLOT_API uint32_t tc_retained_scene_renderer2d_render(tc_retained_scene_renderer2d* renderer, int width, int height);

/* Snapshot for the last successful render call. */
TCPLOT_API int tc_retained_scene_renderer2d_last_timings(const tc_retained_scene_renderer2d* renderer,
                                                         tc_retained_scene_renderer2d_timings* out_timings);

TCPLOT_API void tc_retained_scene_renderer2d_release_gpu(tc_retained_scene_renderer2d* renderer);

#ifdef __cplusplus
}
#endif

#endif
