#include "termin/render/tgfx2_bridge.hpp"

#include <string>
#include <string_view>

#include <tcbase/tc_log.hpp>

#include "tgfx/handles.hpp"
#include "tgfx/resources/tc_mesh.h"
#include "tgfx/resources/tc_texture.h"
#include "tgfx/resources/tc_texture_registry.h"
#include "tgfx/tc_gpu_context.h"
#include "tgfx/tc_gpu_share_group.h"
#include "tgfx2/opengl/opengl_render_device.hpp"
#include "tgfx2/descriptors.hpp"

extern "C" {
// tc_texture_upload_gpu is declared in tgfx_resource_gpu.h; we only
// need the forward decl since this translation unit is C++.
bool tc_texture_upload_gpu(tc_texture* tex);
}

namespace termin {

namespace {

// Translate tc_texture_format into the closest tgfx2::PixelFormat. The
// tgfx2 format is only used as a pipeline cache tag here — the actual
// GL texture object is already configured by the legacy upload path, so
// a cache-key mismatch at worst causes an extra pipeline compile.
tgfx2::PixelFormat tc_format_to_tgfx2(tc_texture_format fmt) {
    switch (fmt) {
        case TC_TEXTURE_RGBA8:   return tgfx2::PixelFormat::RGBA8_UNorm;
        case TC_TEXTURE_RGB8:    return tgfx2::PixelFormat::RGB8_UNorm;
        case TC_TEXTURE_RG8:     return tgfx2::PixelFormat::RG8_UNorm;
        case TC_TEXTURE_R8:      return tgfx2::PixelFormat::R8_UNorm;
        case TC_TEXTURE_RGBA16F: return tgfx2::PixelFormat::RGBA16F;
        case TC_TEXTURE_RGB16F:  return tgfx2::PixelFormat::RGBA16F;  // no RGB16F in tgfx2 enum
        case TC_TEXTURE_DEPTH24: return tgfx2::PixelFormat::D24_UNorm_S8_UInt;
    }
    return tgfx2::PixelFormat::RGBA8_UNorm;
}

} // anonymous namespace

tgfx2::PixelFormat fbo_format_string_to_tgfx2(const char* format) {
    if (!format) return tgfx2::PixelFormat::RGBA8_UNorm;
    std::string_view s(format);
    if (s == "r8")      return tgfx2::PixelFormat::R8_UNorm;
    if (s == "r16f")    return tgfx2::PixelFormat::R16F;
    if (s == "r32f")    return tgfx2::PixelFormat::R32F;
    if (s == "rgba16f") return tgfx2::PixelFormat::RGBA16F;
    if (s == "rgba32f") return tgfx2::PixelFormat::RGBA32F;
    return tgfx2::PixelFormat::RGBA8_UNorm;
}

tgfx2::TextureHandle wrap_fbo_color_as_tgfx2(
    tgfx2::OpenGLRenderDevice& device,
    FramebufferHandle* fbo
) {
    if (!fbo) return {};
    GPUTextureHandle* color = fbo->color_texture();
    if (!color || !color->is_valid()) return {};

    tgfx2::TextureDesc desc;
    desc.width = static_cast<uint32_t>(fbo->get_width());
    desc.height = static_cast<uint32_t>(fbo->get_height());
    desc.format = fbo_format_string_to_tgfx2(fbo->get_format().c_str());
    desc.usage = tgfx2::TextureUsage::ColorAttachment |
                 tgfx2::TextureUsage::Sampled;
    desc.mip_levels = 1;
    desc.sample_count = static_cast<uint32_t>(fbo->get_samples() > 0 ? fbo->get_samples() : 1);

    return device.register_external_texture(
        static_cast<GLuint>(color->get_id()),
        desc
    );
}

tgfx2::TextureHandle wrap_tc_texture_as_tgfx2(
    tgfx2::OpenGLRenderDevice& device,
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

    tgfx2::TextureDesc desc;
    desc.width = tex->width;
    desc.height = tex->height;
    desc.mip_levels = tex->mipmap ? 0 : 1;  // 0 means "respect existing"
    desc.sample_count = 1;
    desc.format = tc_format_to_tgfx2(
        static_cast<tc_texture_format>(tex->format));
    desc.usage = tgfx2::TextureUsage::Sampled;
    if (tex->format == TC_TEXTURE_DEPTH24) {
        desc.usage = desc.usage | tgfx2::TextureUsage::DepthStencilAttachment;
    }

    return device.register_external_texture(
        static_cast<GLuint>(slot->gl_id),
        desc
    );
}

tgfx2::TextureHandle wrap_fbo_depth_as_tgfx2(
    tgfx2::OpenGLRenderDevice& device,
    FramebufferHandle* fbo
) {
    if (!fbo) return {};
    GPUTextureHandle* depth = fbo->depth_texture();
    if (!depth || !depth->is_valid()) return {};

    tgfx2::TextureDesc desc;
    desc.width = static_cast<uint32_t>(fbo->get_width());
    desc.height = static_cast<uint32_t>(fbo->get_height());
    // Shadow FBOs created by create_shadow_framebuffer use GL_DEPTH_COMPONENT24
    // under the hood; D32F is the closest tgfx2 PixelFormat enum value and is
    // only used here as a format tag for the pipeline cache key — the GL
    // texture object is already fully configured by the legacy path.
    desc.format = tgfx2::PixelFormat::D32F;
    desc.usage = tgfx2::TextureUsage::DepthStencilAttachment |
                 tgfx2::TextureUsage::Sampled;
    desc.mip_levels = 1;
    desc.sample_count = static_cast<uint32_t>(fbo->get_samples() > 0 ? fbo->get_samples() : 1);

    return device.register_external_texture(
        static_cast<GLuint>(depth->get_id()),
        desc
    );
}

Tgfx2MeshBinding wrap_mesh_as_tgfx2(
    tgfx2::OpenGLRenderDevice& device,
    tc_mesh* mesh
) {
    Tgfx2MeshBinding out;
    if (!mesh) {
        tc::Log::error("wrap_mesh_as_tgfx2: null mesh");
        return out;
    }

    // Materialize the share group's VBO/EBO via the legacy upload path.
    // Returns 0 on failure.
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

    tgfx2::BufferDesc vb_desc;
    vb_desc.size = static_cast<uint64_t>(mesh->vertex_count) *
                   static_cast<uint64_t>(mesh->layout.stride);
    vb_desc.usage = tgfx2::BufferUsage::Vertex;
    out.vertex_buffer = device.register_external_buffer(slot->vbo, vb_desc);

    tgfx2::BufferDesc ib_desc;
    ib_desc.size = static_cast<uint64_t>(mesh->index_count) * sizeof(uint32_t);
    ib_desc.usage = tgfx2::BufferUsage::Index;
    out.index_buffer = device.register_external_buffer(slot->ebo, ib_desc);

    out.layout.stride = mesh->layout.stride;
    out.layout.attributes.reserve(mesh->layout.attrib_count);
    for (uint8_t i = 0; i < mesh->layout.attrib_count; i++) {
        const tgfx_vertex_attrib& a = mesh->layout.attribs[i];
        tgfx2::VertexAttribute va;
        va.location = a.location;
        va.offset = a.offset;

        // Map (tgfx_attrib_type, size) → tgfx2::VertexFormat. Covers
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
                    case 1: va.format = tgfx2::VertexFormat::Float;  break;
                    case 2: va.format = tgfx2::VertexFormat::Float2; break;
                    case 3: va.format = tgfx2::VertexFormat::Float3; break;
                    case 4: va.format = tgfx2::VertexFormat::Float4; break;
                    default: ok = false; break;
                }
                break;
            case TGFX_ATTRIB_INT32:
                switch (a.size) {
                    case 1: va.format = tgfx2::VertexFormat::Int;  break;
                    case 2: va.format = tgfx2::VertexFormat::Int2; break;
                    case 3: va.format = tgfx2::VertexFormat::Int3; break;
                    case 4: va.format = tgfx2::VertexFormat::Int4; break;
                    default: ok = false; break;
                }
                break;
            case TGFX_ATTRIB_UINT32:
                switch (a.size) {
                    case 1: va.format = tgfx2::VertexFormat::UInt;  break;
                    case 2: va.format = tgfx2::VertexFormat::UInt2; break;
                    case 3: va.format = tgfx2::VertexFormat::UInt3; break;
                    case 4: va.format = tgfx2::VertexFormat::UInt4; break;
                    default: ok = false; break;
                }
                break;
            case TGFX_ATTRIB_INT16:
                switch (a.size) {
                    case 1: va.format = tgfx2::VertexFormat::Short;  break;
                    case 2: va.format = tgfx2::VertexFormat::Short2; break;
                    case 3: va.format = tgfx2::VertexFormat::Short3; break;
                    case 4: va.format = tgfx2::VertexFormat::Short4; break;
                    default: ok = false; break;
                }
                break;
            case TGFX_ATTRIB_UINT16:
                switch (a.size) {
                    case 1: va.format = tgfx2::VertexFormat::UShort;  break;
                    case 2: va.format = tgfx2::VertexFormat::UShort2; break;
                    case 3: va.format = tgfx2::VertexFormat::UShort3; break;
                    case 4: va.format = tgfx2::VertexFormat::UShort4; break;
                    default: ok = false; break;
                }
                break;
            case TGFX_ATTRIB_INT8:
                // 1..3-component int8 attrs are exotic; we only cover
                // the common 4-component skinning-like case.
                if (a.size == 4) {
                    va.format = tgfx2::VertexFormat::Byte4;
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
                    va.format = tgfx2::VertexFormat::UByte4;
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
                case 1: va.format = tgfx2::VertexFormat::Float;  break;
                case 2: va.format = tgfx2::VertexFormat::Float2; break;
                case 4: va.format = tgfx2::VertexFormat::Float4; break;
                default: va.format = tgfx2::VertexFormat::Float3; break;
            }
        }

        out.layout.attributes.push_back(va);
    }

    out.index_count = static_cast<uint32_t>(mesh->index_count);
    out.index_type = tgfx2::IndexType::Uint32;
    out.topology = (mesh->draw_mode == TC_DRAW_LINES)
        ? tgfx2::PrimitiveTopology::LineList
        : tgfx2::PrimitiveTopology::TriangleList;

    return out;
}

} // namespace termin
