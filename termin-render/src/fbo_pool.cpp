// fbo_pool.cpp - Framegraph alias wrapper over tgfx::RenderTargetPool.
#include "termin/render/fbo_pool.hpp"

namespace termin {

bool FBOPool::ensure_native(
    tgfx::IRenderDevice& device,
    const std::string& key,
    const tgfx::RenderTargetPoolDesc& desc
) {
    return ensure(device, key, desc);
}

tgfx::TextureHandle FBOPool::get_color_tgfx2(const std::string& key) const {
    tgfx::TextureHandle direct = color(key);
    if (direct) {
        return direct;
    }
    auto alias_it = alias_to_canonical.find(key);
    if (alias_it != alias_to_canonical.end()) {
        return color(alias_it->second);
    }
    return {};
}

tgfx::TextureHandle FBOPool::get_depth_tgfx2(const std::string& key) const {
    tgfx::TextureHandle direct = depth(key);
    if (direct) {
        return direct;
    }
    auto alias_it = alias_to_canonical.find(key);
    if (alias_it != alias_to_canonical.end()) {
        return depth(alias_it->second);
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
    tgfx::RenderTargetPool::clear();
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
