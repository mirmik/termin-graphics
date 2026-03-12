// fbo_pool.hpp - FBO pool for managing framebuffer allocation
#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "tgfx/graphics_backend.hpp"
#include "termin/render/resource_spec.hpp"

namespace termin {

struct FBOPoolEntry {
public:
    std::string key;
    FramebufferHandlePtr fbo;
    int width = 0;
    int height = 0;
    int samples = 1;
    std::string format;
    TextureFilter filter = TextureFilter::LINEAR;
    bool external = false;

public:
    FBOPoolEntry() = default;
    FBOPoolEntry(FBOPoolEntry&&) = default;
    FBOPoolEntry& operator=(FBOPoolEntry&&) = default;
    FBOPoolEntry(const FBOPoolEntry&) = delete;
    FBOPoolEntry& operator=(const FBOPoolEntry&) = delete;
};

class FBOPool {
public:
    std::vector<FBOPoolEntry> entries;
    std::unordered_map<std::string, std::string> alias_to_canonical;

public:
    FramebufferHandle* ensure(
        GraphicsBackend* graphics,
        const std::string& key,
        int width,
        int height,
        int samples = 1,
        const std::string& format = "",
        TextureFilter filter = TextureFilter::LINEAR
    );

    FramebufferHandle* get(const std::string& key);
    void set(const std::string& key, FramebufferHandle* fbo);
    void add_alias(const std::string& alias, const std::string& canonical);
    void clear();

    std::vector<std::string> keys() const;
};

} // namespace termin
