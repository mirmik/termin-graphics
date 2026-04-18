// tgfx2_interop.h - Bridge between legacy tgfx gpu_ops and tgfx2 IRenderDevice
// Allows the resource system (registries, tc_gpu_context) to route GPU operations
// through tgfx2 while maintaining backward compatibility with GL IDs.
#pragma once

#include "tgfx/tgfx_api.h"

#ifdef __cplusplus
#include <cstdint>
namespace tgfx { class IRenderDevice; }

extern "C" {
#else
#include <stdint.h>
#endif

// Set the tgfx2 render device used by the tgfx2-backed gpu_ops implementation.
// Must be called before tgfx2_gpu_ops_register().
TGFX_API void tgfx2_interop_set_device(void* device);

// Get the tgfx2 render device (returns NULL if not set).
TGFX_API void* tgfx2_interop_get_device(void);

// Register tgfx2-backed gpu_ops vtable.
// Requires tgfx2_interop_set_device() to have been called first.
// The vtable routes resource creation through tgfx::IRenderDevice
// and extracts GL IDs for backward compatibility with existing code.
TGFX_API void tgfx2_gpu_ops_register(void);

// ---------------------------------------------------------------------------
// External GL texture registration — plain-C bridge to
// IRenderDevice::register_external_texture (OpenGL backend only).
//
// Intended for host code (C# / WPF through P/Invoke, any non-C++ caller)
// that already owns a GL texture (e.g. the color attachment of a WPF
// GLWpfControl framebuffer) and wants to hand it to the engine without
// talking to `IRenderDevice&` directly.
//
// `format` is a `tgfx::PixelFormat` cast to int — pass the matching
// attachment format (RGBA8_UNorm = 0 / 1 depending on enum order; see
// tgfx2/enums.hpp on the current build). `usage` is a bitmask of
// `tgfx::TextureUsage` flags; the most common value is
// `Sampled | ColorAttachment | CopyDst` for a composite target.
//
// Returns a non-zero tgfx2 TextureHandle id on success (32-bit handle
// body; the C++ `TextureHandle` is {id}). Returns 0 on error — typically
// when the interop device has not been set, is not an OpenGL backend,
// or the arguments are invalid.
//
// The returned handle is owned by the caller — release with
// `tgfx2_interop_destroy_texture_handle` when the GL texture goes
// away (WPF framebuffer resize, control disposal, etc.). Destroying
// the handle does NOT free the underlying GL texture; the host keeps
// owning it.
TGFX_API uint32_t tgfx2_interop_register_external_gl_texture(
    uint32_t gl_tex_id,
    uint32_t width, uint32_t height,
    int format,
    uint32_t usage);

// Release a handle previously returned by
// tgfx2_interop_register_external_gl_texture. Safe to call with id == 0.
TGFX_API void tgfx2_interop_destroy_texture_handle(uint32_t handle_id);

#ifdef __cplusplus
}

// C++ typed accessors
namespace tgfx {

inline void set_tgfx2_device(tgfx::IRenderDevice* device) {
    tgfx2_interop_set_device(static_cast<void*>(device));
}

inline tgfx::IRenderDevice* get_tgfx2_device() {
    return static_cast<tgfx::IRenderDevice*>(tgfx2_interop_get_device());
}

} // namespace tgfx
#endif
