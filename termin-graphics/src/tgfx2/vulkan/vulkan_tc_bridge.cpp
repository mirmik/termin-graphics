#ifdef TGFX2_HAS_VULKAN

#include "tgfx2/vulkan/vulkan_render_device.hpp"
#include "tgfx2/tc_shader_bridge.hpp"

#include <cstring>
#include <span>
#include <string>
#include <vector>

extern "C" {
#include <tcbase/tc_log.h>
#include <tgfx/resources/tc_texture.h>
#include <tgfx/resources/tc_texture_registry.h>
#include <tgfx/resources/tc_mesh.h>
#include <tgfx/resources/tc_shader.h>
}

namespace tgfx {

// --- tc_texture / tc_mesh per-device caches -----------------------------
//
// These replace the former file-scope g_tex_cache / g_mesh_cache singletons
// in tgfx2_bridge.cpp. Bridge functions now delegate directly to
// IRenderDevice on every backend.

namespace {

PixelFormat tc_format_to_tgfx2(tc_texture_format fmt) {
    switch (fmt) {
        case TC_TEXTURE_RGBA8:   return PixelFormat::RGBA8_UNorm;
        case TC_TEXTURE_RGB8:    return PixelFormat::RGB8_UNorm;
        case TC_TEXTURE_RG8:     return PixelFormat::RG8_UNorm;
        case TC_TEXTURE_R8:      return PixelFormat::R8_UNorm;
        case TC_TEXTURE_RGBA16F: return PixelFormat::RGBA16F;
        case TC_TEXTURE_RGB16F:  return PixelFormat::RGBA16F;
        case TC_TEXTURE_R16F:    return PixelFormat::R16F;
        case TC_TEXTURE_R32F:    return PixelFormat::R32F;
        case TC_TEXTURE_DEPTH24: return PixelFormat::D24_UNorm_S8_UInt;
        case TC_TEXTURE_DEPTH32F: return PixelFormat::D32F;
    }
    return PixelFormat::RGBA8_UNorm;
}

// Normalise tc_texture pixel data so Vulkan can upload it. RGB8 isn't a
// universal VkFormat, so expand it to RGBA8 (alpha = 255) here. Everything
// else is passed through.
std::vector<uint8_t> normalize_pixels(const tc_texture* tex, PixelFormat& out_fmt) {
    const auto fmt = static_cast<tc_texture_format>(tex->format);
    const size_t src_bytes = tc_texture_data_size(tex);
    const auto* src = static_cast<const uint8_t*>(tex->data);

    std::vector<uint8_t> pixels;
    if (fmt == TC_TEXTURE_RGB8) {
        out_fmt = PixelFormat::RGBA8_UNorm;
        pixels.resize(size_t(tex->width) * tex->height * 4);
        for (size_t i = 0, j = 0; i < src_bytes; i += 3, j += 4) {
            pixels[j + 0] = src[i + 0];
            pixels[j + 1] = src[i + 1];
            pixels[j + 2] = src[i + 2];
            pixels[j + 3] = 0xFF;
        }
        return pixels;
    }

    out_fmt = tc_format_to_tgfx2(fmt);
    pixels.assign(src, src + src_bytes);
    return pixels;
}

// Translate tc_texture_usage_flags bitset → tgfx::TextureUsage.
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

} // anonymous namespace

TextureHandle VulkanRenderDevice::ensure_tc_texture(tc_texture* tex) {
    if (!tex) return {};

    if (!tex->header.is_loaded && tex->header.load_callback) {
        tc_texture_ensure_loaded_ptr(tex);
    }

    const bool gpu_first = (tex->storage_kind == TC_TEXTURE_STORAGE_GPU_FIRST);

    if (tex->width == 0 || tex->height == 0) {
        tc_log(TC_LOG_ERROR,
               "VulkanRenderDevice::ensure_tc_texture: tc_texture '%s' has zero size",
               tex->header.name ? tex->header.name : tex->header.uuid);
        return {};
    }
    if (!gpu_first && !tex->data) {
        tc_log(TC_LOG_ERROR,
               "VulkanRenderDevice::ensure_tc_texture: tc_texture '%s' has no CPU pixels",
               tex->header.name ? tex->header.name : tex->header.uuid);
        return {};
    }

    const uint32_t pool_index = tex->header.pool_index;
    const uint32_t version = tex->header.version;

    std::lock_guard<std::mutex> lock(tc_texture_cache_mtx_);
    auto it = tc_texture_cache_.find(pool_index);
    if (it != tc_texture_cache_.end() && it->second.version == version) {
        return it->second.handle;
    }

    // Stale entry — destroy old handle before replacing it.
    if (it != tc_texture_cache_.end()) {
        if (it->second.handle) destroy(it->second.handle);
        tc_texture_cache_.erase(it);
    }

    TextureDesc desc;
    desc.width = tex->width;
    desc.height = tex->height;
    desc.mip_levels = 1;
    desc.sample_count = 1;

    if (gpu_first) {
        // Render-target-style: no CPU upload, just allocate a blank
        // VkImage with whatever attachment bits the caller declared.
        // CopyDst is always added because the staging upload path uses
        // it and so do `blit_to_texture` / `clear_texture`.
        desc.format = tc_format_to_tgfx2(static_cast<tc_texture_format>(tex->format));
        desc.usage = tc_usage_to_tgfx(tex->usage) | TextureUsage::CopyDst;

        TextureHandle handle = create_texture(desc);
        if (!handle) {
            tc_log(TC_LOG_ERROR,
                   "VulkanRenderDevice::ensure_tc_texture: GPU-only create_texture failed for '%s'",
                   tex->header.name ? tex->header.name : tex->header.uuid);
            return {};
        }

        CachedTcTextureEntry entry;
        entry.handle = handle;
        entry.version = version;
        tc_texture_cache_.emplace(pool_index, entry);
        return handle;
    }

    PixelFormat fmt = PixelFormat::RGBA8_UNorm;
    std::vector<uint8_t> pixels = normalize_pixels(tex, fmt);
    if (pixels.empty()) return {};

    desc.format = fmt;
    // CPU-first file textures are normally sampled by materials, but graph
    // pipelines may also route them through PresentToScreenPass/copy_texture
    // as a blit source. CopyDst is needed for staging upload; CopySrc is needed
    // for those texture-to-texture copies.
    desc.usage = TextureUsage::Sampled | TextureUsage::CopySrc | TextureUsage::CopyDst;

    TextureHandle handle = create_texture(desc);
    if (!handle) {
        tc_log(TC_LOG_ERROR,
               "VulkanRenderDevice::ensure_tc_texture: create_texture failed for '%s'",
               tex->header.name ? tex->header.name : tex->header.uuid);
        return {};
    }
    upload_texture(handle, std::span<const uint8_t>(pixels.data(), pixels.size()));

    CachedTcTextureEntry entry;
    entry.handle = handle;
    entry.version = version;
    tc_texture_cache_.emplace(pool_index, entry);
    return handle;
}

void VulkanRenderDevice::invalidate_tc_texture_cache(uint32_t pool_index) {
    std::lock_guard<std::mutex> lock(tc_texture_cache_mtx_);
    auto it = tc_texture_cache_.find(pool_index);
    if (it == tc_texture_cache_.end()) return;
    if (it->second.handle) destroy(it->second.handle);
    tc_texture_cache_.erase(it);
}

std::pair<BufferHandle, BufferHandle> VulkanRenderDevice::ensure_tc_mesh(tc_mesh* mesh) {
    if (!mesh) return {};

    if (!mesh->vertices || mesh->vertex_count == 0 ||
        !mesh->indices  || mesh->index_count == 0)
    {
        tc_log(TC_LOG_ERROR,
               "VulkanRenderDevice::ensure_tc_mesh: tc_mesh '%s' has no CPU data",
               mesh->header.name ? mesh->header.name : mesh->header.uuid);
        return {};
    }

    const uint32_t pool_index = mesh->header.pool_index;
    const uint32_t version = mesh->header.version;

    std::lock_guard<std::mutex> lock(tc_mesh_cache_mtx_);
    auto it = tc_mesh_cache_.find(pool_index);
    if (it != tc_mesh_cache_.end() && it->second.version == version) {
        return {it->second.vbo, it->second.ebo};
    }

    if (it != tc_mesh_cache_.end()) {
        if (it->second.vbo) destroy(it->second.vbo);
        if (it->second.ebo) destroy(it->second.ebo);
        tc_mesh_cache_.erase(it);
    }

    const size_t vb_size = mesh->vertex_count *
                           static_cast<size_t>(mesh->layout.stride);
    const size_t ib_size = mesh->index_count * sizeof(uint32_t);

    BufferDesc vb_desc;
    vb_desc.size  = vb_size;
    vb_desc.usage = BufferUsage::Vertex | BufferUsage::CopyDst;
    BufferHandle vbo = create_buffer(vb_desc);
    upload_buffer(
        vbo,
        std::span<const uint8_t>(
            static_cast<const uint8_t*>(mesh->vertices), vb_size));

    BufferDesc ib_desc;
    ib_desc.size  = ib_size;
    ib_desc.usage = BufferUsage::Index | BufferUsage::CopyDst;
    BufferHandle ebo = create_buffer(ib_desc);
    upload_buffer(
        ebo,
        std::span<const uint8_t>(
            reinterpret_cast<const uint8_t*>(mesh->indices), ib_size));

    CachedTcMeshEntry entry;
    entry.vbo = vbo;
    entry.ebo = ebo;
    entry.version = version;
    tc_mesh_cache_.emplace(pool_index, entry);
    return {vbo, ebo};
}

void VulkanRenderDevice::invalidate_tc_mesh_cache(uint32_t pool_index) {
    std::lock_guard<std::mutex> lock(tc_mesh_cache_mtx_);
    auto it = tc_mesh_cache_.find(pool_index);
    if (it == tc_mesh_cache_.end()) return;
    if (it->second.vbo) destroy(it->second.vbo);
    if (it->second.ebo) destroy(it->second.ebo);
    tc_mesh_cache_.erase(it);
}

bool VulkanRenderDevice::ensure_tc_shader(
    tc_shader* shader,
    ShaderHandle* out_vs,
    ShaderHandle* out_fs)
{
    if (!shader) {
        tc_log(TC_LOG_ERROR, "VulkanRenderDevice::ensure_tc_shader: shader is NULL");
        return false;
    }
    if (!out_fs) {
        tc_log(TC_LOG_ERROR, "VulkanRenderDevice::ensure_tc_shader: out_fs is NULL");
        return false;
    }
    if (!shader->fragment_source) {
        tc_log(TC_LOG_ERROR,
               "VulkanRenderDevice::ensure_tc_shader: missing fragment_source for '%s'",
               shader->name ? shader->name : shader->uuid);
        return false;
    }

    const bool has_vs = shader->vertex_source && shader->vertex_source[0] != '\0';
    const bool artifacts_required = tc_shader_requires_artifacts(shader);
    const auto shader_language = static_cast<tc_shader_language>(shader->language);
    const uint32_t pool_index = shader->pool_index;
    const uint32_t version = shader->version;
    {
        std::lock_guard<std::mutex> lock(tc_shader_cache_mtx_);
        auto it = tc_shader_cache_.find(pool_index);
        if (it != tc_shader_cache_.end() &&
            it->second.version == version &&
            it->second.has_vs == has_vs &&
            it->second.fs &&
            (!has_vs || it->second.vs))
        {
            if (out_vs) *out_vs = it->second.vs;
            *out_fs = it->second.fs;
            return true;
        }
        if (it != tc_shader_cache_.end()) {
            if (it->second.vs) destroy(it->second.vs);
            if (it->second.fs) destroy(it->second.fs);
            tc_shader_cache_.erase(it);
        }
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
                shader,
                BackendType::Vulkan,
                vs_desc.stage,
                vs_desc.bytecode)) {
            if (artifacts_required || shader_language != TC_SHADER_LANGUAGE_GLSL) {
                tc_log(TC_LOG_ERROR,
                       "VulkanRenderDevice::ensure_tc_shader: %s vertex artifact missing or dev compile failed for '%s' language=%u",
                       artifacts_required ? "required" : "non-GLSL",
                       shader->name ? shader->name : shader->uuid,
                       static_cast<unsigned>(shader->language));
                return false;
            }
            vs_desc.source = shader->vertex_source;
        }
        vs = create_shader(vs_desc);
        if (!vs) {
            tc_log(TC_LOG_ERROR,
                   "VulkanRenderDevice::ensure_tc_shader: VS compile failed for '%s'",
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
            shader,
            BackendType::Vulkan,
            fs_desc.stage,
            fs_desc.bytecode)) {
        if (artifacts_required || shader_language != TC_SHADER_LANGUAGE_GLSL) {
            if (vs) destroy(vs);
            tc_log(TC_LOG_ERROR,
                   "VulkanRenderDevice::ensure_tc_shader: %s fragment artifact missing or dev compile failed for '%s' language=%u",
                   artifacts_required ? "required" : "non-GLSL",
                   shader->name ? shader->name : shader->uuid,
                   static_cast<unsigned>(shader->language));
            return false;
        }
        fs_desc.source = shader->fragment_source;
    }
    ShaderHandle fs = create_shader(fs_desc);
    if (!fs) {
        if (vs) destroy(vs);
        tc_log(TC_LOG_ERROR,
               "VulkanRenderDevice::ensure_tc_shader: FS compile failed for '%s'",
               shader->name ? shader->name : shader->uuid);
        return false;
    }

    std::lock_guard<std::mutex> lock(tc_shader_cache_mtx_);
    auto it = tc_shader_cache_.find(pool_index);
    if (it != tc_shader_cache_.end()) {
        if (it->second.vs) destroy(it->second.vs);
        if (it->second.fs) destroy(it->second.fs);
        tc_shader_cache_.erase(it);
    }
    CachedTcShaderEntry entry;
    entry.vs = vs;
    entry.fs = fs;
    entry.version = version;
    entry.has_vs = has_vs;
    tc_shader_cache_.emplace(pool_index, entry);

    if (out_vs) *out_vs = vs;
    *out_fs = fs;
    return true;
}

void VulkanRenderDevice::invalidate_tc_shader_cache(uint32_t pool_index) {
    std::lock_guard<std::mutex> lock(tc_shader_cache_mtx_);
    auto it = tc_shader_cache_.find(pool_index);
    if (it == tc_shader_cache_.end()) return;
    if (it->second.vs) destroy(it->second.vs);
    if (it->second.fs) destroy(it->second.fs);
    tc_shader_cache_.erase(it);
}


} // namespace tgfx

#endif // TGFX2_HAS_VULKAN
