#pragma once

#include <array>
#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <utility>

#include <termin/gui_native/application_host_export.h>
#include <termin/gui_native/document.hpp>
#include <termin/platform/backend_window.hpp>

namespace tgfx {
class GraphicsHost;
class IRenderDevice;
} // namespace tgfx

namespace termin::gui_native {

struct GuiApplicationConfig {
    // Empty font paths resolve from TERMIN_UI_FONT and then from the SDK that
    // contains the loaded application-host library.
    std::string font_path;
    int font_size = 14;
    std::array<float, 4> clear_color{0.03f, 0.035f, 0.045f, 1.0f};
    bool enable_text_input = true;
    bool continuous_rendering = true;
};

struct GuiWindowConfig {
    WindowConfig window;
    // Flat compatibility projection retained for existing windowed callers.
    std::string font_path;
    int font_size = 14;
    std::array<float, 4> clear_color{0.03f, 0.035f, 0.045f, 1.0f};
    bool enable_text_input = true;
    bool continuous_rendering = true;

    GuiApplicationConfig application_config() const {
        GuiApplicationConfig result;
        result.font_path = font_path;
        result.font_size = font_size;
        result.clear_color = clear_color;
        result.enable_text_input = enable_text_input;
        result.continuous_rendering = continuous_rendering;
        return result;
    }
};

struct StandaloneGuiApplicationConfig {
    GuiWindowConfig gui;
    // Shader configuration belongs to this standalone composition root, not
    // to GuiWindowHost. Empty paths resolve from environment/installed SDK.
    std::string sdk_root;
    std::string shader_compiler_path;
    std::string slang_compiler_path;
    std::string shader_cache_root;
    std::string shader_artifact_root;
    bool enable_shader_dev_compile = true;
};

class GuiWindowHost;
class GuiApplicationHost;
class DynamicTextureLease;
struct GuiApplicationHostLeaseState;

// Presentation-neutral destination for a completed native GUI frame. The
// endpoint is borrowed by GuiApplicationHost and must outlive it.
class TERMIN_GUI_NATIVE_HOST_API GuiFrameEndpoint {
  public:
    virtual ~GuiFrameEndpoint() = default;
    virtual std::pair<int, int> framebuffer_size() const = 0;
    virtual void publish_frame(tgfx::TextureHandle color_texture) = 0;
};

// Narrow frame services for native extensions. RenderContext2 is intentionally
// absent so an extension cannot nest or take over the host-owned frame.
class TERMIN_GUI_NATIVE_HOST_API GuiFrame {
  public:
    GuiApplicationHost& host() const;
    tgfx::GraphicsHost& graphics() const;
    tgfx::IRenderDevice& device() const;
    tgfx::TextureHandle color_target() const;
    int framebuffer_width() const;
    int framebuffer_height() const;

  private:
    friend class GuiApplicationHost;
    GuiFrame(GuiApplicationHost& host, int width, int height);

    GuiApplicationHost* host_ = nullptr;
    int width_ = 0;
    int height_ = 0;
};

class TERMIN_GUI_NATIVE_HOST_API GuiFrameExtension {
  public:
    virtual ~GuiFrameExtension() = default;
    virtual void before_ui_frame(GuiFrame&) {}
    virtual void after_ui_frame(GuiFrame&) {}
    virtual void detach(GuiApplicationHost&) noexcept {}
};

// Compatibility names retained for extensions written against the first
// window-only host API.
using GuiWindowFrame = GuiFrame;
using GuiWindowFrameExtension = GuiFrameExtension;

// Shared native GUI frame implementation. It borrows one canonical graphics
// domain, one Document and one frame endpoint. Input and platform services are
// supplied by a surrounding composition such as GuiWindowHost.
class TERMIN_GUI_NATIVE_HOST_API GuiApplicationHost {
  public:
    GuiApplicationHost(tgfx::GraphicsHost& graphics, Document& document,
                       GuiApplicationConfig config, GuiFrameEndpoint& frame_endpoint);
    ~GuiApplicationHost();

