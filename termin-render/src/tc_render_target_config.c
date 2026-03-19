// tc_render_target_config.c - Render target configuration implementation
#include "tc_render_target_config.h"
#include <tcbase/tgfx_intern_string.h>
#include <string.h>

void tc_render_target_config_init(tc_render_target_config* config) {
    if (!config) return;
    memset(config, 0, sizeof(tc_render_target_config));
    config->width = 512;
    config->height = 512;
    config->layer_mask = 0xFFFFFFFFFFFFFFFFULL;
    config->enabled = true;
}

void tc_render_target_config_copy(tc_render_target_config* dst, const tc_render_target_config* src) {
    if (!dst || !src) return;
    dst->name = src->name ? tgfx_intern_string(src->name) : NULL;
    dst->camera_uuid = src->camera_uuid ? tgfx_intern_string(src->camera_uuid) : NULL;
    dst->width = src->width;
    dst->height = src->height;
    dst->pipeline_uuid = src->pipeline_uuid ? tgfx_intern_string(src->pipeline_uuid) : NULL;
    dst->pipeline_name = src->pipeline_name ? tgfx_intern_string(src->pipeline_name) : NULL;
    dst->layer_mask = src->layer_mask;
    dst->enabled = src->enabled;
}
