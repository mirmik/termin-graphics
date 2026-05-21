// tc_skeleton_registry.h - Global skeleton storage with pool + hash table
#pragma once

#include "resources/tc_skeleton.h"

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// Lifecycle (called from tc_init/tc_shutdown)
// ============================================================================

TC_API void tc_skeleton_init(void);
TC_API void tc_skeleton_shutdown(void);

// ============================================================================
// Skeleton operations (handle-based API)
// ============================================================================

// Create a new skeleton with given UUID (or auto-generate if NULL)
TC_API tc_skeleton_handle tc_skeleton_create(const char* uuid);

// Find skeleton by UUID
TC_API tc_skeleton_handle tc_skeleton_find(const char* uuid);

// Find skeleton by name
TC_API tc_skeleton_handle tc_skeleton_find_by_name(const char* name);

// Get existing skeleton or create new one if not found
TC_API tc_skeleton_handle tc_skeleton_get_or_create(const char* uuid);

// Declare a skeleton that will be loaded lazily
TC_API tc_skeleton_handle tc_skeleton_declare(const char* uuid, const char* name);

// Get skeleton data by handle
TC_API tc_skeleton* tc_skeleton_get(tc_skeleton_handle h);

// Check if handle is valid
TC_API bool tc_skeleton_is_valid(tc_skeleton_handle h);

// Destroy skeleton by handle
TC_API bool tc_skeleton_destroy(tc_skeleton_handle h);

// Check if skeleton exists by UUID
TC_API bool tc_skeleton_contains(const char* uuid);

// Get number of skeletons
TC_API size_t tc_skeleton_count(void);

// Check if skeleton data is loaded
TC_API bool tc_skeleton_is_loaded(tc_skeleton_handle h);

// Set load callback for lazy loading
typedef bool (*tc_skeleton_load_fn)(tc_skeleton* skeleton, void* user_data);
TC_API void tc_skeleton_set_load_callback(
    tc_skeleton_handle h,
    tc_skeleton_load_fn callback,
    void* user_data
);

// Ensure skeleton is loaded
TC_API bool tc_skeleton_ensure_loaded(tc_skeleton_handle h);

// ============================================================================
// Skeleton data operations
// ============================================================================

// Allocate bones array (frees existing if any)
// Returns pointer to first bone, or NULL on failure
TC_API tc_bone* tc_skeleton_alloc_bones(tc_skeleton* skeleton, size_t count);

// Get bone by index
TC_API tc_bone* tc_skeleton_get_bone(tc_skeleton* skeleton, size_t index);
TC_API const tc_bone* tc_skeleton_get_bone_const(const tc_skeleton* skeleton, size_t index);

// Find bone index by name (-1 if not found)
TC_API int tc_skeleton_find_bone(const tc_skeleton* skeleton, const char* name);

// Rebuild root indices (call after modifying bones)
TC_API void tc_skeleton_rebuild_roots(tc_skeleton* skeleton);

// ============================================================================
// Skeleton info for debugging/inspection
// ============================================================================

typedef struct tc_skeleton_info {
    tc_skeleton_handle handle;
    char uuid[TC_UUID_SIZE];
    const char* name;
    uint32_t ref_count;
    uint32_t version;
    size_t bone_count;
    uint8_t is_loaded;
    uint8_t _pad[7];
} tc_skeleton_info;

TC_API tc_skeleton_info* tc_skeleton_get_all_info(size_t* count);

// ============================================================================
// Iteration
// ============================================================================

typedef bool (*tc_skeleton_iter_fn)(tc_skeleton_handle h, tc_skeleton* skeleton, void* user_data);
TC_API void tc_skeleton_foreach(tc_skeleton_iter_fn callback, void* user_data);

#ifdef __cplusplus
}
#endif
