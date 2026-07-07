// fbo_pool.hpp - FBO pool for managing framebuffer allocation
#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "tgfx2/enums.hpp"
#include "tgfx2/handles.hpp"
#include "tgfx2/texture_pool.hpp"
#include "termin/render/render_export.hpp"

namespace tgfx {
class IRenderDevice;
}

namespace termin {

using FBOPoolEntry = tgfx::RenderTargetEntry;

class RENDER_API FBOPool : public tgfx::RenderTargetPool {
public:
    std::unordered_map<std::string, std::string> alias_to_canonical;

public:
    FBOPool() = default;
    FBOPool(FBOPool&&) = default;
    FBOPool& operator=(FBOPool&&) = default;
    FBOPool(const FBOPool&) = delete;
    FBOPool& operator=(const FBOPool&) = delete;
    ~FBOPool() = default;

    // Allocate a pair of owned `tgfx::TextureHandle`s (color + optional
    // depth) via the render device. Callers use the returned handles
    // with `RenderContext2::begin_pass`, which assembles a cached GL FBO
    // inside the device on demand.
    //
    // On resize/format change the old handles are destroyed and new
    // ones are allocated; the device's internal FBO cache is also
    // invalidated because the driver may recycle gl_ids.
    bool ensure_native(
        tgfx::IRenderDevice& device,
        const std::string& key,
        const tgfx::RenderTargetPoolDesc& desc
    );

    // Persistent tgfx2 texture handles for this entry's color / depth
    // attachment. Alias-resolving.
    tgfx::TextureHandle get_color_tgfx2(const std::string& key) const;
    tgfx::TextureHandle get_depth_tgfx2(const std::string& key) const;

    // Device that owns the entries' native textures (null if the
    // pool is empty). All entries must share the same device during
    // normal pipeline execution.
    tgfx::IRenderDevice* device() const {
        return entries.empty() ? nullptr : entries.front().native_device;
    }

    void add_alias(const std::string& alias, const std::string& canonical);
    void clear();

    std::vector<std::string> keys() const;
};

} // namespace termin
