#include "tgfx2/d3d11/d3d11_render_device.hpp"

#include "tgfx2/d3d11/d3d11_type_conversions.hpp"
#include "tgfx2/pixel_format_utils.hpp"
#include "tgfx2/tc_texture_upload.hpp"

#include "tgfx2/tc_mesh_bridge.hpp"
#include "tgfx2/tc_shader_bridge.hpp"

#include <cstring>
#include <span>
#include <string>
#include <vector>

#include <tcbase/tc_log.hpp>

extern "C" {
#include "tgfx/resources/tc_mesh.h"
#include "tgfx/resources/tc_shader.h"
#include "tgfx/resources/tc_texture.h"
#include "tgfx/resources/tc_texture_registry.h"
}

namespace tgfx {
    namespace {
        TextureUsage tc_usage_to_tgfx(uint32_t usage) {
            uint32_t out = 0;
            if (usage & TC_TEXTURE_USAGE_SAMPLED)
                out |= static_cast<uint32_t>(TextureUsage::Sampled);
            if (usage & TC_TEXTURE_USAGE_COLOR_ATTACHMENT)
                out |= static_cast<uint32_t>(TextureUsage::ColorAttachment);
            if (usage & TC_TEXTURE_USAGE_DEPTH_ATTACHMENT)
                out |= static_cast<uint32_t>(TextureUsage::DepthStencilAttachment);
            if (usage & TC_TEXTURE_USAGE_COPY_SRC)
                out |= static_cast<uint32_t>(TextureUsage::CopySrc);
            if (usage & TC_TEXTURE_USAGE_COPY_DST)
                out |= static_cast<uint32_t>(TextureUsage::CopyDst);
            return static_cast<TextureUsage>(out);
        }

    } // namespace

    bool D3D11RenderDevice::ensure_tc_shader(tc_shader* shader, ShaderHandle* out_vs, ShaderHandle* out_fs) {
        if (!shader) {
            tc::Log::error("D3D11RenderDevice::ensure_tc_shader: shader is NULL");
            return false;
        }
        if (!out_fs) {
            tc::Log::error("D3D11RenderDevice::ensure_tc_shader: out_fs is NULL");
            return false;
        }
        if (!shader->fragment_source) {
            tc::Log::error("D3D11RenderDevice::ensure_tc_shader: missing fragment_source for '%s'",
                           shader->name ? shader->name : shader->uuid);
            return false;
        }

        const bool has_vs = shader->vertex_source && shader->vertex_source[0] != '\0';
        const uint32_t pool_index = shader->pool_index;
        const uint32_t version = shader->version;
        const auto& resolver = shader_artifact_resolver();
        const uint64_t resolver_revision = resolver.revision();
        const bool resource_layout_ready =
            tc_shader_has_resource_layout(shader) || !tc_shader_requires_artifacts(shader);

        auto it = tc_shader_cache_.find(pool_index);
        if (it != tc_shader_cache_.end() && it->second.version == version &&
            it->second.resolver_revision == resolver_revision && it->second.has_vs == has_vs && it->second.fs &&
            (!has_vs || it->second.vs) && resource_layout_ready) {
            if (out_vs)
                *out_vs = it->second.vs;
            *out_fs = it->second.fs;
            return true;
        }
        if (it != tc_shader_cache_.end()) {
            if (it->second.vs)
                destroy(it->second.vs);
            if (it->second.fs)
                destroy(it->second.fs);
            tc_shader_cache_.erase(it);
        }

        ShaderHandle vs;
        if (has_vs) {
            ShaderDesc vs_desc;
            vs_desc.stage = ShaderStage::Vertex;
            vs_desc.debug_name = std::string(shader->name ? shader->name : shader->uuid) + ":vertex";
            if (shader->vertex_entry && shader->vertex_entry[0]) {
                vs_desc.entry_point = shader->vertex_entry;
            }
            if (!termin::tgfx2_load_or_compile_shader_artifact_for_backend(
                    resolver, shader, BackendType::D3D11, vs_desc.stage, vs_desc.bytecode)) {
                tc::Log::error(
                    "D3D11RenderDevice::ensure_tc_shader: vertex .cso artifact missing or dev compile failed for '%s'",
                    shader->name ? shader->name : shader->uuid);
                return false;
            }
            vs = create_shader(vs_desc);
            if (!vs) {
                tc::Log::error("D3D11RenderDevice::ensure_tc_shader: VS create failed for '%s'",
                               shader->name ? shader->name : shader->uuid);
                return false;
            }
        }

        ShaderDesc fs_desc;
        fs_desc.stage = ShaderStage::Fragment;
        fs_desc.debug_name = std::string(shader->name ? shader->name : shader->uuid) + ":fragment";
        if (shader->fragment_entry && shader->fragment_entry[0]) {
            fs_desc.entry_point = shader->fragment_entry;
        }
        if (!termin::tgfx2_load_or_compile_shader_artifact_for_backend(
                resolver, shader, BackendType::D3D11, fs_desc.stage, fs_desc.bytecode)) {
            if (vs)
                destroy(vs);
            tc::Log::error(
                "D3D11RenderDevice::ensure_tc_shader: fragment .cso artifact missing or dev compile failed for '%s'",
                shader->name ? shader->name : shader->uuid);
            return false;
        }
        ShaderHandle fs = create_shader(fs_desc);
        if (!fs) {
            if (vs)
                destroy(vs);
            tc::Log::error("D3D11RenderDevice::ensure_tc_shader: PS create failed for '%s'",
                           shader->name ? shader->name : shader->uuid);
            return false;
        }

        CachedTcShaderEntry entry;
        entry.vs = vs;
        entry.fs = fs;
        entry.version = version;
        entry.resolver_revision = resolver_revision;
        entry.has_vs = has_vs;
        tc_shader_cache_.emplace(pool_index, entry);

        if (out_vs)
            *out_vs = vs;
        *out_fs = fs;
        return true;
    }

