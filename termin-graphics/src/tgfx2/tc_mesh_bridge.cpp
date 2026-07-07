#include "tgfx2/tc_mesh_bridge.hpp"

#include <algorithm>
#include <span>
#include <string_view>

#include <tcbase/tc_log.hpp>

#include "tgfx/resources/tc_mesh.h"
#include "tgfx/resources/tc_mesh_registry.h"
#include "tgfx2/descriptors.hpp"
#include "tgfx2/i_render_device.hpp"
#include "tgfx2/render_context.hpp"

namespace tgfx {
namespace {

bool fill_binding_from_mesh(Tgfx2MeshBinding& out, tc_mesh* mesh) {
    out.layout_desc.stride = mesh->layout.stride;
    out.layout_desc.use_shader_input_locations =
        mesh->layout.use_shader_input_locations != 0;
    out.layout_desc.attribute_count = std::min<uint32_t>(
        mesh->layout.attrib_count,
        TGFX2_VERTEX_ATTRIBUTE_MAX);
    if (mesh->layout.attrib_count > TGFX2_VERTEX_ATTRIBUTE_MAX) {
        tc::Log::error(
            "wrap_mesh_as_tgfx2: mesh '%s' has %u attributes, max is %u; truncating",
            mesh->header.name ? mesh->header.name : mesh->header.uuid,
            unsigned(mesh->layout.attrib_count),
            TGFX2_VERTEX_ATTRIBUTE_MAX);
    }
    for (uint8_t i = 0; i < mesh->layout.attrib_count; i++) {
        if (i >= TGFX2_VERTEX_ATTRIBUTE_MAX) {
            break;
        }
        const tgfx_vertex_attrib& a = mesh->layout.attribs[i];
        VertexAttributeDesc va;
        va.location = a.location;
        va.offset = a.offset;
        va.semantic = intern_vertex_semantic(a.name);

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

        out.layout_desc.attributes[i] = va;
    }

    out.index_count = static_cast<uint32_t>(mesh->index_count);
    out.index_type = IndexType::Uint32;
    out.topology = (mesh->draw_mode == TC_DRAW_LINES)
        ? PrimitiveTopology::LineList
        : PrimitiveTopology::TriangleList;
    return true;
}

bool semantic_in_list(
    std::string_view semantic,
    std::initializer_list<std::string_view> used_semantics
) {
    for (std::string_view requested : used_semantics) {
        if (semantic == requested) {
            return true;
        }
    }
    return false;
}

const tc_submesh* validate_submesh_range(tc_mesh* mesh, size_t submesh_index) {
    if (!mesh) return nullptr;
    if (mesh->submesh_count == 0 && !tc_mesh_ensure_default_submesh(mesh)) {
        return nullptr;
    }
    const tc_submesh* submesh = tc_mesh_get_submesh(mesh, submesh_index);
    if (!submesh) {
        tc::Log::error(
            "draw_tc_submesh: mesh '%s' has no submesh %zu (count=%zu)",
            mesh->header.name ? mesh->header.name : mesh->header.uuid,
            submesh_index,
            mesh->submesh_count);
        return nullptr;
    }
    if (submesh->index_count == 0 ||
        static_cast<size_t>(submesh->first_index) > mesh->index_count ||
        static_cast<size_t>(submesh->index_count) >
            mesh->index_count - static_cast<size_t>(submesh->first_index)) {
        tc::Log::error(
            "draw_tc_submesh: invalid range for mesh '%s' submesh=%zu first=%u count=%u mesh_indices=%zu",
            mesh->header.name ? mesh->header.name : mesh->header.uuid,
            submesh_index,
            submesh->first_index,
            submesh->index_count,
            mesh->index_count);
        return nullptr;
    }
    return submesh;
}

bool draw_tc_submesh_binding(
    RenderContext2& ctx,
    const Tgfx2MeshBinding& binding,
    const tc_submesh& submesh,
    const VertexLayoutDesc* layout_override
) {
    if (layout_override) {
        ctx.set_vertex_layout(*layout_override);
    } else {
        ctx.set_vertex_layout(binding.layout_desc);
    }
    ctx.set_topology(
        submesh.draw_mode == TC_DRAW_LINES
            ? PrimitiveTopology::LineList
            : PrimitiveTopology::TriangleList);
    ctx.draw(
        binding.vertex_buffer,
        binding.index_buffer,
        static_cast<uint64_t>(submesh.first_index) * sizeof(uint32_t),
        submesh.index_count,
        submesh.vertex_offset,
        binding.index_type);
    return true;
}

} // namespace

std::string_view standard_vertex_semantic_for_location(uint32_t location) {
    switch (location) {
        case 0: return "position";
        case 1: return "normal";
        case 2: return "uv";
        case 3: return "tangent";
        case 4: return "joints";
        case 5: return "weights";
        case 6: return "color";
        default: return {};
    }
}

std::string_view vertex_attribute_semantic(const VertexAttributeDesc& attr) {
    if (attr.semantic && attr.semantic[0] != '\0') {
        return attr.semantic;
    }
    return standard_vertex_semantic_for_location(attr.location);
}

bool vertex_layout_has_semantic(
    const VertexLayoutDesc& layout,
    std::string_view semantic
) {
    const uint32_t attribute_count = std::min(
        layout.attribute_count,
        TGFX2_VERTEX_ATTRIBUTE_MAX);
    for (uint32_t i = 0; i < attribute_count; ++i) {
        if (vertex_attribute_semantic(layout.attributes[i]) == semantic) {
            return true;
        }
    }
    return false;
}

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

VertexLayoutDesc filter_vertex_layout_to_locations(
    const VertexLayoutDesc& layout,
    std::initializer_list<uint32_t> used_locations,
    bool use_shader_input_locations
) {
    VertexLayoutDesc out;
    out.stride = layout.stride;
    out.per_instance = layout.per_instance;
    out.use_shader_input_locations = use_shader_input_locations;
    const uint32_t attribute_count = std::min(
        layout.attribute_count,
        TGFX2_VERTEX_ATTRIBUTE_MAX);
    for (uint32_t i = 0; i < attribute_count; ++i) {
        const VertexAttributeDesc& attr = layout.attributes[i];
        for (uint32_t loc : used_locations) {
            if (attr.location == loc) {
                if (out.attribute_count >= TGFX2_VERTEX_ATTRIBUTE_MAX) {
                    tc::Log::error(
                        "filter_vertex_layout_to_locations: output attribute overflow");
                    return out;
                }
                out.attributes[out.attribute_count++] = attr;
                break;
            }
        }
    }
    return out;
}

VertexLayoutDesc filter_vertex_layout_to_semantics(
    const VertexLayoutDesc& layout,
    std::initializer_list<std::string_view> used_semantics,
    bool use_shader_input_locations
) {
    VertexLayoutDesc out;
    out.stride = layout.stride;
    out.per_instance = layout.per_instance;
    out.use_shader_input_locations = use_shader_input_locations;
    const uint32_t attribute_count = std::min(
        layout.attribute_count,
        TGFX2_VERTEX_ATTRIBUTE_MAX);
    for (uint32_t i = 0; i < attribute_count; ++i) {
        const VertexAttributeDesc& attr = layout.attributes[i];
        if (semantic_in_list(vertex_attribute_semantic(attr), used_semantics)) {
            if (out.attribute_count >= TGFX2_VERTEX_ATTRIBUTE_MAX) {
                tc::Log::error(
                    "filter_vertex_layout_to_semantics: output attribute overflow");
                return out;
            }
            out.attributes[out.attribute_count++] = attr;
        }
    }
    for (std::string_view semantic : used_semantics) {
        if (!vertex_layout_has_semantic(out, semantic)) {
            tc::Log::error(
                "filter_vertex_layout_to_semantics: layout has no '%.*s' attribute",
                static_cast<int>(semantic.size()),
                semantic.data());
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

    const VertexLayoutDesc override_desc =
        layout_override ? make_vertex_layout_desc(*layout_override) : VertexLayoutDesc{};
    ctx.set_vertex_layout(layout_override ? override_desc : binding.layout_desc);
    ctx.set_topology(binding.topology);
    ctx.draw(binding.vertex_buffer, binding.index_buffer,
             binding.index_count, binding.index_type);

    release_mesh_binding(ctx.device(), binding);
    return true;
}

bool draw_tc_mesh(
    RenderContext2& ctx,
    tc_mesh* mesh,
    const VertexLayoutDesc* layout_override
) {
    Tgfx2MeshBinding binding = wrap_mesh_as_tgfx2(ctx.device(), mesh);
    if (binding.index_count == 0) return false;

    ctx.set_vertex_layout(layout_override ? *layout_override : binding.layout_desc);
    ctx.set_topology(binding.topology);
    ctx.draw(binding.vertex_buffer, binding.index_buffer,
             binding.index_count, binding.index_type);

    release_mesh_binding(ctx.device(), binding);
    return true;
}

bool draw_tc_submesh(
    RenderContext2& ctx,
    tc_mesh* mesh,
    size_t submesh_index,
    const VertexBufferLayout* layout_override
) {
    const tc_submesh* submesh = validate_submesh_range(mesh, submesh_index);
    if (!submesh) {
        return false;
    }

    Tgfx2MeshBinding binding = wrap_mesh_as_tgfx2(ctx.device(), mesh);
    if (binding.index_count == 0) return false;

    const VertexLayoutDesc override_desc =
        layout_override ? make_vertex_layout_desc(*layout_override) : VertexLayoutDesc{};
    draw_tc_submesh_binding(
        ctx,
        binding,
        *submesh,
        layout_override ? &override_desc : nullptr);
    release_mesh_binding(ctx.device(), binding);
    return true;
}

bool draw_tc_submesh(
    RenderContext2& ctx,
    tc_mesh* mesh,
    size_t submesh_index,
    const VertexLayoutDesc* layout_override
) {
    const tc_submesh* submesh = validate_submesh_range(mesh, submesh_index);
    if (!submesh) {
        return false;
    }

    Tgfx2MeshBinding binding = wrap_mesh_as_tgfx2(ctx.device(), mesh);
    if (binding.index_count == 0) return false;

    draw_tc_submesh_binding(ctx, binding, *submesh, layout_override);
    release_mesh_binding(ctx.device(), binding);
    return true;
}

bool draw_tc_mesh(
    RenderContext2& ctx,
    tc_mesh* mesh,
    std::initializer_list<uint32_t> used_locations,
    bool use_shader_input_locations
) {
    Tgfx2MeshBinding binding = wrap_mesh_as_tgfx2(ctx.device(), mesh);
    if (binding.index_count == 0) return false;

    VertexLayoutDesc filtered =
        filter_vertex_layout_to_locations(
            binding.layout_desc,
            used_locations,
            use_shader_input_locations);
    ctx.set_vertex_layout(filtered);
    ctx.set_topology(binding.topology);
    ctx.draw(binding.vertex_buffer, binding.index_buffer,
             binding.index_count, binding.index_type);

    release_mesh_binding(ctx.device(), binding);
    return true;
}

bool draw_tc_submesh(
    RenderContext2& ctx,
    tc_mesh* mesh,
    size_t submesh_index,
    std::initializer_list<uint32_t> used_locations,
    bool use_shader_input_locations
) {
    const tc_submesh* submesh = validate_submesh_range(mesh, submesh_index);
    if (!submesh) {
        return false;
    }

    Tgfx2MeshBinding binding = wrap_mesh_as_tgfx2(ctx.device(), mesh);
    if (binding.index_count == 0) return false;

    VertexLayoutDesc filtered =
        filter_vertex_layout_to_locations(
            binding.layout_desc,
            used_locations,
            use_shader_input_locations);
    draw_tc_submesh_binding(ctx, binding, *submesh, &filtered);
    release_mesh_binding(ctx.device(), binding);
    return true;
}

bool draw_tc_mesh(
    RenderContext2& ctx,
    tc_mesh* mesh,
    std::initializer_list<std::string_view> used_semantics,
    bool use_shader_input_locations
) {
    Tgfx2MeshBinding binding = wrap_mesh_as_tgfx2(ctx.device(), mesh);
    if (binding.index_count == 0) return false;

    VertexLayoutDesc filtered =
        filter_vertex_layout_to_semantics(
            binding.layout_desc,
            used_semantics,
            use_shader_input_locations);
    ctx.set_vertex_layout(filtered);
    ctx.set_topology(binding.topology);
    ctx.draw(binding.vertex_buffer, binding.index_buffer,
             binding.index_count, binding.index_type);

    release_mesh_binding(ctx.device(), binding);
    return true;
}

bool draw_tc_submesh(
    RenderContext2& ctx,
    tc_mesh* mesh,
    size_t submesh_index,
    std::initializer_list<std::string_view> used_semantics,
    bool use_shader_input_locations
) {
    const tc_submesh* submesh = validate_submesh_range(mesh, submesh_index);
    if (!submesh) {
        return false;
    }

    Tgfx2MeshBinding binding = wrap_mesh_as_tgfx2(ctx.device(), mesh);
    if (binding.index_count == 0) return false;

    VertexLayoutDesc filtered =
        filter_vertex_layout_to_semantics(
            binding.layout_desc,
            used_semantics,
            use_shader_input_locations);
    draw_tc_submesh_binding(ctx, binding, *submesh, &filtered);
    release_mesh_binding(ctx.device(), binding);
    return true;
}

} // namespace tgfx
