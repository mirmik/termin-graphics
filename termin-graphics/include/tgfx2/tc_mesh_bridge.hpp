#pragma once

#include <cstdint>
#include <initializer_list>
#include <string_view>

#include "tgfx2/tgfx2_api.h"
#include "tgfx2/enums.hpp"
#include "tgfx2/handles.hpp"
#include "tgfx2/vertex_layout.hpp"

extern "C" {
struct tc_mesh;
}

namespace tgfx {

class IRenderDevice;
class RenderContext2;

// Backend-neutral bridge from the canonical Termin C mesh resource to tgfx2
// draw handles. The mesh remains the source of layout/index/topology data;
// this layer only materializes or reuses backend buffers for the active device.
struct Tgfx2MeshBinding {
    BufferHandle vertex_buffer;
    BufferHandle index_buffer;
    VertexBufferLayout layout;
    uint32_t index_count = 0;
    IndexType index_type = IndexType::Uint32;
    PrimitiveTopology topology = PrimitiveTopology::TriangleList;
    bool destroy_vertex_buffer = false;
    bool destroy_index_buffer = false;
};

TGFX2_API Tgfx2MeshBinding wrap_mesh_as_tgfx2(
    IRenderDevice& device,
    tc_mesh* mesh
);

TGFX2_API void release_mesh_binding(
    IRenderDevice& device,
    const Tgfx2MeshBinding& binding
);

TGFX2_API VertexBufferLayout filter_vertex_layout_to_locations(
    const VertexBufferLayout& layout,
    std::initializer_list<uint32_t> used_locations,
    bool use_shader_input_locations = false
);

TGFX2_API VertexBufferLayout filter_vertex_layout_to_semantics(
    const VertexBufferLayout& layout,
    std::initializer_list<std::string_view> used_semantics,
    bool use_shader_input_locations = false
);

TGFX2_API std::string_view standard_vertex_semantic_for_location(uint32_t location);

TGFX2_API std::string_view vertex_attribute_semantic(const VertexAttribute& attr);

TGFX2_API bool vertex_layout_has_semantic(
    const VertexBufferLayout& layout,
    std::string_view semantic
);

// Draws a tc_mesh into the currently open RenderContext2 pass using the
// currently bound shader/resources/render state. Optional layout override lets
// depth/id/shadow passes trim unused attributes while keeping the standard mesh
// wrapping/upload/release path.
TGFX2_API bool draw_tc_mesh(
    RenderContext2& ctx,
    tc_mesh* mesh,
    const VertexBufferLayout* layout_override = nullptr
);

TGFX2_API bool draw_tc_submesh(
    RenderContext2& ctx,
    tc_mesh* mesh,
    size_t submesh_index,
    const VertexBufferLayout* layout_override = nullptr
);

TGFX2_API bool draw_tc_mesh(
    RenderContext2& ctx,
    tc_mesh* mesh,
    std::initializer_list<uint32_t> used_locations,
    bool use_shader_input_locations = false
);

TGFX2_API bool draw_tc_submesh(
    RenderContext2& ctx,
    tc_mesh* mesh,
    size_t submesh_index,
    std::initializer_list<uint32_t> used_locations,
    bool use_shader_input_locations = false
);

TGFX2_API bool draw_tc_mesh(
    RenderContext2& ctx,
    tc_mesh* mesh,
    std::initializer_list<std::string_view> used_semantics,
    bool use_shader_input_locations = false
);

TGFX2_API bool draw_tc_submesh(
    RenderContext2& ctx,
    tc_mesh* mesh,
    size_t submesh_index,
    std::initializer_list<std::string_view> used_semantics,
    bool use_shader_input_locations = false
);

} // namespace tgfx
