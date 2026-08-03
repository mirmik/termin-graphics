#include "tgfx2/webgpu/webgpu_render_device.hpp"

#include "tgfx2/pixel_format_utils.hpp"
#include "tgfx2/tc_mesh_bridge.hpp"
#include "tgfx2/tc_shader_bridge.hpp"
#include "tgfx2/tc_texture_upload.hpp"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <span>
#include <string>

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
    uint32_t result = 0;
    if (usage & TC_TEXTURE_USAGE_SAMPLED)
        result |= static_cast<uint32_t>(TextureUsage::Sampled);
    if (usage & TC_TEXTURE_USAGE_COLOR_ATTACHMENT)
        result |= static_cast<uint32_t>(TextureUsage::ColorAttachment);
    if (usage & TC_TEXTURE_USAGE_DEPTH_ATTACHMENT)
        result |= static_cast<uint32_t>(TextureUsage::DepthStencilAttachment);
    if (usage & TC_TEXTURE_USAGE_COPY_SRC)
        result |= static_cast<uint32_t>(TextureUsage::CopySrc);
    if (usage & TC_TEXTURE_USAGE_COPY_DST)
        result |= static_cast<uint32_t>(TextureUsage::CopyDst);
    return static_cast<TextureUsage>(result);
}

bool read_text_file(const std::string& path, std::string& result) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        tc::Log::error("WebGpuRenderDevice: missing shader layout sidecar '%s'", path.c_str());
        return false;
    }
    result.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
    if (result.empty()) {
        tc::Log::error("WebGpuRenderDevice: empty shader layout sidecar '%s'", path.c_str());
        return false;
    }
    return true;
}

bool load_webgpu_stage(
    const termin::ShaderArtifactResolver& resolver,
    tc_shader* shader,
    ShaderStage stage,
    ShaderDesc& desc)
{
    std::vector<uint8_t> artifact;
    if (!termin::tgfx2_load_or_compile_shader_artifact_for_backend(
            resolver, shader, BackendType::WebGPU, stage, artifact)) {
        return false;
    }
    desc.source.assign(reinterpret_cast<const char*>(artifact.data()), artifact.size());

    std::string artifact_path;
    if (!termin::tgfx2_shader_artifact_path(
            resolver, shader->uuid, BackendType::WebGPU, stage, artifact_path)) {
        return false;
    }
    return read_text_file(artifact_path + ".layout.json", desc.resource_layout_json);
}

} // namespace

bool WebGpuRenderDevice::ensure_tc_shader(
    tc_shader* shader,
    ShaderHandle* out_vs,
    ShaderHandle* out_fs)
{
    if (!shader || !out_fs) {
        tc::Log::error("WebGpuRenderDevice::ensure_tc_shader: invalid arguments");
        return false;
    }
    const bool has_vs = shader->vertex_source && shader->vertex_source[0] != '\0';
    const auto& resolver = shader_artifact_resolver();
    const uint64_t resolver_revision = resolver.revision();
    auto cached = tc_shader_cache_.find(shader->pool_index);
    if (cached != tc_shader_cache_.end() &&
        cached->second.version == shader->version &&
        cached->second.resolver_revision == resolver_revision &&
        cached->second.has_vs == has_vs && cached->second.fs &&
        (!has_vs || cached->second.vs)) {
        if (out_vs) *out_vs = cached->second.vs;
        *out_fs = cached->second.fs;
        return true;
    }
    if (cached != tc_shader_cache_.end()) {
        if (cached->second.vs) destroy(cached->second.vs);
        if (cached->second.fs) destroy(cached->second.fs);
        tc_shader_cache_.erase(cached);
    }

    ShaderHandle vs;
    try {
        if (has_vs) {
            ShaderDesc desc;
            desc.stage = ShaderStage::Vertex;
            desc.debug_name = std::string(shader->name ? shader->name : shader->uuid) + ":vertex";
            if (shader->vertex_entry && shader->vertex_entry[0]) desc.entry_point = shader->vertex_entry;
            if (!load_webgpu_stage(resolver, shader, desc.stage, desc)) return false;
            vs = create_shader(desc);
        }

        ShaderDesc desc;
        desc.stage = ShaderStage::Fragment;
        desc.debug_name = std::string(shader->name ? shader->name : shader->uuid) + ":fragment";
        if (shader->fragment_entry && shader->fragment_entry[0]) desc.entry_point = shader->fragment_entry;
        if (!load_webgpu_stage(resolver, shader, desc.stage, desc)) {
            if (vs) destroy(vs);
            return false;
        }
        ShaderHandle fs = create_shader(desc);
        tc_shader_cache_.emplace(
            shader->pool_index,
            CachedTcShaderEntry{vs, fs, shader->version, resolver_revision, has_vs});
        if (out_vs) *out_vs = vs;
        *out_fs = fs;
        return true;
    } catch (const std::exception& error) {
        if (vs) destroy(vs);
        tc::Log::error(
            "WebGpuRenderDevice::ensure_tc_shader: failed for '%s': %s",
            shader->name ? shader->name : shader->uuid,
            error.what());
        return false;
    }
}

void WebGpuRenderDevice::invalidate_tc_shader_cache(uint32_t pool_index) {
    auto entry = tc_shader_cache_.find(pool_index);
    if (entry == tc_shader_cache_.end()) return;
    if (entry->second.vs) destroy(entry->second.vs);
    if (entry->second.fs) destroy(entry->second.fs);
    tc_shader_cache_.erase(entry);
}

