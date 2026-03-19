// tc_render_target_config.h - Render target configuration for scene serialization
#ifndef TC_RENDER_TARGET_CONFIG_H
#define TC_RENDER_TARGET_CONFIG_H

#include "tc_types.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct tc_render_target_config {
    const char* name;           // Render target name (interned)
    const char* camera_uuid;    // Camera entity UUID (interned)
    int width;                  // Render width in pixels
    int height;                 // Render height in pixels
    const char* pipeline_uuid;  // Pipeline asset UUID (interned, nullable)
    const char* pipeline_name;  // Special pipeline name (interned, nullable)
    uint64_t layer_mask;        // Layer mask for rendering
    bool enabled;               // Whether render target is enabled
} tc_render_target_config;

// Initialize render target config with defaults
TC_API void tc_render_target_config_init(tc_render_target_config* config);

// Copy render target config (interns all strings)
TC_API void tc_render_target_config_copy(tc_render_target_config* dst, const tc_render_target_config* src);

#ifdef __cplusplus
}
#endif

#endif
