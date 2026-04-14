#include "termin/render/tgfx2_bridge.hpp"

#include <string>
#include <string_view>

#include <tcbase/tc_log.hpp>

#include "tgfx/handles.hpp"
#include "tgfx/resources/tc_mesh.h"
#include "tgfx/tc_gpu_context.h"
#include "tgfx/tc_gpu_share_group.h"
#include "tgfx2/opengl/opengl_render_device.hpp"
#include "tgfx2/descriptors.hpp"

namespace termin {

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
        // tgfx2::VertexFormat currently only enumerates float variants
        // (Float, Float2, Float3, Float4) and UByte4{,N}. The layouts
        // produced by tc_vertex_layout_pos* helpers are all FLOAT32 with
        // size 1..4, which matches cleanly. Integer attribute types
        // (used only by skinning joint indices today) need a VertexFormat
        // extension before Stage 5.B can wire skinning through tgfx2.
        tgfx2::VertexAttribute va;
        va.location = a.location;
        va.offset = a.offset;
        switch (a.size) {
            case 1: va.format = tgfx2::VertexFormat::Float;  break;
            case 2: va.format = tgfx2::VertexFormat::Float2; break;
            case 3: va.format = tgfx2::VertexFormat::Float3; break;
            case 4: va.format = tgfx2::VertexFormat::Float4; break;
            default:
                tc::Log::error("wrap_mesh_as_tgfx2: unsupported attrib size %u for '%s'",
                               unsigned(a.size),
                               mesh->header.name ? mesh->header.name : mesh->header.uuid);
                va.format = tgfx2::VertexFormat::Float3;
                break;
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
