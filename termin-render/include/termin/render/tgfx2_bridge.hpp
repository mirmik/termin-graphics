// tgfx2_bridge.hpp - Interop helpers between core_c tc_mesh / tc_texture and tgfx2
#pragma once

#include <cstdint>

#include "tgfx2/handles.hpp"
#include "tgfx2/enums.hpp"
#include "tgfx2/tc_mesh_bridge.hpp"
#include "tgfx2/vertex_layout.hpp"

#include "termin/render/render_export.hpp"

extern "C" {
#include "tgfx/resources/tc_texture.h"
}

struct tc_mesh;

namespace tgfx {
class OpenGLRenderDevice;
class IRenderDevice;
class RenderContext2;
}

namespace termin {

// Wrap a core_c tc_texture as a tgfx2 TextureHandle for the duration of a
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

using Tgfx2MeshBinding = tgfx::Tgfx2MeshBinding;

// Compatibility wrappers. The implementation lives in termin-graphics
// (tgfx2/tc_mesh_bridge.hpp); render passes keep including this header while
// the call sites are migrated.
RENDER_API Tgfx2MeshBinding wrap_mesh_as_tgfx2(
    tgfx::IRenderDevice& device,
    tc_mesh* mesh
);

// Complement to wrap_mesh_as_tgfx2. The OpenGL share-group path creates
// per-call external buffer wrappers that must be released here,
// and is a no-op on the cached non-GL path. Safe on a default-
// constructed Tgfx2MeshBinding (index_count == 0).
RENDER_API void release_mesh_binding(
    tgfx::IRenderDevice& device,
    const Tgfx2MeshBinding& binding
);

// Drop every VertexAttribute whose `location` is not in the provided set.
// Stride and per_instance are preserved — the caller keeps feeding the
// same interleaved VBO, we just tell the pipeline to skip attribute
// fetches that the shader will never read. Without this, Vulkan logs
// "Vertex attribute at location N not consumed by vertex shader"
// performance warnings for every minimal-input VS (shadow / depth-only /
// id-pass) paired with a full mesh layout.
//
// Takes an initializer_list so call sites read as
// `filter_vertex_layout_to_locations(layout, {0})`.
RENDER_API tgfx::VertexBufferLayout filter_vertex_layout_to_locations(
    const tgfx::VertexBufferLayout& layout,
    std::initializer_list<uint32_t> used_locations
);

RENDER_API bool draw_tc_mesh(
    tgfx::RenderContext2& ctx,
    tc_mesh* mesh,
    const tgfx::VertexBufferLayout* layout_override = nullptr
);

RENDER_API bool draw_tc_mesh(
    tgfx::RenderContext2& ctx,
    tc_mesh* mesh,
    std::initializer_list<uint32_t> used_locations
);

} // namespace termin
