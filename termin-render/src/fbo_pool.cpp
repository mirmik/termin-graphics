// fbo_pool.cpp - FBO pool implementation
#include "termin/render/fbo_pool.hpp"
#include "termin/render/tgfx2_bridge.hpp"

#include "tgfx2/opengl/opengl_render_device.hpp"

#include <tcbase/tc_log.hpp>

namespace termin {

namespace {

// Destroy any tgfx2 wrappers attached to an entry. Called whenever
// the underlying FBO is replaced (ensure recreate, resize), set()
// clears the owned FBO, or the entry is removed from the pool.
void release_tgfx2_wrappers(FBOPoolEntry& entry) {
    if (!entry.tgfx2_device) return;
    if (entry.color_tgfx2) {
        entry.tgfx2_device->destroy(entry.color_tgfx2);
        entry.color_tgfx2 = {};
    }
    if (entry.depth_tgfx2) {
        entry.tgfx2_device->destroy(entry.depth_tgfx2);
        entry.depth_tgfx2 = {};
    }
    entry.tgfx2_device = nullptr;
}

// (Re)create persistent tgfx2 wrappers for the entry's FBO color +
// depth attachments. Idempotent — safe to call on a fresh entry or
// on one whose wrappers were already released.
void wrap_entry_for_tgfx2(FBOPoolEntry& entry,
                          tgfx2::OpenGLRenderDevice* tgfx2_device) {
    release_tgfx2_wrappers(entry);
    if (!tgfx2_device || !entry.fbo) return;

    entry.tgfx2_device = tgfx2_device;
    entry.color_tgfx2 = wrap_fbo_color_as_tgfx2(*tgfx2_device, entry.fbo.get());
    // depth_tgfx2 will remain {} for FBOs whose depth is a renderbuffer
    // rather than a texture (common for color FBOs). That's fine —
    // callers read it as "no depth texture available".
    entry.depth_tgfx2 = wrap_fbo_depth_as_tgfx2(*tgfx2_device, entry.fbo.get());
}

} // anonymous namespace

FramebufferHandle* FBOPool::ensure(
    GraphicsBackend* graphics,
    const std::string& key,
    int width,
    int height,
    int samples,
    const std::string& format,
    TextureFilter filter,
    tgfx2::OpenGLRenderDevice* tgfx2_device
) {
    for (auto& entry : entries) {
        if (entry.key == key) {
            bool needs_recreate = (entry.samples != samples) ||
                                  (entry.format != format) ||
                                  (entry.filter != filter);

            if (needs_recreate && entry.fbo && !entry.external) {
                auto new_fbo = graphics->create_framebuffer(width, height, samples, format, filter);
                if (new_fbo) {
                    release_tgfx2_wrappers(entry);
                    entry.fbo = std::move(new_fbo);
                    entry.width = width;
                    entry.height = height;
                    entry.samples = samples;
                    entry.format = format;
                    entry.filter = filter;
                    wrap_entry_for_tgfx2(entry, tgfx2_device);
                    // The old GL FBO / textures are deleted. Any ctx2
                    // fbo_cache entries keyed on the old gl_ids are now
                    // stale — the driver may recycle those ids for new
                    // textures. Invalidate to force begin_pass to
                    // rebuild fresh FBOs from current attachments.
                    if (tgfx2_device) tgfx2_device->invalidate_fbo_cache();
                }
                return entry.fbo.get();
            }

            if (entry.fbo && (entry.width != width || entry.height != height)) {
                release_tgfx2_wrappers(entry);
                entry.fbo->resize(width, height);
                entry.width = width;
                entry.height = height;
                wrap_entry_for_tgfx2(entry, tgfx2_device);
                // Same rationale as the recreate branch above — resize
                // deletes the backing GL textures.
                if (tgfx2_device) tgfx2_device->invalidate_fbo_cache();
            } else if (tgfx2_device && !entry.tgfx2_device) {
                // Entry was allocated without a tgfx2 device; attach
                // now so subsequent frames can pull pre-cached handles.
                wrap_entry_for_tgfx2(entry, tgfx2_device);
            }
            return entry.fbo.get();
        }
    }

    if (!graphics) {
        tc::Log::error("FBOPool::ensure: graphics is null");
        return nullptr;
    }

    auto fbo = graphics->create_framebuffer(width, height, samples, format, filter);
    if (!fbo) {
        tc::Log::error("FBOPool::ensure: failed to create framebuffer '%s'", key.c_str());
        return nullptr;
    }

    FramebufferHandle* ptr = fbo.get();

    FBOPoolEntry entry;
    entry.key = key;
    entry.fbo = std::move(fbo);
    entry.width = width;
    entry.height = height;
    entry.samples = samples;
    entry.format = format;
    entry.filter = filter;
    entry.external = false;
    wrap_entry_for_tgfx2(entry, tgfx2_device);
    entries.push_back(std::move(entry));

    return ptr;
}

tgfx2::TextureHandle FBOPool::get_color_tgfx2(const std::string& key) const {
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

tgfx2::TextureHandle FBOPool::get_depth_tgfx2(const std::string& key) const {
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

FramebufferHandle* FBOPool::get(const std::string& key) {
    for (auto& entry : entries) {
        if (entry.key == key) {
            return entry.fbo.get();
        }
    }

    auto alias_it = alias_to_canonical.find(key);
    if (alias_it != alias_to_canonical.end()) {
        for (auto& entry : entries) {
            if (entry.key == alias_it->second) {
                return entry.fbo.get();
            }
        }
    }
    return nullptr;
}

void FBOPool::set(const std::string& key, FramebufferHandle* fbo) {
    (void) fbo;

    for (auto& entry : entries) {
        if (entry.key == key) {
            release_tgfx2_wrappers(entry);
            entry.fbo.reset();
            entry.external = true;
            return;
        }
    }

    FBOPoolEntry entry;
    entry.key = key;
    entry.fbo.reset();
    entry.external = true;
    entries.push_back(std::move(entry));
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
