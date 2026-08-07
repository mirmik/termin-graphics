#pragma once

#include "termin_visual_scene/tc_graphic_item.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Internal invalidation shared by the scene owner and low-level tree API. */
void tc_visual_scene_touch_order(tc_visual_scene* scene);

#ifdef __cplusplus
}
#endif
