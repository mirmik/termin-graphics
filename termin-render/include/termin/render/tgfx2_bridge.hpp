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
#include "tgfx2/vertex_layout.hpp"

#include "termin/render/render_export.hpp"

struct tc_mesh;

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

// Result of wrapping a tc_mesh as tgfx2 buffers + layout for one draw.
//
// vertex_buffer / index_buffer are non-owning external handles around the
// legacy VBO/EBO that the mesh's share group already created. The caller
// MUST destroy(handle) both handles after the draw completes; the
// underlying GL buffer objects survive because register_external_buffer
// marked them external. index_count == 0 means the wrap failed and the
// binding should not be used.
struct Tgfx2MeshBinding {
    tgfx2::BufferHandle vertex_buffer;
    tgfx2::BufferHandle index_buffer;
    tgfx2::VertexBufferLayout layout;
    uint32_t index_count = 0;
    tgfx2::IndexType index_type = tgfx2::IndexType::Uint32;
    tgfx2::PrimitiveTopology topology = tgfx2::PrimitiveTopology::TriangleList;
};

// Wrap a tc_mesh's GPU shadow as a tgfx2 vertex/index buffer pair plus
// its translated vertex layout. Triggers the legacy upload path first
// (tc_mesh_upload_gpu) so the share group's VBO/EBO are guaranteed to
// exist. Used by Phase 2 passes (ShadowPass, IdPass, ...) that draw
// through tgfx2 while TcMesh itself still allocates GL buffers via
// the legacy ops vtable.
RENDER_API Tgfx2MeshBinding wrap_mesh_as_tgfx2(
    tgfx2::OpenGLRenderDevice& device,
    tc_mesh* mesh
);

} // namespace termin
