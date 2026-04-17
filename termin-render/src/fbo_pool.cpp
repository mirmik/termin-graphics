// fbo_pool.cpp - FBO pool implementation
#include "termin/render/fbo_pool.hpp"

#include "tgfx2/descriptors.hpp"
#include "tgfx2/i_render_device.hpp"
#include "tgfx2/opengl/opengl_render_device.hpp"

#include <tcbase/tc_log.hpp>

namespace termin {

namespace {

// Destroy the owned tgfx2 textures attached to an entry.
void release_tgfx2_wrappers(FBOPoolEntry& entry) {
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

} // anonymous namespace

bool FBOPool::ensure_native(
    tgfx::IRenderDevice& device,
    const std::string& key,
    int width,
    int height,
    tgfx::PixelFormat color_format,
    bool has_depth,
    tgfx::PixelFormat depth_format,
    int samples
) {
    auto alloc_textures = [&](FBOPoolEntry& entry) {
        tgfx::TextureDesc cdesc;
        cdesc.width = static_cast<uint32_t>(width);
        cdesc.height = static_cast<uint32_t>(height);
        cdesc.format = color_format;
        cdesc.sample_count = static_cast<uint32_t>(samples);
        cdesc.usage = tgfx::TextureUsage::Sampled |
                      tgfx::TextureUsage::ColorAttachment;
        entry.color_tgfx2 = device.create_texture(cdesc);
        if (has_depth) {
            tgfx::TextureDesc ddesc;
            ddesc.width = static_cast<uint32_t>(width);
            ddesc.height = static_cast<uint32_t>(height);
            ddesc.format = depth_format;
            ddesc.sample_count = static_cast<uint32_t>(samples);
            ddesc.usage = tgfx::TextureUsage::Sampled |
                          tgfx::TextureUsage::DepthStencilAttachment;
            entry.depth_tgfx2 = device.create_texture(ddesc);
        }
    };

    for (auto& entry : entries) {
        if (entry.key != key) continue;

        bool needs_recreate = entry.native_device != &device ||
                              entry.width != width ||
                              entry.height != height ||
                              entry.samples != samples ||
                              entry.color_format != color_format ||
                              entry.has_depth != has_depth ||
                              (has_depth && entry.depth_format != depth_format);
        if (!needs_recreate) {
            return true;
        }
        release_tgfx2_wrappers(entry);
        // The GL textures we just destroyed may have their gl_ids
        // immediately reused by create_texture below. Any FBO the
        // device cached keyed on the old gl_id would then point at
        // fresh attachments whose size/format may differ — silent
        // black-screen territory. Dump the whole cache here so
        // begin_pass rebuilds FBOs against the new textures.
        if (auto* gl_dev = dynamic_cast<tgfx::OpenGLRenderDevice*>(&device)) {
            gl_dev->invalidate_fbo_cache();
        }
        entry.native_device = &device;
        entry.width = width;
        entry.height = height;
        entry.samples = samples;
        entry.color_format = color_format;
        entry.depth_format = depth_format;
        entry.has_depth = has_depth;
        alloc_textures(entry);
        return true;
    }

    FBOPoolEntry entry;
    entry.key = key;
    entry.native_device = &device;
    entry.width = width;
    entry.height = height;
    entry.samples = samples;
    entry.color_format = color_format;
    entry.depth_format = depth_format;
    entry.has_depth = has_depth;
    alloc_textures(entry);
    entries.push_back(std::move(entry));
    return true;
}

tgfx::TextureHandle FBOPool::get_color_tgfx2(const std::string& key) const {
    for (const auto& entry : entries) {
        if (entry.key == key) return entry.color_tgfx2;
    }
    auto alias_it = alias_to_canonical.find(key);
    if (alias_it != alias_to_canonical.end()) {
        for (const auto& entry : entries) {
            if (entry.key == alias_it->second) return entry.color_tgfx2;
        }
    }
    return {};
}

tgfx::TextureHandle FBOPool::get_depth_tgfx2(const std::string& key) const {
    for (const auto& entry : entries) {
        if (entry.key == key) return entry.depth_tgfx2;
    }
    auto alias_it = alias_to_canonical.find(key);
    if (alias_it != alias_to_canonical.end()) {
        for (const auto& entry : entries) {
            if (entry.key == alias_it->second) return entry.depth_tgfx2;
        }
    }
    return {};
}

void FBOPool::add_alias(const std::string& alias, const std::string& canonical) {
    if (alias == canonical) {
        return;
    }
    alias_to_canonical[alias] = canonical;
}

void FBOPool::clear() {
    for (auto& entry : entries) {
        release_tgfx2_wrappers(entry);
    }
    entries.clear();
    alias_to_canonical.clear();
}

std::vector<std::string> FBOPool::keys() const {
    std::vector<std::string> result;
    result.reserve(entries.size() + alias_to_canonical.size());
    for (const auto& entry : entries) {
        result.push_back(entry.key);
    }
    for (const auto& pair : alias_to_canonical) {
        result.push_back(pair.first);
    }
    return result;
}

} // namespace termin
