#include "tgfx2/texture_pool.hpp"

#include "tgfx2/i_render_device.hpp"

#include <tcbase/tc_log.h>

namespace tgfx {

namespace {

bool texture_desc_equal(const TextureDesc& a, const TextureDesc& b) {
    return a.width == b.width &&
           a.height == b.height &&
           a.mip_levels == b.mip_levels &&
           a.sample_count == b.sample_count &&
           a.format == b.format &&
           a.usage == b.usage;
}

bool render_target_desc_equal(const RenderTargetPoolDesc& a, const RenderTargetPoolDesc& b) {
    return a.width == b.width &&
           a.height == b.height &&
           a.samples == b.samples &&
           a.color_format == b.color_format &&
           a.has_depth == b.has_depth &&
           (!a.has_depth || a.depth_format == b.depth_format);
}

void release_texture(TexturePoolEntry& entry) {
    if (entry.device && entry.handle) {
        entry.device->destroy(entry.handle);
    }
    entry.handle = {};
    entry.device = nullptr;
}

void release_render_target(RenderTargetEntry& entry) {
    if (entry.native_device) {
        if (entry.color_tgfx2) {
            entry.native_device->destroy(entry.color_tgfx2);
        }
        if (entry.depth_tgfx2) {
            entry.native_device->destroy(entry.depth_tgfx2);
        }
    }
    entry.color_tgfx2 = {};
    entry.depth_tgfx2 = {};
    entry.native_device = nullptr;
}

} // namespace

TexturePool::~TexturePool() {
    clear();
}

bool TexturePool::ensure(IRenderDevice& device,
                         std::string_view key,
                         const TextureDesc& desc) {
    for (auto& entry : entries) {
        if (entry.key != key) {
            continue;
        }

        const bool needs_recreate = entry.device != &device ||
                                    !texture_desc_equal(entry.desc, desc) ||
                                    !entry.handle;
        if (!needs_recreate) {
            return true;
        }

        release_texture(entry);
        device.invalidate_render_target_cache();
        entry.device = &device;
        entry.desc = desc;
        entry.handle = device.create_texture(desc);
        if (!entry.handle) {
            tc_log(TC_LOG_ERROR,
                   "TexturePool: failed to create texture '%s'; request will be retried",
                   entry.key.c_str());
            return false;
        }
        return true;
    }

    TexturePoolEntry entry;
    entry.key = std::string(key);
    entry.device = &device;
    entry.desc = desc;
    entry.handle = device.create_texture(desc);
    const bool ok = static_cast<bool>(entry.handle);
    if (!ok) {
        tc_log(TC_LOG_ERROR,
               "TexturePool: failed to create texture '%s'; request will be retried",
               entry.key.c_str());
    }
    entries.push_back(std::move(entry));
    return ok;
}

TextureHandle TexturePool::get(std::string_view key) const {
    for (const auto& entry : entries) {
        if (entry.key == key) {
            return entry.handle;
        }
    }
    return {};
}

void TexturePool::clear() {
    for (auto& entry : entries) {
        release_texture(entry);
    }
    entries.clear();
}

RenderTargetPool::~RenderTargetPool() {
    clear();
}

bool RenderTargetPool::ensure(IRenderDevice& device,
                              std::string_view key,
                              const RenderTargetPoolDesc& desc) {
    auto alloc_textures = [&](RenderTargetEntry& entry) {
        TextureDesc color_desc;
        color_desc.width = static_cast<uint32_t>(desc.width);
        color_desc.height = static_cast<uint32_t>(desc.height);
        color_desc.format = desc.color_format;
        color_desc.sample_count = static_cast<uint32_t>(desc.samples);
        color_desc.usage = TextureUsage::Sampled |
                           TextureUsage::ColorAttachment |
                           TextureUsage::CopySrc |
                           TextureUsage::CopyDst;
        entry.color_tgfx2 = device.create_texture(color_desc);

        if (desc.has_depth) {
            TextureDesc depth_desc;
            depth_desc.width = static_cast<uint32_t>(desc.width);
            depth_desc.height = static_cast<uint32_t>(desc.height);
            depth_desc.format = desc.depth_format;
            depth_desc.sample_count = static_cast<uint32_t>(desc.samples);
            depth_desc.usage = TextureUsage::Sampled |
                               TextureUsage::DepthStencilAttachment |
                               TextureUsage::CopySrc |
                               TextureUsage::CopyDst;
            entry.depth_tgfx2 = device.create_texture(depth_desc);
        }
    };

    for (auto& entry : entries) {
        if (entry.key != key) {
            continue;
        }

        const bool needs_recreate = entry.native_device != &device ||
                                    !render_target_desc_equal(entry.desc, desc) ||
                                    !entry.color_tgfx2 ||
                                    (desc.has_depth && !entry.depth_tgfx2);
        if (!needs_recreate) {
            return true;
        }

        release_render_target(entry);
        device.invalidate_render_target_cache();
        entry.native_device = &device;
        entry.desc = desc;
        alloc_textures(entry);
        const bool ok = static_cast<bool>(entry.color_tgfx2) &&
                        (!desc.has_depth || static_cast<bool>(entry.depth_tgfx2));
        if (!ok) {
            tc_log(TC_LOG_ERROR,
                   "RenderTargetPool: failed to create target '%s'; request will be retried",
                   entry.key.c_str());
        }
        return ok;
    }

    RenderTargetEntry entry;
    entry.key = std::string(key);
    entry.native_device = &device;
    entry.desc = desc;
    alloc_textures(entry);
    const bool ok = static_cast<bool>(entry.color_tgfx2) &&
                    (!desc.has_depth || static_cast<bool>(entry.depth_tgfx2));
    if (!ok) {
        tc_log(TC_LOG_ERROR,
               "RenderTargetPool: failed to create target '%s'; request will be retried",
               entry.key.c_str());
    }
    entries.push_back(std::move(entry));
    return ok;
}

TextureHandle RenderTargetPool::color(std::string_view key) const {
    for (const auto& entry : entries) {
        if (entry.key == key) {
            return entry.color_tgfx2;
        }
    }
    return {};
}

TextureHandle RenderTargetPool::depth(std::string_view key) const {
    for (const auto& entry : entries) {
        if (entry.key == key) {
            return entry.depth_tgfx2;
        }
    }
    return {};
}

IRenderDevice* RenderTargetPool::device() const {
    return entries.empty() ? nullptr : entries.front().native_device;
}

void RenderTargetPool::clear() {
    for (auto& entry : entries) {
        release_render_target(entry);
    }
    entries.clear();
}

} // namespace tgfx
