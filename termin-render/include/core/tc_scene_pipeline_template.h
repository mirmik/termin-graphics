// tc_scene_pipeline_template.h - Scene pipeline template (graph source)
// Stores graph data (nodes, connections, viewport_frames) as tc_value.
// Compiles to RenderPipeline via graph_compiler.

#ifndef TC_SCENE_PIPELINE_TEMPLATE_H
#define TC_SCENE_PIPELINE_TEMPLATE_H

#include "tc_types.h"
#include <tgfx/tc_handle.h>
#include <tgfx/resources/tc_resource.h>
#include "tc_value.h"

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// Handle definition
// ============================================================================

TC_DEFINE_HANDLE(tc_spt_handle)

#ifdef __cplusplus
#define TC_SPT_HANDLE_INVALID (tc_spt_handle{0xFFFFFFFF, 0})
#else
#define TC_SPT_HANDLE_INVALID ((tc_spt_handle){0xFFFFFFFF, 0})
#endif

// ============================================================================
// Scene pipeline template structure
// ============================================================================

typedef struct tc_scene_pipeline_template {
    tc_resource_header header;

    tc_value graph_data;              // TC_VALUE_DICT: nodes, connections, viewport_frames

    char** target_viewports;          // Extracted viewport names (for fast access)
    size_t target_viewport_count;
} tc_scene_pipeline_template;

// ============================================================================
// Registry (pool-based)
// ============================================================================

// Declare template in registry (allocates slot, sets uuid/name)
TC_API tc_spt_handle tc_spt_declare(const char* uuid, const char* name);

// Get pointer from handle (returns NULL if invalid)
TC_API tc_scene_pipeline_template* tc_spt_get(tc_spt_handle h);

// Check if handle is valid (allocated and not freed)
TC_API bool tc_spt_is_valid(tc_spt_handle h);

// Check if template data is loaded
TC_API bool tc_spt_is_loaded(tc_spt_handle h);

// Find by UUID (returns invalid handle if not found)
TC_API tc_spt_handle tc_spt_find_by_uuid(const char* uuid);

// Find by name (returns invalid handle if not found)
TC_API tc_spt_handle tc_spt_find_by_name(const char* name);

// ============================================================================
// Graph data
// ============================================================================

// Set graph data (takes ownership of tc_value, extracts target_viewports)
TC_API void tc_spt_set_graph(tc_spt_handle h, tc_value graph);

// Get graph data (returns pointer, caller must not free)
TC_API const tc_value* tc_spt_get_graph(tc_spt_handle h);

// ============================================================================
// Accessors
// ============================================================================

// Get UUID
TC_API const char* tc_spt_get_uuid(tc_spt_handle h);

// Get name
TC_API const char* tc_spt_get_name(tc_spt_handle h);

// Set name
TC_API void tc_spt_set_name(tc_spt_handle h, const char* name);

// ============================================================================
// Target viewports (extracted from graph_data.viewport_frames)
// ============================================================================

TC_API size_t tc_spt_viewport_count(tc_spt_handle h);
TC_API const char* tc_spt_get_viewport(tc_spt_handle h, size_t index);

// ============================================================================
// Lazy loading
// ============================================================================

// Set load callback (called when data is needed but not loaded)
TC_API void tc_spt_set_load_callback(
    tc_spt_handle h,
    tc_resource_load_fn callback,
    void* user_data
);

// Trigger load if not loaded (returns true if loaded successfully)
TC_API bool tc_spt_ensure_loaded(tc_spt_handle h);

// ============================================================================
// Lifecycle
// ============================================================================

// Free template (returns slot to pool)
TC_API void tc_spt_free(tc_spt_handle h);

// Free all templates (call at shutdown)
TC_API void tc_spt_free_all(void);

#ifdef __cplusplus
}
#endif

#endif // TC_SCENE_PIPELINE_TEMPLATE_H