    void D3D11RenderDevice::invalidate_tc_shader_cache(uint32_t pool_index) {
        auto it = tc_shader_cache_.find(pool_index);
        if (it == tc_shader_cache_.end())
            return;
        if (it->second.vs)
            destroy(it->second.vs);
        if (it->second.fs)
            destroy(it->second.fs);
        tc_shader_cache_.erase(it);
    }

    TextureHandle D3D11RenderDevice::ensure_tc_texture(tc_texture* tex) {
        if (!tex)
            return {};

        if (!tex->header.is_loaded) {
            tc_texture_ensure_loaded_ptr(tex);
        }

        const bool gpu_first = tex->storage_kind == TC_TEXTURE_STORAGE_GPU_FIRST;
        if (tex->width == 0 || tex->height == 0) {
            tc::Log::error("D3D11RenderDevice::ensure_tc_texture: tc_texture '%s' has zero size",
                           tex->header.name ? tex->header.name : tex->header.uuid);
            return {};
        }
        if (!gpu_first && !tex->data) {
            tc::Log::error("D3D11RenderDevice::ensure_tc_texture: tc_texture '%s' has no CPU pixels",
                           tex->header.name ? tex->header.name : tex->header.uuid);
            return {};
        }

        const uint32_t pool_index = tex->header.pool_index;
        const uint32_t version = tex->header.version;
        auto it = tc_texture_cache_.find(pool_index);
        if (it != tc_texture_cache_.end() && it->second.version == version) {
            return it->second.handle;
        }
        if (it != tc_texture_cache_.end()) {
            if (it->second.handle)
                destroy(it->second.handle);
            tc_texture_cache_.erase(it);
        }

        TextureDesc desc;
        desc.width = tex->width;
        desc.height = tex->height;
        desc.mip_levels = 1;
        desc.sample_count = 1;

        if (gpu_first) {
            if (tex->mipmap) {
                tc::Log::error("D3D11RenderDevice::ensure_tc_texture: GPU-first texture '%s' requests mipmaps without "
                               "a source chain",
                               tex->header.name ? tex->header.name : tex->header.uuid);
                return {};
            }
            desc.format = pixel_format_for_tc_texture(static_cast<tc_texture_format>(tex->format),
                                                      static_cast<tc_texture_encoding>(tex->encoding));
            if (desc.format == PixelFormat::Undefined) {
                tc::Log::error("D3D11RenderDevice::ensure_tc_texture: GPU-first texture '%s' has unsupported "
                               "format/encoding %u/%u",
                               tex->header.name ? tex->header.name : tex->header.uuid,
                               static_cast<unsigned>(tex->format),
                               static_cast<unsigned>(tex->encoding));
                return {};
            }
            desc.usage = tc_usage_to_tgfx(tex->usage) | TextureUsage::CopyDst;
            if (static_cast<uint32_t>(desc.usage) == static_cast<uint32_t>(TextureUsage::CopyDst)) {
                desc.usage = desc.usage | TextureUsage::Sampled;
            }

            TextureHandle handle = create_texture(desc);
            if (!handle) {
                tc::Log::error("D3D11RenderDevice::ensure_tc_texture: GPU-first create_texture failed for '%s'",
                               tex->header.name ? tex->header.name : tex->header.uuid);
                return {};
            }
            tc_texture_cache_.emplace(pool_index, CachedTcTextureEntry{handle, version});
            return handle;
        }

        TcTextureUpload upload;
        if (!prepare_tc_texture_upload(tex, upload)) {
            return {};
        }

        desc.format = upload.format;
        desc.mip_levels = static_cast<uint32_t>(upload.levels.size());
        desc.usage = TextureUsage::Sampled | TextureUsage::CopySrc | TextureUsage::CopyDst;
        TextureHandle handle = create_texture(desc);
        if (!handle) {
            tc::Log::error("D3D11RenderDevice::ensure_tc_texture: create_texture failed for '%s'",
                           tex->header.name ? tex->header.name : tex->header.uuid);
            return {};
        }

        for (uint32_t mip = 0; mip < upload.levels.size(); ++mip) {
            const auto& pixels = upload.levels[mip];
            upload_texture(handle, std::span<const uint8_t>(pixels.data(), pixels.size()), mip);
        }
        tc_texture_cache_.emplace(pool_index, CachedTcTextureEntry{handle, version});
        return handle;
    }

