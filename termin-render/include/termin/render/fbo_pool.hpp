// fbo_pool.hpp - FBO pool for managing framebuffer allocation
#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "tgfx/graphics_backend.hpp"
#include "tgfx2/enums.hpp"
#include "tgfx2/handles.hpp"
#include "termin/render/render_export.hpp"
#include "termin/render/resource_spec.hpp"

namespace tgfx2 {
class IRenderDevice;
class OpenGLRenderDevice;
}

namespace termin {

struct FBOPoolEntry {
public:
    std::string key;
    int width = 0;
    int height = 0;
    int samples = 1;

    // --- Native tgfx2 path (Stage 8.3+) ---
    // When `native` is true, color_tgfx2 / depth_tgfx2 are owned
    // textures created via native_device->create_texture(). `fbo`
    // and legacy fields are unused.
    bool native = false;
    tgfx2::IRenderDevice* native_device = nullptr;
    tgfx2::PixelFormat color_format = tgfx2::PixelFormat::RGBA8_UNorm;
    tgfx2::PixelFormat depth_format = tgfx2::PixelFormat::D32F;
    bool has_depth = false;

    // --- Legacy path (kept for present_to_screen and interim interop) ---
    FramebufferHandlePtr fbo;
    std::string format;
    TextureFilter filter = TextureFilter::LINEAR;
    bool external = false;

    // tgfx2 texture handles for this render target.
    // - Native path: owned via native_device, destroyed on recreate/clear.
    // - Legacy path: external wrappers around the FBO's GL textures
    //   (register_external_texture), destroyed via tgfx2_device.
    tgfx2::TextureHandle color_tgfx2;
    tgfx2::TextureHandle depth_tgfx2;

    // Device back-pointer for legacy external-wrap path. Unused when
    // `native` is true (native_device handles destroy instead).
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

    // Stage 8.3: native tgfx2 path. Allocates a pair of owned
    // `tgfx2::TextureHandle`s (color + optional depth) via the render
    // device. No legacy `FramebufferHandle` is created — callers use
    // the returned handles directly with `RenderContext2::begin_pass`,
    // which assembles a cached GL FBO inside the device on demand.
    //
    // Returns true on success. Resize/format changes on subsequent
    // calls transparently destroy the old handles and allocate fresh
    // ones.
    bool ensure_native(
        tgfx2::IRenderDevice& device,
        const std::string& key,
        int width,
        int height,
        tgfx2::PixelFormat color_format = tgfx2::PixelFormat::RGBA8_UNorm,
        bool has_depth = true,
        tgfx2::PixelFormat depth_format = tgfx2::PixelFormat::D32F,
        int samples = 1
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
