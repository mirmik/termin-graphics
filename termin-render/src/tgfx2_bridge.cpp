#include "termin/render/tgfx2_bridge.hpp"

#include <string>
#include <string_view>

#include <tcbase/tc_log.hpp>

#include "tgfx/resources/tc_texture.h"
#include "tgfx/resources/tc_texture_registry.h"
#include "tgfx/tc_gpu_context.h"
#include "tgfx/tc_gpu_share_group.h"
#include "tgfx2/i_render_device.hpp"
#ifdef TGFX2_HAS_OPENGL
#include "tgfx2/opengl/opengl_render_device.hpp"
#endif
#include "tgfx2/descriptors.hpp"

extern "C" {
// tc_texture_upload_gpu is declared in tgfx_resource_gpu.h; we only
// need the forward decl since this translation unit is C++.
bool tc_texture_upload_gpu(tc_texture* tex);
}

namespace termin {

namespace {

// Translate tc_texture_format into the closest tgfx::PixelFormat. The
// tgfx2 format is only used as a pipeline cache tag here. On OpenGL the
// actual texture object is configured by the core_c upload path, so a
// cache-key mismatch at worst causes an extra pipeline compile.
tgfx::PixelFormat tc_format_to_tgfx2(tc_texture_format fmt) {
    switch (fmt) {
        case TC_TEXTURE_RGBA8:   return tgfx::PixelFormat::RGBA8_UNorm;
        case TC_TEXTURE_RGB8:    return tgfx::PixelFormat::RGB8_UNorm;
        case TC_TEXTURE_RG8:     return tgfx::PixelFormat::RG8_UNorm;
        case TC_TEXTURE_R8:      return tgfx::PixelFormat::R8_UNorm;
        case TC_TEXTURE_RGBA16F: return tgfx::PixelFormat::RGBA16F;
        case TC_TEXTURE_RGB16F:  return tgfx::PixelFormat::RGBA16F;  // no RGB16F in tgfx2 enum
        case TC_TEXTURE_R16F:    return tgfx::PixelFormat::R16F;
        case TC_TEXTURE_R32F:    return tgfx::PixelFormat::R32F;
        case TC_TEXTURE_DEPTH24: return tgfx::PixelFormat::D24_UNorm_S8_UInt;
        case TC_TEXTURE_DEPTH32F: return tgfx::PixelFormat::D32F;
    }
    return tgfx::PixelFormat::RGBA8_UNorm;
}

} // anonymous namespace

namespace {

#ifdef TGFX2_HAS_OPENGL
tgfx::TextureHandle wrap_tc_texture_gl(
    tgfx::OpenGLRenderDevice& device,
    tc_texture* tex
) {
    // Materialize the core_c GL texture object. Per-context upload goes
    // through tc_gpu_ops and stores the GL id on the share group's
    // texture slot at tex->header.pool_index.
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
    if (tex->usage & TC_TEXTURE_USAGE_COPY_SRC) {
        desc.usage = desc.usage | tgfx::TextureUsage::CopySrc;
    }
    if (tex->usage & TC_TEXTURE_USAGE_COPY_DST) {
        desc.usage = desc.usage | tgfx::TextureUsage::CopyDst;
    }
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
#endif

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
#ifdef TGFX2_HAS_OPENGL
        return wrap_tc_texture_gl(
            static_cast<tgfx::OpenGLRenderDevice&>(device), tex);
#else
        tc::Log::error("wrap_tc_texture_as_tgfx2: OpenGL backend not compiled");
        return {};
#endif
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

Tgfx2MeshBinding wrap_mesh_as_tgfx2(
    tgfx::IRenderDevice& device,
    tc_mesh* mesh
) {
    return tgfx::wrap_mesh_as_tgfx2(device, mesh);
}

void release_mesh_binding(
    tgfx::IRenderDevice& device,
    const Tgfx2MeshBinding& binding
) {
    tgfx::release_mesh_binding(device, binding);
}

tgfx::VertexBufferLayout filter_vertex_layout_to_locations(
    const tgfx::VertexBufferLayout& layout,
    std::initializer_list<uint32_t> used_locations
) {
    return tgfx::filter_vertex_layout_to_locations(layout, used_locations);
}

bool draw_tc_mesh(
    tgfx::RenderContext2& ctx,
    tc_mesh* mesh,
    const tgfx::VertexBufferLayout* layout_override
) {
    return tgfx::draw_tc_mesh(ctx, mesh, layout_override);
}

bool draw_tc_mesh(
    tgfx::RenderContext2& ctx,
    tc_mesh* mesh,
    std::initializer_list<uint32_t> used_locations
) {
    return tgfx::draw_tc_mesh(ctx, mesh, used_locations);
}

} // namespace termin
