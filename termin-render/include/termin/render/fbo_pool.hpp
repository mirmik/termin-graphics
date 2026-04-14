// fbo_pool.hpp - FBO pool for managing framebuffer allocation
#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "tgfx/graphics_backend.hpp"
#include "tgfx2/handles.hpp"
#include "termin/render/render_export.hpp"
#include "termin/render/resource_spec.hpp"

namespace tgfx2 { class OpenGLRenderDevice; }

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

    // tgfx2 wrappers around the legacy FBO's color+depth GL textures,
    // created once at ensure() time and reused across frames. Replace
    // the old per-frame wrap_fbo_*_as_tgfx2 churn that forced
    // RenderContext2 to invalidate its FBO cache every end_frame.
    // Empty when the FBO has no matching attachment (e.g. color FBO
    // whose depth is a renderbuffer — depth_tgfx2 stays {}).
    tgfx2::TextureHandle color_tgfx2;
    tgfx2::TextureHandle depth_tgfx2;

    // Device back-pointer used to destroy the wrappers when the entry
    // is re-allocated, resized, or cleared. The device outlives the
    // pool so a raw pointer is safe.
    tgfx2::OpenGLRenderDevice* tgfx2_device = nullptr;

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

    // Create or reuse a legacy FBO. When `tgfx2_device` is non-null,
    // the FBO's color (and depth, if texture-backed) GL objects are
    // also wrapped as tgfx2 external textures and stored on the entry
    // — persistent across frames, destroyed together with the FBO on
    // resize/clear.
    FramebufferHandle* ensure(
        GraphicsBackend* graphics,
        const std::string& key,
        int width,
        int height,
        int samples = 1,
        const std::string& format = "",
        TextureFilter filter = TextureFilter::LINEAR,
        tgfx2::OpenGLRenderDevice* tgfx2_device = nullptr
    );

    FramebufferHandle* get(const std::string& key);

    // Return the persistent tgfx2 wrapper for the FBO's color (or
    // depth) attachment. Resolves through alias mapping. Returns an
    // invalid handle when the entry was allocated without a tgfx2
    // device, or when the underlying attachment is a renderbuffer
    // (depth_tgfx2).
    tgfx2::TextureHandle get_color_tgfx2(const std::string& key) const;
    tgfx2::TextureHandle get_depth_tgfx2(const std::string& key) const;

    void set(const std::string& key, FramebufferHandle* fbo);
    void add_alias(const std::string& alias, const std::string& canonical);
    void clear();

    std::vector<std::string> keys() const;
};

} // namespace termin
