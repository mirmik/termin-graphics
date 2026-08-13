// tc_render_core_settings.h - Process-level render execution settings
#ifndef TC_RENDER_CORE_SETTINGS_H
#define TC_RENDER_CORE_SETTINGS_H

#include "tc_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum tc_render_sync_mode {
    TC_RENDER_SYNC_NONE = 0,
    TC_RENDER_SYNC_FLUSH = 1,
    TC_RENDER_SYNC_FINISH = 2
} tc_render_sync_mode;

TC_API tc_render_sync_mode tc_render_core_settings_get_sync_mode(void);
TC_API void tc_render_core_settings_set_sync_mode(tc_render_sync_mode mode);

#ifdef __cplusplus
}
#endif

#endif
