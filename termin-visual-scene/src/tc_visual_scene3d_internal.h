#include "termin_visual_scene/tc_visual_item3d.h"
#ifdef __cplusplus
extern "C" {
#endif
void tc_visual_scene3d_touch_order(tc_visual_scene3d* scene);
tc_visual_scene3d* tc_visual_scene3d_resolve_internal(tc_visual_scene3d_handle handle);
tc_visual_item3d* tc_visual_scene3d_resolve_item(tc_visual_scene3d_handle, tc_visual_item3d_handle);
#ifdef __cplusplus
}
#endif
