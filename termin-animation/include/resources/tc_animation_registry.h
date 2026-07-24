// tc_animation_registry.h - Global animation storage with pool + hash table
#pragma once

#include "resources/tc_animation.h"

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// Lifecycle (called from tc_init/tc_shutdown)
// ============================================================================

TC_API void tc_animation_init(void);
TC_API void tc_animation_shutdown(void);

// ============================================================================
// Animation operations (handle-based API)
// ============================================================================

// Create a new animation with given UUID (or auto-generate if NULL)
TC_API tc_animation_handle tc_animation_create(const char* uuid);

// Find animation by UUID
TC_API tc_animation_handle tc_animation_find(const char* uuid);

// Find animation by name
TC_API tc_animation_handle tc_animation_find_by_name(const char* name);

// Get existing animation or create new one if not found
TC_API tc_animation_handle tc_animation_get_or_create(const char* uuid);

// Declare an animation that will be loaded lazily
TC_API tc_animation_handle tc_animation_declare(const char* uuid, const char* name);

// Get animation data by handle
TC_API tc_animation* tc_animation_get(tc_animation_handle h);

// Check if handle is valid
TC_API bool tc_animation_is_valid(tc_animation_handle h);

// Destroy animation by handle
TC_API bool tc_animation_destroy(tc_animation_handle h);

// Check if animation exists by UUID
TC_API bool tc_animation_contains(const char* uuid);

// Get number of animations
TC_API size_t tc_animation_count(void);

// Check if animation data is loaded
TC_API bool tc_animation_is_loaded(tc_animation_handle h);

// Ensure animation is loaded
TC_API bool tc_animation_ensure_loaded(tc_animation_handle h);

// ============================================================================
// Animation data operations
// ============================================================================

// Allocate channels array (frees existing if any)
TC_API tc_animation_channel* tc_animation_alloc_channels(tc_animation* anim, size_t count);

// Get channel by index
TC_API tc_animation_channel* tc_animation_get_channel(tc_animation* anim, size_t index);

// Find channel by target name (-1 if not found)
TC_API int tc_animation_find_channel(const tc_animation* anim, const char* target_name);

// Allocate keyframes for a channel
TC_API tc_keyframe_vec3* tc_animation_channel_alloc_translation(tc_animation_channel* ch, size_t count);
TC_API tc_keyframe_quat* tc_animation_channel_alloc_rotation(tc_animation_channel* ch, size_t count);
TC_API tc_keyframe_scalar* tc_animation_channel_alloc_scale(tc_animation_channel* ch, size_t count);

// Recompute animation duration from channels
TC_API void tc_animation_recompute_duration(tc_animation* anim);

// ============================================================================
// Animation info for debugging/inspection
// ============================================================================

typedef struct tc_animation_info {
    tc_animation_handle handle;
    char uuid[TC_UUID_SIZE];
    const char* name;
    uint32_t ref_count;
    uint32_t version;
    double duration;
    size_t channel_count;
    uint8_t is_loaded;
    uint8_t loop;
    uint8_t _pad[6];
} tc_animation_info;

TC_API tc_animation_info* tc_animation_get_all_info(size_t* count);

// ============================================================================
// Iteration
// ============================================================================

typedef bool (*tc_animation_iter_fn)(tc_animation_handle h, tc_animation* animation, void* user_data);
TC_API void tc_animation_foreach(tc_animation_iter_fn callback, void* user_data);

#ifdef __cplusplus
}
#endif
