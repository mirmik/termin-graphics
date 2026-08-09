#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Semantic meaning of RGB values published at a color-output boundary.
// Transfer encoding is deliberately separate: scene-linear data still needs
// tone mapping before it can be written to an SDR display target.
typedef enum tc_color_content {
    TC_COLOR_CONTENT_SCENE_LINEAR = 0,
    TC_COLOR_CONTENT_DISPLAY_LINEAR = 1,
    TC_COLOR_CONTENT_DISPLAY_SRGB = 2,
} tc_color_content;

#ifdef __cplusplus
}
#endif
