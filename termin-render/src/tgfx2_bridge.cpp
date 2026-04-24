#include "termin/render/tgfx2_bridge.hpp"

#include <span>
#include <string>
#include <string_view>

#include <tcbase/tc_log.hpp>

#include "tgfx/resources/tc_mesh.h"
#include "tgfx/resources/tc_texture.h"
#include "tgfx/resources/tc_texture_registry.h"
#include "tgfx/tc_gpu_context.h"
#include "tgfx/tc_gpu_share_group.h"
#include "tgfx2/i_render_device.hpp"
#include "tgfx2/opengl/opengl_render_device.hpp"
#include "tgfx2/descriptors.hpp"

extern "C" {
// tc_texture_upload_gpu is declared in tgfx_resource_gpu.h; we only
// need the forward decl since this translation unit is C++.
bool tc_texture_upload_gpu(tc_texture* tex);
}

namespace termin {

namespace {

// Translate tc_texture_format into the closest tgfx::PixelFormat. The
// tgfx2 format is only used as a pipeline cache tag here — the actual
// GL texture object is already configured by the legacy upload path, so
// a cache-key mismatch at worst causes an extra pipeline compile.
tgfx::PixelFormat tc_format_to_tgfx2(tc_texture_format fmt) {
    switch (fmt) {
        case TC_TEXTURE_RGBA8:   return tgfx::PixelFormat::RGBA8_UNorm;
        case TC_TEXTURE_RGB8:    return tgfx::PixelFormat::RGB8_UNorm;
        case TC_TEXTURE_RG8:     return tgfx::PixelFormat::RG8_UNorm;
        case TC_TEXTURE_R8:      return tgfx::PixelFormat::R8_UNorm;
        case TC_TEXTURE_RGBA16F: return tgfx::PixelFormat::RGBA16F;
        case TC_TEXTURE_RGB16F:  return tgfx::PixelFormat::RGBA16F;  // no RGB16F in tgfx2 enum
        case TC_TEXTURE_DEPTH24: return tgfx::PixelFormat::D24_UNorm_S8_UInt;
        case TC_TEXTURE_DEPTH32F: return tgfx::PixelFormat::D32F;
    }
    return tgfx::PixelFormat::RGBA8_UNorm;
}

} // anonymous namespace

namespace {

tgfx::TextureHandle wrap_tc_texture_gl(
    tgfx::OpenGLRenderDevice& device,
    tc_texture* tex
) {
    // Materialize the legacy GL texture object. Per-context upload
    // goes through tc_gpu_ops and stores the GL id on the share
    // group's texture slot at tex->header.pool_index.
    if (!tc_texture_upload_gpu(tex)) {
        tc::Log::error("wrap_tc_texture_as_tgfx2: upload failed for '%s'",
                       tex->header.name ? tex->header.name : tex->header.uuid);
        return {};
    }

    tc_gpu_context* ctx = tc_gpu_get_context();
    if (!ctx) {
        tc::Log::error("wrap_tc_texture_as_tgfx2: no active GPU context");
        return {};
    }
    tc_gpu_slot* slot = tc_gpu_context_texture_slot(ctx, tex->header.pool_index);
    if (!slot || slot->gl_id == 0) {
        tc::Log::error("wrap_tc_texture_as_tgfx2: slot has no GL id for '%s'",
                       tex->header.name ? tex->header.name : tex->header.uuid);
        return {};
    }

    tgfx::TextureDesc desc;
    desc.width = tex->width;
    desc.height = tex->height;
    desc.mip_levels = tex->mipmap ? 0 : 1;  // 0 means "respect existing"
    desc.sample_count = 1;
    desc.format = tc_format_to_tgfx2(
        static_cast<tc_texture_format>(tex->format));
    desc.usage = tgfx::TextureUsage::Sampled;
    if (tex->format == TC_TEXTURE_DEPTH24 ||
        tex->format == TC_TEXTURE_DEPTH32F)
    {
        desc.usage = desc.usage | tgfx::TextureUsage::DepthStencilAttachment;
    }

    return device.register_external_texture(
        static_cast<GLuint>(slot->gl_id),
        desc
    );
}

// Non-GL backends have no share-group slot to wrap — delegate to the
// device's per-tc_texture cache through the IRenderDevice virtual hook.
// Backends that implement it (Vulkan today) own the returned handle and
// drain it in their destructor / on registry destroy-hook fire.
// Backends that don't (OpenGL stays on the share-group path above) get
// an empty default — the GL branch above already returned by then, so we
// never actually reach this with `device.backend_type() == OpenGL`.
tgfx::TextureHandle wrap_tc_texture_non_gl(
    tgfx::IRenderDevice& device, tc_texture* tex
) {
    return device.ensure_tc_texture(tex);
}

} // anonymous namespace

tgfx::TextureHandle wrap_tc_texture_as_tgfx2(
    tgfx::IRenderDevice& device,
    tc_texture_handle handle
) {
    if (tc_texture_handle_is_invalid(handle)) return {};

    tc_texture* tex = tc_texture_get(handle);
    if (!tex) {
        tc::Log::error("wrap_tc_texture_as_tgfx2: tc_texture not found "
                       "for handle (index=%u gen=%u)",
                       handle.index, handle.generation);
        return {};
    }

    if (device.backend_type() == tgfx::BackendType::OpenGL) {
        return wrap_tc_texture_gl(
            static_cast<tgfx::OpenGLRenderDevice&>(device), tex);
    }
    return wrap_tc_texture_non_gl(device, tex);
}

void release_texture_binding(
    tgfx::IRenderDevice& device,
    tgfx::TextureHandle binding
) {
    if (!binding) return;
    if (device.backend_type() == tgfx::BackendType::OpenGL) {
        device.destroy(binding);
    }
    // Non-GL path: cache owns the handle, don't destroy.
}

namespace {

// Build the layout + index_count + topology portion of the binding
// from the tc_mesh — shared between GL and non-GL paths.
bool fill_binding_from_mesh(Tgfx2MeshBinding& out, tc_mesh* mesh) {
    out.layout.stride = mesh->layout.stride;
    out.layout.attributes.reserve(mesh->layout.attrib_count);
    for (uint8_t i = 0; i < mesh->layout.attrib_count; i++) {
        const tgfx_vertex_attrib& a = mesh->layout.attribs[i];
        tgfx::VertexAttribute va;
        va.location = a.location;
        va.offset = a.offset;

        // Map (tgfx_attrib_type, size) → tgfx::VertexFormat. Covers
        // the full 1..4 component range for FLOAT32, INT32, UINT32,
        // INT16, UINT16 and the 4-component packed int8/uint8 forms.
        // Integer attribute types read via glVertexAttribIPointer in
        // the OpenGL backend — see vertex_format_is_integer() in
        // opengl_type_conversions.cpp and the branch in
        // OpenGLCommandList::bind_vertex_buffer.
        bool ok = true;
        switch (static_cast<tgfx_attrib_type>(a.type)) {
            case TGFX_ATTRIB_FLOAT32:
                switch (a.size) {
                    case 1: va.format = tgfx::VertexFormat::Float;  break;
                    case 2: va.format = tgfx::VertexFormat::Float2; break;
                    case 3: va.format = tgfx::VertexFormat::Float3; break;
                    case 4: va.format = tgfx::VertexFormat::Float4; break;
                    default: ok = false; break;
                }
                break;
            case TGFX_ATTRIB_INT32:
                switch (a.size) {
                    case 1: va.format = tgfx::VertexFormat::Int;  break;
                    case 2: va.format = tgfx::VertexFormat::Int2; break;
                    case 3: va.format = tgfx::VertexFormat::Int3; break;
                    case 4: va.format = tgfx::VertexFormat::Int4; break;
                    default: ok = false; break;
                }
                break;
            case TGFX_ATTRIB_UINT32:
                switch (a.size) {
                    case 1: va.format = tgfx::VertexFormat::UInt;  break;
                    case 2: va.format = tgfx::VertexFormat::UInt2; break;
                    case 3: va.format = tgfx::VertexFormat::UInt3; break;
                    case 4: va.format = tgfx::VertexFormat::UInt4; break;
                    default: ok = false; break;
                }
                break;
            case TGFX_ATTRIB_INT16:
                switch (a.size) {
                    case 1: va.format = tgfx::VertexFormat::Short;  break;
                    case 2: va.format = tgfx::VertexFormat::Short2; break;
                    case 3: va.format = tgfx::VertexFormat::Short3; break;
                    case 4: va.format = tgfx::VertexFormat::Short4; break;
                    default: ok = false; break;
                }
                break;
            case TGFX_ATTRIB_UINT16:
                switch (a.size) {
                    case 1: va.format = tgfx::VertexFormat::UShort;  break;
                    case 2: va.format = tgfx::VertexFormat::UShort2; break;
                    case 3: va.format = tgfx::VertexFormat::UShort3; break;
                    case 4: va.format = tgfx::VertexFormat::UShort4; break;
                    default: ok = false; break;
                }
                break;
            case TGFX_ATTRIB_INT8:
                // 1..3-component int8 attrs are exotic; we only cover
                // the common 4-component skinning-like case.
                if (a.size == 4) {
                    va.format = tgfx::VertexFormat::Byte4;
                } else {
                    ok = false;
                }
                break;
            case TGFX_ATTRIB_UINT8:
                // UByte4 = raw integer (glVertexAttribIPointer). Legacy
                // vertex colors were historically passed as UInt8 with
                // normalization — that corresponds to UByte4N, which the
                // current tgfx1 layout helpers don't request. If a mesh
                // really needs normalized uint8, extend tgfx_attrib_type
                // with a normalized flag; for now map to the raw variant.
                if (a.size == 4) {
                    va.format = tgfx::VertexFormat::UByte4;
                } else {
                    ok = false;
                }
                break;
            default:
                ok = false;
                break;
        }

        if (!ok) {
            tc::Log::error(
                "wrap_mesh_as_tgfx2: unsupported attrib (type=%u, size=%u) "
                "for '%s' — substituting Float%u",
                unsigned(a.type), unsigned(a.size),
                mesh->header.name ? mesh->header.name : mesh->header.uuid,
                unsigned(a.size >= 1 && a.size <= 4 ? a.size : 3));
            // Fall back to a float variant so the draw at least doesn't
            // crash. Shader will read garbage but the pipeline stays alive.
            switch (a.size) {
                case 1: va.format = tgfx::VertexFormat::Float;  break;
                case 2: va.format = tgfx::VertexFormat::Float2; break;
                case 4: va.format = tgfx::VertexFormat::Float4; break;
                default: va.format = tgfx::VertexFormat::Float3; break;
            }
        }

        out.layout.attributes.push_back(va);
    }

    out.index_count = static_cast<uint32_t>(mesh->index_count);
    out.index_type = tgfx::IndexType::Uint32;
    out.topology = (mesh->draw_mode == TC_DRAW_LINES)
        ? tgfx::PrimitiveTopology::LineList
        : tgfx::PrimitiveTopology::TriangleList;

    return true;
}

// OpenGL-specific branch: materialize via the legacy share-group slot
// and register_external_buffer. Buffers are non-owning and must be
// destroy()'d by the caller (done in release_mesh_binding).
Tgfx2MeshBinding wrap_mesh_gl(
    tgfx::OpenGLRenderDevice& device, tc_mesh* mesh
) {
    Tgfx2MeshBinding out;

    if (tc_mesh_upload_gpu(mesh) == 0) {
        tc::Log::error("wrap_mesh_as_tgfx2: tc_mesh_upload_gpu failed for '%s'",
                       mesh->header.name ? mesh->header.name : mesh->header.uuid);
        return out;
    }

    tc_gpu_context* ctx = tc_gpu_get_context();
    if (!ctx || !ctx->share_group) {
        tc::Log::error("wrap_mesh_as_tgfx2: no active GPU context");
        return out;
    }
    tc_gpu_mesh_data_slot* slot = tc_gpu_share_group_mesh_data_slot(
        ctx->share_group, mesh->header.pool_index);
    if (!slot || slot->vbo == 0 || slot->ebo == 0) {
        tc::Log::error("wrap_mesh_as_tgfx2: mesh data slot has no VBO/EBO");
        return out;
    }

    tgfx::BufferDesc vb_desc;
    vb_desc.size = static_cast<uint64_t>(mesh->vertex_count) *
                   static_cast<uint64_t>(mesh->layout.stride);
    vb_desc.usage = tgfx::BufferUsage::Vertex;
    out.vertex_buffer = device.register_external_buffer(slot->vbo, vb_desc);

    tgfx::BufferDesc ib_desc;
    ib_desc.size = static_cast<uint64_t>(mesh->index_count) * sizeof(uint32_t);
    ib_desc.usage = tgfx::BufferUsage::Index;
    out.index_buffer = device.register_external_buffer(slot->ebo, ib_desc);

    if (!fill_binding_from_mesh(out, mesh)) {
        out = Tgfx2MeshBinding{};
    }
    return out;
}

} // anonymous namespace

Tgfx2MeshBinding wrap_mesh_as_tgfx2(
    tgfx::IRenderDevice& device,
    tc_mesh* mesh
) {
    Tgfx2MeshBinding out;
    if (!mesh) {
        tc::Log::error("wrap_mesh_as_tgfx2: null mesh");
        return out;
    }

    // OpenGL keeps the legacy share-group path so existing callers that
    // also use tc_mesh_draw_gpu / raw GL side-by-side stay consistent.
    if (device.backend_type() == tgfx::BackendType::OpenGL) {
        return wrap_mesh_gl(
            static_cast<tgfx::OpenGLRenderDevice&>(device), mesh);
    }

    // Non-GL backend (currently Vulkan): delegate to the device's own
    // per-tc_mesh cache through the IRenderDevice virtual hook. Buffers
    // are owned by the device and reused across frames; a mesh header
    // version bump inside ensure_tc_mesh triggers a re-upload.
    auto [vbo, ebo] = device.ensure_tc_mesh(mesh);
    if (!vbo || !ebo) return out;
    out.vertex_buffer = vbo;
    out.index_buffer  = ebo;
    if (!fill_binding_from_mesh(out, mesh)) {
        out = Tgfx2MeshBinding{};
    }
    return out;
}

void release_mesh_binding(
    tgfx::IRenderDevice& device,
    const Tgfx2MeshBinding& binding
) {
    if (binding.index_count == 0) return;

    if (device.backend_type() == tgfx::BackendType::OpenGL) {
        // Handles returned by register_external_buffer are per-call
        // non-owning wrappers around the share-group VBO/EBO. Destroy
        // them to keep the handle pool bounded; underlying GL objects
        // survive because they're marked external.
        if (binding.vertex_buffer) device.destroy(binding.vertex_buffer);
        if (binding.index_buffer)  device.destroy(binding.index_buffer);
        return;
    }

    // Non-GL path: buffers are owned by the cache, reused across frames.
    // Nothing to do per-draw.
    (void)binding;
}

tgfx::VertexBufferLayout filter_vertex_layout_to_locations(
    const tgfx::VertexBufferLayout& layout,
    std::initializer_list<uint32_t> used_locations
) {
    tgfx::VertexBufferLayout out;
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

} // namespace termin
