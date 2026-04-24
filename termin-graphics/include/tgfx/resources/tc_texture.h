// tc_texture.h - Texture data structures
#pragma once

#include "tgfx/tgfx_api.h"
#include "tgfx/tc_handle.h"
#include "tgfx/resources/tc_resource.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// Texture handle - safe reference to texture in pool
// ============================================================================

TC_DEFINE_HANDLE(tc_texture_handle)

// ============================================================================
// Texture format
// ============================================================================

typedef enum tc_texture_format {
    TC_TEXTURE_RGBA8 = 0,   // 4 channels, 8 bits each
    TC_TEXTURE_RGB8  = 1,   // 3 channels, 8 bits each
    TC_TEXTURE_RG8   = 2,   // 2 channels, 8 bits each
    TC_TEXTURE_R8    = 3,   // 1 channel, 8 bits
    TC_TEXTURE_RGBA16F = 4, // 4 channels, 16-bit float
    TC_TEXTURE_RGB16F = 5,  // 3 channels, 16-bit float
    TC_TEXTURE_DEPTH24 = 6, // depth texture, 24 bits (for shadow maps)
    TC_TEXTURE_DEPTH32F = 7,// depth texture, 32-bit float
} tc_texture_format;

// ============================================================================
// Storage kind & usage flags
// ============================================================================
//
// Most tc_texture instances are CPU-pixels-with-GPU-mirror: the source of
// truth lives in `tc_texture.data`, and bridge code uploads it on demand.
// Render targets break that model — they have no CPU origin; the GPU image
// is born blank and is written into by render passes. Marking such a
// texture as GPU_ONLY tells the upload path "don't expect a CPU blob, just
// allocate the image with the requested usage flags".
typedef enum tc_texture_storage_kind {
    TC_TEXTURE_STORAGE_CPU_PIXELS = 0,  // default — pixels in tex->data
    TC_TEXTURE_STORAGE_GPU_ONLY   = 1,  // render-target-style; no CPU data
} tc_texture_storage_kind;

// Usage hint that backends translate to native creation flags
// (VkImageUsageFlags on Vulkan; ignored on GL since the legacy upload
// path picks attachment / sampled state from the bind site). Bitfield —
// values OR together.
typedef enum tc_texture_usage_flags {
    TC_TEXTURE_USAGE_SAMPLED          = 1u << 0,  // bound as a shader resource (default)
    TC_TEXTURE_USAGE_COLOR_ATTACHMENT = 1u << 1,  // can be a color render target
    TC_TEXTURE_USAGE_DEPTH_ATTACHMENT = 1u << 2,  // can be a depth/stencil render target
    TC_TEXTURE_USAGE_COPY_SRC         = 1u << 3,  // valid source for blit / copy / readback
    TC_TEXTURE_USAGE_COPY_DST         = 1u << 4,  // valid destination for blit / copy / upload
} tc_texture_usage_flags;

// ============================================================================
// Texture data
// ============================================================================

typedef struct tc_texture {
    tc_resource_header header;  // common resource fields (uuid, name, version, etc.)
    void* data;                 // raw pixel data blob (NULL for GPU_ONLY)
    uint32_t width;
    uint32_t height;
    uint8_t channels;           // 1, 2, 3, or 4
    uint8_t format;             // tc_texture_format
    uint8_t flip_x;             // transform flag
    uint8_t flip_y;             // transform flag (default true for OpenGL)
    uint8_t transpose;          // transform flag
    uint8_t mipmap;             // generate mipmaps on upload
    uint8_t clamp;              // use clamp wrapping (vs repeat)
    uint8_t compare_mode;       // enable depth comparison for sampler2DShadow
    uint8_t storage_kind;       // tc_texture_storage_kind, default = CPU_PIXELS
    uint32_t usage;             // tc_texture_usage_flags bitset, default = SAMPLED
    const char* source_path;    // optional source file path (interned string)
} tc_texture;


// ============================================================================
// Helper functions
// ============================================================================

// Get bytes per pixel for format
TGFX_API size_t tc_texture_format_bpp(tc_texture_format format);

// Get channel count for format
TGFX_API uint8_t tc_texture_format_channels(tc_texture_format format);

// Calculate texture data size in bytes
static inline size_t tc_texture_data_size(const tc_texture* tex) {
    return tex->width * tex->height * tex->channels;
}

// ============================================================================
// Storage / usage accessors
// ============================================================================

// Setter for storage_kind. Marking a texture GPU_ONLY tells subsequent
// upload paths not to require `tex->data` and to allocate the GPU image
// with the texture's `usage` flags.
TGFX_API void tc_texture_set_storage_kind(tc_texture* tex, tc_texture_storage_kind kind);

// Replace the usage bitset. Pass an OR of tc_texture_usage_flags values.
TGFX_API void tc_texture_set_usage(tc_texture* tex, uint32_t usage);

// Set width/height/format in one call. Bumps `header.version` so cached
// GPU handles get re-created on the next bridge lookup. Used by render
// targets when they are resized.
TGFX_API void tc_texture_set_size_format(
    tc_texture* tex,
    uint32_t width, uint32_t height,
    tc_texture_format format
);

static inline bool tc_texture_is_gpu_only(const tc_texture* tex) {
    return tex && tex->storage_kind == TC_TEXTURE_STORAGE_GPU_ONLY;
}

// ============================================================================
// Reference counting
// ============================================================================

// Increment reference count
TGFX_API void tc_texture_add_ref(tc_texture* tex);

// Decrement reference count. Returns true if texture was destroyed
TGFX_API bool tc_texture_release(tc_texture* tex);

// ============================================================================
// UUID computation
// ============================================================================

// Compute UUID from texture data (FNV-1a hash)
TGFX_API void tc_texture_compute_uuid(
    const void* data, size_t size,
    uint32_t width, uint32_t height, uint8_t channels,
    char* uuid_out
);

#ifdef __cplusplus
}
#endif
