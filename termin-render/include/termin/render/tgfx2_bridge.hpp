// tgfx2_bridge.hpp - Interop helpers between legacy tc_mesh / tc_texture and tgfx2
#pragma once

#include <cstdint>

#include "tgfx2/handles.hpp"
#include "tgfx2/enums.hpp"
#include "tgfx2/vertex_layout.hpp"

#include "termin/render/render_export.hpp"

extern "C" {
#include "tgfx/resources/tc_texture.h"
}

struct tc_mesh;

namespace tgfx {
class OpenGLRenderDevice;
class IRenderDevice;
}

namespace termin {

// Wrap a legacy tc_texture as a tgfx2 TextureHandle for the duration of a
// draw. Backend-agnostic:
//
//  - OpenGL: triggers tc_texture_upload_gpu, pulls the GL id out of the
//    share group's texture slot, and registers it as an external
//    non-owning tgfx2 texture. Call release_texture_binding (or
//    device.destroy directly) to free the handle; the GL object
//    survives because register_external_texture marked it external.
//
//  - Non-GL (Vulkan, ...): there is no share group slot to wrap. The
//    first call creates a real tgfx2 texture and uploads tc_texture->data
//    into it, caches the handle keyed on (texture, device) (by pool_index
//    + version). Subsequent calls reuse the cache entry until the
//    tc_texture's version bumps. Handles stay owned by the cache — the
//    caller must NOT destroy them. release_texture_binding is a no-op
//    here.
//
// Returns an invalid handle (id == 0) if the tc_texture handle is
// invalid, the tc_texture has no pixel data, or the upload fails.
RENDER_API tgfx::TextureHandle wrap_tc_texture_as_tgfx2(
    tgfx::IRenderDevice& device,
    tc_texture_handle handle
);

// Complement to wrap_tc_texture_as_tgfx2. OpenGL path destroys the
// non-owning external wrapper; non-GL path is a no-op (cache owns the
// handle). Safe on an empty handle.
RENDER_API void release_texture_binding(
    tgfx::IRenderDevice& device,
    tgfx::TextureHandle binding
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
    tgfx::BufferHandle vertex_buffer;
    tgfx::BufferHandle index_buffer;
    tgfx::VertexBufferLayout layout;
    uint32_t index_count = 0;
    tgfx::IndexType index_type = tgfx::IndexType::Uint32;
    tgfx::PrimitiveTopology topology = tgfx::PrimitiveTopology::TriangleList;
};

// Wrap a tc_mesh's GPU shadow as a tgfx2 vertex/index buffer pair plus
// its translated vertex layout. Used by Phase 2 passes (ShadowPass,
// IdPass, ColorPass, ...) that draw through tgfx2 while tc_mesh still
// owns the source CPU data.
//
// OpenGL path: triggers the legacy upload (tc_mesh_upload_gpu) so the
// share group's VBO/EBO exist, then wraps them as non-owning tgfx2
// buffers via register_external_buffer. The caller MUST destroy(handle)
// both buffers after the draw — underlying GL objects survive.
//
// Non-GL path (Vulkan / future): uploads tc_mesh->vertices / ->indices
// into freshly-created tgfx2 BufferHandles and caches them keyed on
// (mesh, device). Returned handles stay owned by the cache; the caller
// must NOT destroy them — they are reused across frames and released
// when the cache entry is evicted (mesh version change / device
// teardown).
//
// Call-site rule: always pair every non-empty wrap_mesh_as_tgfx2 result
// with `release_mesh_binding(device, binding)` after the draw. That
// function is a no-op on the cached (Vulkan) path and does the legacy
// destroy() on the OpenGL path — one call site, both backends happy.
RENDER_API Tgfx2MeshBinding wrap_mesh_as_tgfx2(
    tgfx::IRenderDevice& device,
    tc_mesh* mesh
);

// Complement to wrap_mesh_as_tgfx2 — frees the buffers on the legacy
// OpenGL path (where register_external_buffer'd handles leak otherwise),
// and is a no-op on the cached non-GL path. Safe on a default-
// constructed Tgfx2MeshBinding (index_count == 0).
RENDER_API void release_mesh_binding(
    tgfx::IRenderDevice& device,
    const Tgfx2MeshBinding& binding
);

} // namespace termin
