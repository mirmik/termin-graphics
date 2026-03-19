// tc_viewport_config.c - Viewport configuration implementation
#include "tc_viewport_config.h"
#include <tcbase/tgfx_intern_string.h>
#include <string.h>

void tc_viewport_config_init(tc_viewport_config* config) {
    if (!config) return;
    memset(config, 0, sizeof(tc_viewport_config));
    config->display_name = tgfx_intern_string("Main");
    config->region[0] = 0.0f;
    config->region[1] = 0.0f;
    config->region[2] = 1.0f;
    config->region[3] = 1.0f;
    config->input_mode = tgfx_intern_string("simple");
    config->enabled = true;
}

void tc_viewport_config_copy(tc_viewport_config* dst, const tc_viewport_config* src) {
    if (!dst || !src) return;
    dst->name = src->name ? tgfx_intern_string(src->name) : NULL;
    dst->display_name = src->display_name ? tgfx_intern_string(src->display_name) : tgfx_intern_string("Main");
    dst->render_target_name = src->render_target_name ? tgfx_intern_string(src->render_target_name) : NULL;
    dst->region[0] = src->region[0];
    dst->region[1] = src->region[1];
    dst->region[2] = src->region[2];
    dst->region[3] = src->region[3];
    dst->depth = src->depth;
    dst->input_mode = src->input_mode ? tgfx_intern_string(src->input_mode) : tgfx_intern_string("simple");
    dst->block_input_in_editor = src->block_input_in_editor;
    dst->enabled = src->enabled;
}
