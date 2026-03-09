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
} tc_texture_format;

// ============================================================================
// Texture data
// ============================================================================

typedef struct tc_texture {
    tc_resource_header header;  // common resource fields (uuid, name, version, etc.)
    void* data;                 // raw pixel data blob
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
