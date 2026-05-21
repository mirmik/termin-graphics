// tc_skeleton.h - Skeleton data structures for skeletal animation
#pragma once

#include <tcbase/tc_types.h>
#include <tcbase/tc_binding_types.h>
#include <tcbase/tc_resource.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// Skeleton handle - safe reference to skeleton in pool
// ============================================================================

TC_DEFINE_HANDLE(tc_skeleton_handle)

// ============================================================================
// Bone data
// ============================================================================

#define TC_BONE_NAME_MAX 64

typedef struct tc_bone {
    char name[TC_BONE_NAME_MAX];
    int32_t index;
    int32_t parent_index;  // -1 for root bones

    // 4x4 inverse bind matrix (column-major)
    double inverse_bind_matrix[16];

    // Bind pose local transform
    double bind_translation[3];
    double bind_rotation[4];  // quaternion [x, y, z, w]
    double bind_scale[3];
} tc_bone;

// ============================================================================
// Skeleton data
// ============================================================================

typedef struct tc_skeleton {
    tc_resource_header header;  // common resource fields

    tc_bone* bones;             // array of bones (owned, malloc'd)
    size_t bone_count;

    int32_t* root_indices;      // indices of root bones (owned, malloc'd)
    size_t root_count;
} tc_skeleton;

// ============================================================================
// Bone helpers
// ============================================================================

// Initialize bone to identity
static inline void tc_bone_init(tc_bone* bone) {
    if (!bone) return;
    bone->name[0] = '\0';
    bone->index = 0;
    bone->parent_index = -1;

    // Identity matrix
    for (int i = 0; i < 16; i++) bone->inverse_bind_matrix[i] = 0.0;
    bone->inverse_bind_matrix[0] = 1.0;
    bone->inverse_bind_matrix[5] = 1.0;
    bone->inverse_bind_matrix[10] = 1.0;
    bone->inverse_bind_matrix[15] = 1.0;

    bone->bind_translation[0] = 0.0;
    bone->bind_translation[1] = 0.0;
    bone->bind_translation[2] = 0.0;

    bone->bind_rotation[0] = 0.0;
    bone->bind_rotation[1] = 0.0;
    bone->bind_rotation[2] = 0.0;
    bone->bind_rotation[3] = 1.0;

    bone->bind_scale[0] = 1.0;
    bone->bind_scale[1] = 1.0;
    bone->bind_scale[2] = 1.0;
}

static inline bool tc_bone_is_root(const tc_bone* bone) {
    return bone && bone->parent_index < 0;
}

// ============================================================================
// Reference counting
// ============================================================================

TC_API void tc_skeleton_add_ref(tc_skeleton* skeleton);
TC_API bool tc_skeleton_release(tc_skeleton* skeleton);

#ifdef __cplusplus
}
#endif
