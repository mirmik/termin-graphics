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
}

namespace termin {

// Wrap a legacy tc_texture as a tgfx2 TextureHandle that is non-owning.
// Triggers tc_texture_upload_gpu first so the share group's GL texture
// object exists, then reads the GL id from the current context's
// texture slot and registers it as an external tgfx2 texture.
//
// The returned handle can be fed into ctx2.bind_sampled_texture or
// apply_material_phase_ubo's texture list. It must be destroyed via
// device.destroy() after use; the underlying GL texture survives
// because register_external_texture marked it external.
//
// Returns an invalid handle (id == 0) if the texture handle is
// invalid, upload fails, or no GPU context is active.
RENDER_API tgfx::TextureHandle wrap_tc_texture_as_tgfx2(
    tgfx::OpenGLRenderDevice& device,
    tc_texture_handle handle
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
// its translated vertex layout. Triggers the legacy upload path first
// (tc_mesh_upload_gpu) so the share group's VBO/EBO are guaranteed to
// exist. Used by Phase 2 passes (ShadowPass, IdPass, ...) that draw
// through tgfx2 while TcMesh itself still allocates GL buffers via
// the legacy ops vtable.
RENDER_API Tgfx2MeshBinding wrap_mesh_as_tgfx2(
    tgfx::OpenGLRenderDevice& device,
    tc_mesh* mesh
);

} // namespace termin
