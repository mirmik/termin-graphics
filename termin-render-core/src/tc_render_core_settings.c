#include "tc_render_core_settings.h"

static tc_render_sync_mode g_render_sync_mode = TC_RENDER_SYNC_NONE;

tc_render_sync_mode tc_render_core_settings_get_sync_mode(void) {
    return g_render_sync_mode;
}

void tc_render_core_settings_set_sync_mode(tc_render_sync_mode mode) {
    g_render_sync_mode = mode;
}
