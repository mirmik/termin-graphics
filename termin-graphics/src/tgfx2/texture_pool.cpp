#include "tgfx2/texture_pool.hpp"

#include "tgfx2/i_render_device.hpp"

namespace tgfx {

namespace {

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
                         int width,
                         int height,
                         PixelFormat format,
                         TextureUsage usage,
                         uint32_t sample_count) {
    for (auto& entry : entries) {
        if (entry.key != key) {
            continue;
        }

        const bool needs_recreate = entry.device != &device ||
                                    entry.width != width ||
                                    entry.height != height ||
                                    entry.sample_count != sample_count ||
                                    entry.format != format ||
                                    entry.usage != usage;
        if (!needs_recreate) {
            return true;
        }

        release_texture(entry);
        device.invalidate_render_target_cache();
        entry.device = &device;
        entry.width = width;
        entry.height = height;
        entry.sample_count = sample_count;
        entry.format = format;
        entry.usage = usage;

        TextureDesc desc;
        desc.width = static_cast<uint32_t>(width);
        desc.height = static_cast<uint32_t>(height);
        desc.sample_count = sample_count;
        desc.format = format;
        desc.usage = usage;
        entry.handle = device.create_texture(desc);
        return static_cast<bool>(entry.handle);
    }

    TexturePoolEntry entry;
    entry.key = std::string(key);
    entry.device = &device;
    entry.width = width;
    entry.height = height;
    entry.sample_count = sample_count;
    entry.format = format;
    entry.usage = usage;

    TextureDesc desc;
    desc.width = static_cast<uint32_t>(width);
    desc.height = static_cast<uint32_t>(height);
    desc.sample_count = sample_count;
    desc.format = format;
    desc.usage = usage;
    entry.handle = device.create_texture(desc);
    const bool ok = static_cast<bool>(entry.handle);
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
                              int width,
                              int height,
                              PixelFormat color_format,
                              bool has_depth,
                              PixelFormat depth_format,
                              int samples) {
    auto alloc_textures = [&](RenderTargetEntry& entry) {
        TextureDesc color_desc;
        color_desc.width = static_cast<uint32_t>(width);
        color_desc.height = static_cast<uint32_t>(height);
        color_desc.format = color_format;
        color_desc.sample_count = static_cast<uint32_t>(samples);
        color_desc.usage = TextureUsage::Sampled |
                           TextureUsage::ColorAttachment |
                           TextureUsage::CopySrc |
                           TextureUsage::CopyDst;
        entry.color_tgfx2 = device.create_texture(color_desc);

        if (has_depth) {
            TextureDesc depth_desc;
            depth_desc.width = static_cast<uint32_t>(width);
            depth_desc.height = static_cast<uint32_t>(height);
            depth_desc.format = depth_format;
            depth_desc.sample_count = static_cast<uint32_t>(samples);
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
                                    entry.width != width ||
                                    entry.height != height ||
                                    entry.samples != samples ||
                                    entry.color_format != color_format ||
                                    entry.has_depth != has_depth ||
                                    (has_depth && entry.depth_format != depth_format);
        if (!needs_recreate) {
            return true;
        }

        release_render_target(entry);
        device.invalidate_render_target_cache();
        entry.native_device = &device;
        entry.width = width;
        entry.height = height;
        entry.samples = samples;
        entry.color_format = color_format;
        entry.depth_format = depth_format;
        entry.has_depth = has_depth;
        alloc_textures(entry);
        return static_cast<bool>(entry.color_tgfx2) &&
               (!has_depth || static_cast<bool>(entry.depth_tgfx2));
    }

    RenderTargetEntry entry;
    entry.key = std::string(key);
    entry.native_device = &device;
    entry.width = width;
    entry.height = height;
    entry.samples = samples;
    entry.color_format = color_format;
    entry.depth_format = depth_format;
    entry.has_depth = has_depth;
    alloc_textures(entry);
    const bool ok = static_cast<bool>(entry.color_tgfx2) &&
                    (!has_depth || static_cast<bool>(entry.depth_tgfx2));
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
