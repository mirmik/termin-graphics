#pragma once

#include <cstdint>
#include <memory>
#include <span>

#include <termin/gui_native/application_host_export.h>
#include <tgfx2/handles.hpp>

namespace tgfx {
class GraphicsHost;
}

namespace termin::gui_native {

class Canvas;
class GuiApplicationHost;
class GuiWindowHost;

enum class DynamicTextureOwnership {
    Empty,
    Owned,
    Borrowed,
    Released,
};

enum class CanvasTextureLayer {
    Image,
    Overlay,
};

// Host-bound RGBA8 texture lifetime. Canvas remains a non-owning consumer;
// this object owns upload/recreate/release policy and validates the graphics
// domain and host/document lifetime on every operation.
class TERMIN_GUI_NATIVE_HOST_API DynamicTextureLease {
  public:
    explicit DynamicTextureLease(GuiApplicationHost& host);
    explicit DynamicTextureLease(GuiWindowHost& host);
    ~DynamicTextureLease();

    DynamicTextureLease(const DynamicTextureLease&) = delete;
    DynamicTextureLease& operator=(const DynamicTextureLease&) = delete;
    DynamicTextureLease(DynamicTextureLease&&) noexcept;
    DynamicTextureLease& operator=(DynamicTextureLease&&) noexcept;

    void set_rgba8(uint32_t width, uint32_t height, std::span<const uint8_t> pixels);
    void update_region_rgba8(uint32_t x, uint32_t y, uint32_t width, uint32_t height,
                             std::span<const uint8_t> pixels);
    void borrow(tgfx::GraphicsHost& texture_owner, tgfx::TextureHandle texture);

    void bind_canvas(Canvas& canvas, CanvasTextureLayer layer = CanvasTextureLayer::Image);
    void unbind_canvas(Canvas& canvas, CanvasTextureLayer layer);

    // clear() returns the lease to Empty and keeps it reusable. release()
    // permanently detaches it from the host and is idempotent.
    void clear();
    void release();

    DynamicTextureOwnership ownership() const;
    tgfx::TextureHandle texture() const;
    uint32_t width() const;
    uint32_t height() const;
    bool empty() const;
    bool released() const;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace termin::gui_native