TextureHandle WebGpuRenderDevice::ensure_tc_texture(tc_texture* texture) {
    if (!texture) return {};
    if (!texture->header.is_loaded) tc_texture_ensure_loaded_ptr(texture);
    const bool gpu_first = texture->storage_kind == TC_TEXTURE_STORAGE_GPU_FIRST;
    if (texture->width == 0 || texture->height == 0 || (!gpu_first && !texture->data)) {
        tc::Log::error(
            "WebGpuRenderDevice::ensure_tc_texture: texture '%s' has no image data",
            texture->header.name ? texture->header.name : texture->header.uuid);
        return {};
    }
    auto cached = tc_texture_cache_.find(texture->header.pool_index);
    if (cached != tc_texture_cache_.end() && cached->second.version == texture->header.version) {
        return cached->second.handle;
    }
    if (cached != tc_texture_cache_.end()) {
        if (cached->second.handle) destroy(cached->second.handle);
        tc_texture_cache_.erase(cached);
    }

    TextureDesc desc;
    desc.width = texture->width;
    desc.height = texture->height;
    desc.sample_count = 1;
    if (gpu_first) {
        if (texture->mipmap) {
            tc::Log::error(
                "WebGpuRenderDevice::ensure_tc_texture: GPU-first texture '%s' requests unsupported mip generation",
                texture->header.name ? texture->header.name : texture->header.uuid);
            return {};
        }
        desc.format = pixel_format_for_tc_texture(
            static_cast<tc_texture_format>(texture->format),
            static_cast<tc_texture_encoding>(texture->encoding));
        if (desc.format == PixelFormat::Undefined) {
            tc::Log::error("WebGpuRenderDevice::ensure_tc_texture: unsupported GPU texture format");
            return {};
        }
        desc.usage = tc_usage_to_tgfx(texture->usage) | TextureUsage::CopyDst;
        if (desc.usage == TextureUsage::CopyDst) desc.usage = desc.usage | TextureUsage::Sampled;
    } else {
        TcTextureUpload upload;
        if (!prepare_tc_texture_upload(texture, upload)) return {};
        desc.format = upload.format;
        desc.mip_levels = static_cast<uint32_t>(upload.levels.size());
        desc.usage = TextureUsage::Sampled | TextureUsage::CopySrc | TextureUsage::CopyDst;
        TextureHandle handle = create_texture(desc);
        if (!handle) return {};
        for (uint32_t mip = 0; mip < upload.levels.size(); ++mip) {
            const auto& pixels = upload.levels[mip];
            upload_texture(handle, std::span<const uint8_t>(pixels.data(), pixels.size()), mip);
        }
        tc_texture_cache_.emplace(
            texture->header.pool_index,
            CachedTcTextureEntry{handle, texture->header.version});
        return handle;
    }

    TextureHandle handle = create_texture(desc);
    if (!handle) return {};
    tc_texture_cache_.emplace(
        texture->header.pool_index,
        CachedTcTextureEntry{handle, texture->header.version});
    return handle;
}

void WebGpuRenderDevice::invalidate_tc_texture_cache(uint32_t pool_index) {
    auto entry = tc_texture_cache_.find(pool_index);
    if (entry == tc_texture_cache_.end()) return;
    if (entry->second.handle) destroy(entry->second.handle);
    tc_texture_cache_.erase(entry);
}

std::pair<BufferHandle, BufferHandle> WebGpuRenderDevice::ensure_tc_mesh(tc_mesh* mesh) {
    if (!mesh || !mesh->vertices || mesh->vertex_count == 0 ||
        !mesh->indices || mesh->index_count == 0 || mesh->layout.stride == 0) {
        tc::Log::error("WebGpuRenderDevice::ensure_tc_mesh: mesh has no CPU geometry");
        return {};
    }
    auto cached = tc_mesh_cache_.find(mesh->header.pool_index);
    if (cached != tc_mesh_cache_.end() && cached->second.version == mesh->header.version) {
        return {cached->second.vbo, cached->second.ebo};
    }
    if (cached != tc_mesh_cache_.end()) {
        if (cached->second.vbo) destroy(cached->second.vbo);
        if (cached->second.ebo) destroy(cached->second.ebo);
        tc_mesh_cache_.erase(cached);
    }

    const size_t vertex_bytes = tc_mesh_vertices_size(mesh);
    const size_t index_bytes = tc_mesh_indices_size(mesh);
    BufferHandle vbo = create_buffer({vertex_bytes, BufferUsage::Vertex | BufferUsage::CopyDst});
    if (!vbo) return {};
    upload_buffer(vbo, std::span<const uint8_t>(
        static_cast<const uint8_t*>(mesh->vertices), vertex_bytes));
    BufferHandle ebo = create_buffer({index_bytes, BufferUsage::Index | BufferUsage::CopyDst});
    if (!ebo) {
        destroy(vbo);
        return {};
    }
    upload_buffer(ebo, std::span<const uint8_t>(
        reinterpret_cast<const uint8_t*>(mesh->indices), index_bytes));
    tc_mesh_cache_.emplace(
        mesh->header.pool_index,
        CachedTcMeshEntry{vbo, ebo, mesh->header.version});
    return {vbo, ebo};
}

void WebGpuRenderDevice::invalidate_tc_mesh_cache(uint32_t pool_index) {
    auto entry = tc_mesh_cache_.find(pool_index);
    if (entry == tc_mesh_cache_.end()) return;
    if (entry->second.vbo) destroy(entry->second.vbo);
    if (entry->second.ebo) destroy(entry->second.ebo);
    tc_mesh_cache_.erase(entry);
}

} // namespace tgfx