    void D3D11RenderDevice::invalidate_tc_texture_cache(uint32_t pool_index) {
        auto it = tc_texture_cache_.find(pool_index);
        if (it == tc_texture_cache_.end())
            return;
        if (it->second.handle)
            destroy(it->second.handle);
        tc_texture_cache_.erase(it);
    }

    std::pair<BufferHandle, BufferHandle> D3D11RenderDevice::ensure_tc_mesh(tc_mesh* mesh) {
        if (!mesh)
            return {};

        if (!mesh->vertices || mesh->vertex_count == 0 || !mesh->indices || mesh->index_count == 0 ||
            mesh->layout.stride == 0) {
            tc::Log::error("D3D11RenderDevice::ensure_tc_mesh: tc_mesh '%s' has no CPU mesh data",
                           mesh->header.name ? mesh->header.name : mesh->header.uuid);
            return {};
        }

        const uint32_t pool_index = mesh->header.pool_index;
        const uint32_t version = mesh->header.version;
        auto it = tc_mesh_cache_.find(pool_index);
        if (it != tc_mesh_cache_.end() && it->second.version == version) {
            return {it->second.vbo, it->second.ebo};
        }
        if (it != tc_mesh_cache_.end()) {
            if (it->second.vbo)
                destroy(it->second.vbo);
            if (it->second.ebo)
                destroy(it->second.ebo);
            tc_mesh_cache_.erase(it);
        }

        const size_t vb_size = tc_mesh_vertices_size(mesh);
        const size_t ib_size = tc_mesh_indices_size(mesh);

        BufferDesc vb_desc;
        vb_desc.size = vb_size;
        vb_desc.usage = BufferUsage::Vertex | BufferUsage::CopyDst;
        BufferHandle vbo = create_buffer(vb_desc);
        if (!vbo) {
            tc::Log::error("D3D11RenderDevice::ensure_tc_mesh: failed to allocate VBO for '%s'",
                           mesh->header.name ? mesh->header.name : mesh->header.uuid);
            return {};
        }
        upload_buffer(vbo, std::span<const uint8_t>(static_cast<const uint8_t*>(mesh->vertices), vb_size));

        BufferDesc ib_desc;
        ib_desc.size = ib_size;
        ib_desc.usage = BufferUsage::Index | BufferUsage::CopyDst;
        BufferHandle ebo = create_buffer(ib_desc);
        if (!ebo) {
            tc::Log::error("D3D11RenderDevice::ensure_tc_mesh: failed to allocate EBO for '%s'",
                           mesh->header.name ? mesh->header.name : mesh->header.uuid);
            destroy(vbo);
            return {};
        }
        upload_buffer(ebo, std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(mesh->indices), ib_size));

        tc_mesh_cache_.emplace(pool_index, CachedTcMeshEntry{vbo, ebo, version});
        return {vbo, ebo};
    }

    void D3D11RenderDevice::invalidate_tc_mesh_cache(uint32_t pool_index) {
        auto it = tc_mesh_cache_.find(pool_index);
        if (it == tc_mesh_cache_.end())
            return;
        if (it->second.vbo)
            destroy(it->second.vbo);
        if (it->second.ebo)
            destroy(it->second.ebo);
        tc_mesh_cache_.erase(it);
    }

} // namespace tgfx
