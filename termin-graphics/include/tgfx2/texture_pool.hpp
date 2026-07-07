// texture_pool.hpp - Keyed tgfx2 texture/render-target caches.
#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "tgfx2/descriptors.hpp"
#include "tgfx2/handles.hpp"
#include "tgfx2/tgfx2_api.h"

namespace tgfx {

class IRenderDevice;

struct TGFX2_TYPE_API TexturePoolEntry {
    std::string key;
    TextureDesc desc;
    IRenderDevice* device = nullptr;
    TextureHandle handle;

    TexturePoolEntry() = default;
    TexturePoolEntry(TexturePoolEntry&&) = default;
    TexturePoolEntry& operator=(TexturePoolEntry&&) = default;
    TexturePoolEntry(const TexturePoolEntry&) = delete;
    TexturePoolEntry& operator=(const TexturePoolEntry&) = delete;
};

class TGFX2_TYPE_API TexturePool {
public:
    std::vector<TexturePoolEntry> entries;

    TexturePool() = default;
    TexturePool(TexturePool&&) = default;
    TexturePool& operator=(TexturePool&&) = default;
    TexturePool(const TexturePool&) = delete;
    TexturePool& operator=(const TexturePool&) = delete;
    ~TexturePool();

    bool ensure(IRenderDevice& device,
                std::string_view key,
                const TextureDesc& desc);
    TextureHandle get(std::string_view key) const;
    void clear();
};

struct TGFX2_TYPE_API RenderTargetPoolDesc {
    int width = 0;
    int height = 0;
    int samples = 1;
    PixelFormat color_format = PixelFormat::RGBA8_UNorm;
    bool has_depth = true;
    PixelFormat depth_format = PixelFormat::D24_UNorm;
};

struct TGFX2_TYPE_API RenderTargetEntry {
    std::string key;
    IRenderDevice* native_device = nullptr;
    RenderTargetPoolDesc desc;
    TextureHandle color_tgfx2;
    TextureHandle depth_tgfx2;

    RenderTargetEntry() = default;
    RenderTargetEntry(RenderTargetEntry&&) = default;
    RenderTargetEntry& operator=(RenderTargetEntry&&) = default;
    RenderTargetEntry(const RenderTargetEntry&) = delete;
    RenderTargetEntry& operator=(const RenderTargetEntry&) = delete;
};

class TGFX2_TYPE_API RenderTargetPool {
public:
    std::vector<RenderTargetEntry> entries;

    RenderTargetPool() = default;
    RenderTargetPool(RenderTargetPool&&) = default;
    RenderTargetPool& operator=(RenderTargetPool&&) = default;
    RenderTargetPool(const RenderTargetPool&) = delete;
    RenderTargetPool& operator=(const RenderTargetPool&) = delete;
    ~RenderTargetPool();

    bool ensure(IRenderDevice& device,
                std::string_view key,
                const RenderTargetPoolDesc& desc);
    TextureHandle color(std::string_view key) const;
    TextureHandle depth(std::string_view key) const;
    IRenderDevice* device() const;
    void clear();
};

} // namespace tgfx
