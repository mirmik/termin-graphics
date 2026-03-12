// fbo_pool.cpp - FBO pool implementation
#include "termin/render/fbo_pool.hpp"

#include <tcbase/tc_log.hpp>

namespace termin {

FramebufferHandle* FBOPool::ensure(
    GraphicsBackend* graphics,
    const std::string& key,
    int width,
    int height,
    int samples,
    const std::string& format,
    TextureFilter filter
) {
    for (auto& entry : entries) {
        if (entry.key == key) {
            bool needs_recreate = (entry.samples != samples) ||
                                  (entry.format != format) ||
                                  (entry.filter != filter);

            if (needs_recreate && entry.fbo && !entry.external) {
                auto new_fbo = graphics->create_framebuffer(width, height, samples, format, filter);
                if (new_fbo) {
                    entry.fbo = std::move(new_fbo);
                    entry.width = width;
                    entry.height = height;
                    entry.samples = samples;
                    entry.format = format;
                    entry.filter = filter;
                }
                return entry.fbo.get();
            }

            if (entry.fbo && (entry.width != width || entry.height != height)) {
                entry.fbo->resize(width, height);
                entry.width = width;
                entry.height = height;
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
    entries.push_back(std::move(entry));

    return ptr;
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