    GuiApplicationHost(const GuiApplicationHost&) = delete;
    GuiApplicationHost& operator=(const GuiApplicationHost&) = delete;
    GuiApplicationHost(GuiApplicationHost&&) = delete;
    GuiApplicationHost& operator=(GuiApplicationHost&&) = delete;

    tgfx::GraphicsHost& graphics();
    const tgfx::GraphicsHost& graphics() const;
    tgfx::IRenderDevice& device();
    const tgfx::IRenderDevice& device() const;

    bool render_frame();
    void run_deferred();
    void defer(std::function<void()> callback);
    void request_repaint();
    bool repaint_requested() const;

    GuiFrameExtension& install_frame_extension(std::unique_ptr<GuiFrameExtension> extension);
    std::unique_ptr<GuiFrameExtension> remove_frame_extension(GuiFrameExtension& extension);

    void wait_idle();
    void close();
    bool is_open() const;

    bool continuous_rendering() const;
    size_t rendered_frame_count() const;
    int framebuffer_width() const;
    int framebuffer_height() const;
    tgfx::TextureHandle color_target() const;

  private:
    friend class DynamicTextureLease;
    std::shared_ptr<GuiApplicationHostLeaseState> texture_lease_state() const;
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Per-window adapter. It borrows the canonical graphics domain and Document,
// owns its BackendWindow and delegates all UI/frame work to one
// GuiApplicationHost. Both borrowed objects must outlive the host.
class TERMIN_GUI_NATIVE_HOST_API GuiWindowHost {
  public:
    GuiWindowHost(WindowedGraphicsSession& graphics_session, Document& document,
                  GuiWindowConfig config);
    GuiWindowHost(tgfx::GraphicsHost& graphics, Document& document, GuiWindowConfig config,
                  BackendWindowPtr window);
    ~GuiWindowHost();

    GuiWindowHost(const GuiWindowHost&) = delete;
    GuiWindowHost& operator=(const GuiWindowHost&) = delete;
    GuiWindowHost(GuiWindowHost&&) = delete;
    GuiWindowHost& operator=(GuiWindowHost&&) = delete;

    BackendWindow& window();
    const BackendWindow& window() const;
    GuiApplicationHost& application_host();
    const GuiApplicationHost& application_host() const;
    tgfx::GraphicsHost& graphics();
    const tgfx::GraphicsHost& graphics() const;
    tgfx::IRenderDevice& device();
    const tgfx::IRenderDevice& device() const;

    size_t pump_events();
    bool render_frame();
    bool tick();

    // Submission is thread-safe; callbacks execute on the owner thread at the
    // beginning of a later tick. Nested submissions wait for the next tick.
    void defer(std::function<void()> callback);
    void request_repaint();
    bool repaint_requested() const;

    GuiWindowFrameExtension&
    install_frame_extension(std::unique_ptr<GuiWindowFrameExtension> extension);
    std::unique_ptr<GuiWindowFrameExtension>
    remove_frame_extension(GuiWindowFrameExtension& extension);

    bool should_close() const;
    void request_close();
    void wait_idle();
    void close();
    bool is_open() const;

    size_t rendered_frame_count() const;
    int framebuffer_width() const;
    int framebuffer_height() const;
    tgfx::TextureHandle color_target() const;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// One-window convenience composition. It owns one session, one Document and
// one GuiWindowHost in the required shutdown order.
class TERMIN_GUI_NATIVE_HOST_API StandaloneGuiApplication {
  public:
    explicit StandaloneGuiApplication(StandaloneGuiApplicationConfig config);
    ~StandaloneGuiApplication();

    StandaloneGuiApplication(const StandaloneGuiApplication&) = delete;
    StandaloneGuiApplication& operator=(const StandaloneGuiApplication&) = delete;
    StandaloneGuiApplication(StandaloneGuiApplication&&) noexcept;
    StandaloneGuiApplication& operator=(StandaloneGuiApplication&&) noexcept;

    WindowedGraphicsSession& graphics_session();
    const WindowedGraphicsSession& graphics_session() const;
    Document& document();
    const Document& document() const;
    GuiWindowHost& window_host();
    const GuiWindowHost& window_host() const;
    void close();
    bool is_open() const;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace termin::gui_native
