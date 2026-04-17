// fbo_pool.hpp - FBO pool for managing framebuffer allocation
#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "tgfx2/enums.hpp"
#include "tgfx2/handles.hpp"
#include "termin/render/render_export.hpp"

namespace tgfx2 {
class IRenderDevice;
}

namespace termin {

// FBO pool entry — owns a pair of tgfx2 textures (color + optional
// depth) that `RenderContext2::begin_pass` can attach into a cached
// GL FBO.
struct FBOPoolEntry {
public:
    std::string key;
    int width = 0;
    int height = 0;
    int samples = 1;
    tgfx2::IRenderDevice* native_device = nullptr;
    tgfx2::PixelFormat color_format = tgfx2::PixelFormat::RGBA8_UNorm;
    tgfx2::PixelFormat depth_format = tgfx2::PixelFormat::D24_UNorm;
    bool has_depth = false;
    tgfx2::TextureHandle color_tgfx2;
    tgfx2::TextureHandle depth_tgfx2;

public:
    FBOPoolEntry() = default;
    FBOPoolEntry(FBOPoolEntry&&) = default;
    FBOPoolEntry& operator=(FBOPoolEntry&&) = default;
    FBOPoolEntry(const FBOPoolEntry&) = delete;
    FBOPoolEntry& operator=(const FBOPoolEntry&) = delete;
};

class RENDER_API FBOPool {
public:
    std::vector<FBOPoolEntry> entries;
    std::unordered_map<std::string, std::string> alias_to_canonical;

public:
    FBOPool() = default;
    FBOPool(FBOPool&&) = default;
    FBOPool& operator=(FBOPool&&) = default;
    FBOPool(const FBOPool&) = delete;
    FBOPool& operator=(const FBOPool&) = delete;
    ~FBOPool() { clear(); }

    // Allocate a pair of owned `tgfx2::TextureHandle`s (color + optional
    // depth) via the render device. Callers use the returned handles
    // with `RenderContext2::begin_pass`, which assembles a cached GL FBO
    // inside the device on demand.
    //
    // On resize/format change the old handles are destroyed and new
    // ones are allocated; the device's internal FBO cache is also
    // invalidated because the driver may recycle gl_ids.
    bool ensure_native(
        tgfx2::IRenderDevice& device,
        const std::string& key,
        int width,
        int height,
        tgfx2::PixelFormat color_format = tgfx2::PixelFormat::RGBA8_UNorm,
        bool has_depth = true,
        tgfx2::PixelFormat depth_format = tgfx2::PixelFormat::D24_UNorm,
        int samples = 1
    );

    // Persistent tgfx2 texture handles for this entry's color / depth
    // attachment. Alias-resolving.
    tgfx2::TextureHandle get_color_tgfx2(const std::string& key) const;
    tgfx2::TextureHandle get_depth_tgfx2(const std::string& key) const;

    // Device that owns the entries' native textures (null if the
    // pool is empty). All entries must share the same device during
    // normal pipeline execution.
    tgfx2::IRenderDevice* device() const {
        return entries.empty() ? nullptr : entries.front().native_device;
    }

    void add_alias(const std::string& alias, const std::string& canonical);
    void clear();

    std::vector<std::string> keys() const;
};

} // namespace termin
