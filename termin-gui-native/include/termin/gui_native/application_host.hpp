#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <termin/gui_native/application_host_export.h>
#include <termin/gui_native/document.hpp>
#include <termin/platform/backend_window.hpp>
#include <tgfx2/enums.hpp>

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

struct OffscreenGuiApplicationConfig {
    GuiApplicationConfig gui;
    int width = 1280;
    int height = 720;
    tgfx::BackendType backend = tgfx::BackendType::Vulkan;
    // Shader configuration follows the same installed-SDK resolution contract
    // as StandaloneGuiApplication, without creating a native window.
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
class ColorPicker;
struct GuiApplicationHostLeaseState;

// Ordered backend-neutral input and application close state. Implementations
// may accept and drain events from different threads.
class TERMIN_GUI_NATIVE_HOST_API GuiInputSource {
  public:
    virtual ~GuiInputSource() = default;
    virtual bool poll_event(WindowEvent& event) = 0;
    virtual bool should_close() const = 0;
    virtual void request_close() = 0;
};

// Explicit platform capabilities used by Document interaction. A
// GuiApplicationHost requires all three capabilities; implementations that do
// not provide one must report it during construction instead of silently
// turning the operation into a no-op.
class TERMIN_GUI_NATIVE_HOST_API GuiPlatformServices {
  public:
    virtual ~GuiPlatformServices() = default;
    virtual bool supports_text_input() const noexcept = 0;
    virtual bool supports_clipboard() const noexcept = 0;
    virtual bool supports_cursor() const noexcept = 0;
    virtual bool set_text_input_enabled(bool enabled) = 0;
    virtual std::string clipboard_text() const = 0;
    virtual bool set_clipboard_text(const std::string& text) = 0;
    virtual bool set_cursor(WindowCursor cursor) = 0;
};

// Thread-safe producer queue used by automation and offscreen composition.
class TERMIN_GUI_NATIVE_HOST_API QueuedGuiInputSource final : public GuiInputSource {
  public:
    QueuedGuiInputSource();
    ~QueuedGuiInputSource() override;

    QueuedGuiInputSource(const QueuedGuiInputSource&) = delete;
    QueuedGuiInputSource& operator=(const QueuedGuiInputSource&) = delete;
    QueuedGuiInputSource(QueuedGuiInputSource&&) = delete;
    QueuedGuiInputSource& operator=(QueuedGuiInputSource&&) = delete;

    void push_event(WindowEvent event);
    bool poll_event(WindowEvent& event) override;
    bool should_close() const override;
    void request_close() override;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Meaningful platform implementation for tests, automation and the future
// offscreen composition. State remains observable instead of disappearing
// into window-shaped no-ops.
class TERMIN_GUI_NATIVE_HOST_API InMemoryGuiPlatformServices final : public GuiPlatformServices {
  public:
    InMemoryGuiPlatformServices();
    ~InMemoryGuiPlatformServices() override;

    InMemoryGuiPlatformServices(const InMemoryGuiPlatformServices&) = delete;
    InMemoryGuiPlatformServices& operator=(const InMemoryGuiPlatformServices&) = delete;
    InMemoryGuiPlatformServices(InMemoryGuiPlatformServices&&) = delete;
    InMemoryGuiPlatformServices& operator=(InMemoryGuiPlatformServices&&) = delete;

    bool supports_text_input() const noexcept override;
    bool supports_clipboard() const noexcept override;
    bool supports_cursor() const noexcept override;
    bool set_text_input_enabled(bool enabled) override;
    std::string clipboard_text() const override;
    bool set_clipboard_text(const std::string& text) override;
    bool set_cursor(WindowCursor cursor) override;

    bool text_input_enabled() const;
    WindowCursor cursor() const;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

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
// domain, one Document and the three typed environment boundaries. All
// borrowed objects must outlive it.
class TERMIN_GUI_NATIVE_HOST_API GuiApplicationHost {
  public:
    GuiApplicationHost(tgfx::GraphicsHost& graphics, Document& document,
                       GuiApplicationConfig config, GuiFrameEndpoint& frame_endpoint,
                       GuiInputSource& input_source, GuiPlatformServices& platform_services);
    ~GuiApplicationHost();

    GuiApplicationHost(const GuiApplicationHost&) = delete;
    GuiApplicationHost& operator=(const GuiApplicationHost&) = delete;
    GuiApplicationHost(GuiApplicationHost&&) = delete;
    GuiApplicationHost& operator=(GuiApplicationHost&&) = delete;

    tgfx::GraphicsHost& graphics();
    const tgfx::GraphicsHost& graphics() const;
    tgfx::IRenderDevice& device();
    const tgfx::IRenderDevice& device() const;

    size_t pump_events();
    bool render_frame();
    bool tick();
    void request_repaint();
    bool repaint_requested() const;
    void set_event_interceptor(std::function<bool(const WindowEvent&)> interceptor);
    void set_frame_callbacks(std::function<void(GuiFrame&)> before_ui_frame,
                             std::function<void(GuiFrame&)> after_ui_frame = {});
    void register_color_picker(ColorPicker& picker);
    void unregister_color_picker(ColorPicker& picker);

    GuiFrameExtension& install_frame_extension(std::unique_ptr<GuiFrameExtension> extension);
    std::unique_ptr<GuiFrameExtension> remove_frame_extension(GuiFrameExtension& extension);

    void wait_idle();
    bool should_close() const;
    void request_close();
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
    void request_repaint();
    bool repaint_requested() const;
    void set_event_interceptor(std::function<bool(const WindowEvent&)> interceptor);
    void set_frame_callbacks(std::function<void(GuiFrame&)> before_ui_frame,
                             std::function<void(GuiFrame&)> after_ui_frame = {});
    void register_color_picker(ColorPicker& picker);
    void unregister_color_picker(ColorPicker& picker);

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

// Owning, display-independent composition for automation, remote tools and
// server rendering. It uses an isolated graphics domain and the same
// GuiApplicationHost frame/input/platform contracts as GuiWindowHost.
class TERMIN_GUI_NATIVE_HOST_API OffscreenGuiApplication {
  public:
    explicit OffscreenGuiApplication(OffscreenGuiApplicationConfig config);
    ~OffscreenGuiApplication();

    OffscreenGuiApplication(const OffscreenGuiApplication&) = delete;
    OffscreenGuiApplication& operator=(const OffscreenGuiApplication&) = delete;
    OffscreenGuiApplication(OffscreenGuiApplication&&) noexcept;
    OffscreenGuiApplication& operator=(OffscreenGuiApplication&&) noexcept;

    tgfx::GraphicsHost& graphics();
    const tgfx::GraphicsHost& graphics() const;
    Document& document();
    const Document& document() const;
    GuiApplicationHost& application_host();
    const GuiApplicationHost& application_host() const;
    QueuedGuiInputSource& input_source();
    InMemoryGuiPlatformServices& platform_services();

    void push_event(WindowEvent event);
    size_t pump_events();
    bool render_frame();
    bool tick();
    void resize(int width, int height);
    std::pair<int, int> framebuffer_size() const;
    uint64_t frame_generation() const;
    tgfx::TextureHandle latest_frame_texture() const;
    std::pair<int, int> latest_frame_size() const;
    std::vector<float> read_frame_rgba_float();

    void request_repaint();
    bool repaint_requested() const;
    bool should_close() const;
    void request_close();
    void wait_idle();
    void close();
    bool is_open() const;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace termin::gui_native
