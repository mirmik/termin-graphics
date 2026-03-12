// tc_project_settings.h - Project-level render settings accessible from C/C++
#ifndef TC_PROJECT_SETTINGS_H
#define TC_PROJECT_SETTINGS_H

#include "tc_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum tc_render_sync_mode {
    TC_RENDER_SYNC_NONE = 0,
    TC_RENDER_SYNC_FLUSH = 1,
    TC_RENDER_SYNC_FINISH = 2
} tc_render_sync_mode;

TC_API tc_render_sync_mode tc_project_settings_get_render_sync_mode(void);
TC_API void tc_project_settings_set_render_sync_mode(tc_render_sync_mode mode);

#ifdef __cplusplus
}
#endif

#endif
