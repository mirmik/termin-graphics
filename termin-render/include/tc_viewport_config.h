// tc_viewport_config.h - Viewport configuration for scene mounting
#ifndef TC_VIEWPORT_CONFIG_H
#define TC_VIEWPORT_CONFIG_H

#include "tc_types.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Viewport configuration structure
typedef struct tc_viewport_config {
    const char* name;           // Viewport name (interned)
    const char* display_name;   // Display name (interned)
    const char* camera_uuid;    // Camera entity UUID (interned)
    float region[4];            // Normalized region (x, y, width, height)
    const char* pipeline_uuid;  // Pipeline asset UUID (interned, nullable)
    const char* pipeline_name;  // Special pipeline name like "(Editor)" (interned, nullable)
    int depth;                  // Viewport depth for ordering
    const char* input_mode;     // Input mode: "none", "simple", "editor" (interned)
    bool block_input_in_editor; // Block input in editor mode
    uint64_t layer_mask;        // Layer mask for rendering
    bool enabled;               // Whether viewport is enabled
} tc_viewport_config;

// Initialize viewport config with defaults
TC_API void tc_viewport_config_init(tc_viewport_config* config);

// Copy viewport config (interns all strings)
TC_API void tc_viewport_config_copy(tc_viewport_config* dst, const tc_viewport_config* src);

#ifdef __cplusplus
}
#endif

#endif // TC_VIEWPORT_CONFIG_H
