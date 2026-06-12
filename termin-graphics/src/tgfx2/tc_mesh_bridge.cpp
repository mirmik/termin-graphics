#include "tgfx2/tc_mesh_bridge.hpp"

#include <algorithm>
#include <cstring>
#include <span>
#include <vector>

#include <tcbase/tc_log.hpp>

#include "tgfx/resources/tc_mesh.h"
#include "tgfx2/descriptors.hpp"
#include "tgfx2/i_render_device.hpp"
#include "tgfx2/render_context.hpp"

namespace tgfx {
namespace {

bool fill_binding_from_mesh(Tgfx2MeshBinding& out, tc_mesh* mesh) {
    out.layout.stride = mesh->layout.stride;
    out.layout.use_shader_input_locations =
        mesh->layout.use_shader_input_locations != 0;
    out.layout.attributes.reserve(mesh->layout.attrib_count);
    for (uint8_t i = 0; i < mesh->layout.attrib_count; i++) {
        const tgfx_vertex_attrib& a = mesh->layout.attribs[i];
        VertexAttribute va;
        va.location = a.location;
        va.offset = a.offset;

        bool ok = true;
        switch (static_cast<tgfx_attrib_type>(a.type)) {
            case TGFX_ATTRIB_FLOAT32:
                switch (a.size) {
                    case 1: va.format = VertexFormat::Float;  break;
                    case 2: va.format = VertexFormat::Float2; break;
                    case 3: va.format = VertexFormat::Float3; break;
                    case 4: va.format = VertexFormat::Float4; break;
                    default: ok = false; break;
                }
                break;
            case TGFX_ATTRIB_INT32:
                switch (a.size) {
                    case 1: va.format = VertexFormat::Int;  break;
                    case 2: va.format = VertexFormat::Int2; break;
                    case 3: va.format = VertexFormat::Int3; break;
                    case 4: va.format = VertexFormat::Int4; break;
                    default: ok = false; break;
                }
                break;
            case TGFX_ATTRIB_UINT32:
                switch (a.size) {
                    case 1: va.format = VertexFormat::UInt;  break;
                    case 2: va.format = VertexFormat::UInt2; break;
                    case 3: va.format = VertexFormat::UInt3; break;
                    case 4: va.format = VertexFormat::UInt4; break;
                    default: ok = false; break;
                }
                break;
            case TGFX_ATTRIB_INT16:
                switch (a.size) {
                    case 1: va.format = VertexFormat::Short;  break;
                    case 2: va.format = VertexFormat::Short2; break;
                    case 3: va.format = VertexFormat::Short3; break;
                    case 4: va.format = VertexFormat::Short4; break;
                    default: ok = false; break;
                }
                break;
            case TGFX_ATTRIB_UINT16:
                switch (a.size) {
                    case 1: va.format = VertexFormat::UShort;  break;
                    case 2: va.format = VertexFormat::UShort2; break;
                    case 3: va.format = VertexFormat::UShort3; break;
                    case 4: va.format = VertexFormat::UShort4; break;
                    default: ok = false; break;
                }
                break;
            case TGFX_ATTRIB_INT8:
                if (a.size == 4) va.format = VertexFormat::Byte4;
                else ok = false;
                break;
            case TGFX_ATTRIB_UINT8:
                if (a.size == 4) va.format = VertexFormat::UByte4;
                else ok = false;
                break;
            default:
                ok = false;
                break;
        }

        if (!ok) {
            tc::Log::error(
                "wrap_mesh_as_tgfx2: unsupported attrib (type=%u, size=%u) "
                "for '%s' - substituting Float%u",
                unsigned(a.type), unsigned(a.size),
                mesh->header.name ? mesh->header.name : mesh->header.uuid,
                unsigned(a.size >= 1 && a.size <= 4 ? a.size : 3));
            switch (a.size) {
                case 1: va.format = VertexFormat::Float;  break;
                case 2: va.format = VertexFormat::Float2; break;
                case 4: va.format = VertexFormat::Float4; break;
                default: va.format = VertexFormat::Float3; break;
            }
        }

        out.layout.attributes.push_back(va);
    }

    out.index_count = static_cast<uint32_t>(mesh->index_count);
    out.index_type = IndexType::Uint32;
    out.topology = (mesh->draw_mode == TC_DRAW_LINES)
        ? PrimitiveTopology::LineList
        : PrimitiveTopology::TriangleList;
    return true;
}

bool layout_has_location(const VertexBufferLayout& layout, uint32_t location) {
    return std::any_of(layout.attributes.begin(), layout.attributes.end(),
        [location](const VertexAttribute& attr) {
            return attr.location == location;
        });
}

void append_default_standard_attributes(Tgfx2MeshBinding& out, uint32_t old_stride) {
    uint32_t offset = old_stride;
    if (!layout_has_location(out.layout, 1)) {
        out.layout.attributes.push_back({1, VertexFormat::Float3, offset});
        offset += 3u * sizeof(float);
    }
    if (!layout_has_location(out.layout, 2)) {
        out.layout.attributes.push_back({2, VertexFormat::Float2, offset});
        offset += 2u * sizeof(float);
    }
    if (!layout_has_location(out.layout, 3)) {
        out.layout.attributes.push_back({3, VertexFormat::Float4, offset});
        offset += 4u * sizeof(float);
    }
    out.layout.stride = offset;
}

bool needs_default_standard_attributes(const VertexBufferLayout& layout) {
    return !layout_has_location(layout, 1) ||
           !layout_has_location(layout, 2) ||
           !layout_has_location(layout, 3);
}

BufferHandle create_augmented_vertex_buffer(
    IRenderDevice& device,
    tc_mesh* mesh,
    uint32_t new_stride
) {
    const uint32_t old_stride = mesh->layout.stride;
    if (old_stride == 0 || new_stride <= old_stride) return {};

    const auto* src = static_cast<const uint8_t*>(mesh->vertices);
    std::vector<uint8_t> vertices(
        static_cast<size_t>(mesh->vertex_count) * static_cast<size_t>(new_stride),
        0);
    for (uint32_t i = 0; i < mesh->vertex_count; ++i) {
        std::memcpy(
            vertices.data() + static_cast<size_t>(i) * new_stride,
            src + static_cast<size_t>(i) * old_stride,
            old_stride);
    }

    BufferDesc desc;
    desc.size = vertices.size();
    desc.usage = BufferUsage::Vertex | BufferUsage::CopyDst;
    BufferHandle buffer = device.create_buffer(desc);
    if (!buffer) {
        tc::Log::error("wrap_mesh_as_tgfx2: failed to allocate augmented VBO for '%s'",
                       mesh->header.name ? mesh->header.name : mesh->header.uuid);
        return {};
    }
    device.upload_buffer(buffer, std::span<const uint8_t>(vertices.data(), vertices.size()));
    return buffer;
}

} // namespace

Tgfx2MeshBinding wrap_mesh_as_tgfx2(IRenderDevice& device, tc_mesh* mesh) {
    Tgfx2MeshBinding out;
    if (!mesh) {
        tc::Log::error("wrap_mesh_as_tgfx2: null mesh");
        return out;
    }

    auto [vbo, ebo] = device.ensure_tc_mesh(mesh);
    if (!vbo || !ebo) return out;
    out.vertex_buffer = vbo;
    out.index_buffer = ebo;
    if (!fill_binding_from_mesh(out, mesh)) {
        return Tgfx2MeshBinding{};
    }

    if (needs_default_standard_attributes(out.layout)) {
        const uint32_t old_stride = out.layout.stride;
        append_default_standard_attributes(out, old_stride);
        BufferHandle augmented_vbo =
            create_augmented_vertex_buffer(device, mesh, out.layout.stride);
        if (!augmented_vbo) {
            return Tgfx2MeshBinding{};
        }
        out.vertex_buffer = augmented_vbo;
        out.destroy_vertex_buffer = true;
    }
    return out;
}

void release_mesh_binding(IRenderDevice& device, const Tgfx2MeshBinding& binding) {
    if (binding.index_count == 0) return;

    if (binding.destroy_vertex_buffer && binding.vertex_buffer) {
        device.destroy(binding.vertex_buffer);
    }
    if (binding.destroy_index_buffer && binding.index_buffer) {
        device.destroy(binding.index_buffer);
    }
}

VertexBufferLayout filter_vertex_layout_to_locations(
    const VertexBufferLayout& layout,
    std::initializer_list<uint32_t> used_locations
) {
    VertexBufferLayout out;
    out.stride = layout.stride;
    out.per_instance = layout.per_instance;
    for (const auto& attr : layout.attributes) {
        for (uint32_t loc : used_locations) {
            if (attr.location == loc) {
                out.attributes.push_back(attr);
                break;
            }
        }
    }
    return out;
}

bool draw_tc_mesh(
    RenderContext2& ctx,
    tc_mesh* mesh,
    const VertexBufferLayout* layout_override
) {
    Tgfx2MeshBinding binding = wrap_mesh_as_tgfx2(ctx.device(), mesh);
    if (binding.index_count == 0) return false;

    ctx.set_vertex_layout(layout_override ? *layout_override : binding.layout);
    ctx.set_topology(binding.topology);
    ctx.draw(binding.vertex_buffer, binding.index_buffer,
             binding.index_count, binding.index_type);

    release_mesh_binding(ctx.device(), binding);
    return true;
}

bool draw_tc_mesh(
    RenderContext2& ctx,
    tc_mesh* mesh,
    std::initializer_list<uint32_t> used_locations
) {
    Tgfx2MeshBinding binding = wrap_mesh_as_tgfx2(ctx.device(), mesh);
    if (binding.index_count == 0) return false;

    VertexBufferLayout filtered =
        filter_vertex_layout_to_locations(binding.layout, used_locations);
    ctx.set_vertex_layout(filtered);
    ctx.set_topology(binding.topology);
    ctx.draw(binding.vertex_buffer, binding.index_buffer,
             binding.index_count, binding.index_type);

    release_mesh_binding(ctx.device(), binding);
    return true;
}

} // namespace tgfx
