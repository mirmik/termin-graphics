// tgfx2_bridge.hpp - Interop helpers between legacy tgfx FBOs and tgfx2 textures
//
// Phase 2 of the tgfx2 migration lets individual passes draw through
// tgfx2::RenderContext2 while the surrounding pipeline still manages
// framebuffers via the legacy FBOPool. These helpers wrap a legacy
// FramebufferHandle's color/depth attachment as a non-owning tgfx2
// TextureHandle suitable for begin_pass() and bind_texture().
#pragma once

#include <cstdint>

#include "tgfx2/handles.hpp"
#include "tgfx2/enums.hpp"

#include "termin/render/render_export.hpp"

namespace tgfx2 {
class OpenGLRenderDevice;
}

namespace termin {

class FramebufferHandle;

// Translate a legacy tgfx framebuffer format string ("rgba8", "r8",
// "rgba16f", ...) to the tgfx2 PixelFormat enum. Defaults to RGBA8_UNorm
// for unknown strings.
RENDER_API tgfx2::PixelFormat fbo_format_string_to_tgfx2(const char* format);

// Wrap the color attachment of a legacy FramebufferHandle as a tgfx2
// TextureHandle that is non-owning. The caller should `destroy()` the
// handle when done; the underlying GL texture is preserved.
//
// Returns an invalid handle (id == 0) if either argument is null or the
// FBO has no color attachment.
RENDER_API tgfx2::TextureHandle wrap_fbo_color_as_tgfx2(
    tgfx2::OpenGLRenderDevice& device,
    FramebufferHandle* fbo
);

} // namespace termin
