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
class IRenderDevice;
class RenderContext2;
}

namespace termin {

// Wrap a core_c tc_texture as a tgfx2 TextureHandle. The active
// IRenderDevice owns the bridge cache keyed by pool_index + version.
// OpenGL and Vulkan both create real tgfx2 texture handles through
// IRenderDevice::ensure_tc_texture().
//
// Returns an invalid handle (id == 0) if the tc_texture handle is
// invalid, the CPU-first tc_texture has no pixel data, or allocation/upload
// fails.
RENDER_API tgfx::TextureHandle wrap_tc_texture_as_tgfx2(
    tgfx::IRenderDevice& device,
    tc_texture_handle handle
);

// Complement to wrap_tc_texture_as_tgfx2. Currently a no-op: the
// IRenderDevice cache owns the handle and releases it on cache
// invalidation, device teardown, or tc_texture destroy-hook.
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

// Complement to wrap_mesh_as_tgfx2. Device-owned mesh-cache handles must
// not be destroyed here; only per-call augmented buffers are released.
// Safe on a default-constructed Tgfx2MeshBinding (index_count == 0).
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
