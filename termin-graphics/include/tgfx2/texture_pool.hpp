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
    int width = 0;
    int height = 0;
    uint32_t sample_count = 1;
    PixelFormat format = PixelFormat::Undefined;
    TextureUsage usage{};
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
                int width,
                int height,
                PixelFormat format,
                TextureUsage usage,
                uint32_t sample_count = 1);
    TextureHandle get(std::string_view key) const;
    void clear();
};

struct TGFX2_TYPE_API RenderTargetEntry {
    std::string key;
    int width = 0;
    int height = 0;
    int samples = 1;
    IRenderDevice* native_device = nullptr;
    PixelFormat color_format = PixelFormat::RGBA8_UNorm;
    PixelFormat depth_format = PixelFormat::D24_UNorm;
    bool has_depth = false;
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
                int width,
                int height,
                PixelFormat color_format = PixelFormat::RGBA8_UNorm,
                bool has_depth = true,
                PixelFormat depth_format = PixelFormat::D24_UNorm,
                int samples = 1);
    TextureHandle color(std::string_view key) const;
    TextureHandle depth(std::string_view key) const;
    IRenderDevice* device() const;
    void clear();
};

} // namespace tgfx
