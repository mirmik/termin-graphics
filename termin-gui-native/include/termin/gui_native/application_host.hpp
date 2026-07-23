#pragma once

#include <array>
#include <cstddef>
#include <functional>
#include <memory>
#include <string>

#include <termin/gui_native/application_host_export.h>
#include <termin/gui_native/document.hpp>
#include <termin/platform/backend_window.hpp>

namespace tgfx {
class GraphicsHost;
class IRenderDevice;
}

namespace termin::gui_native {

struct GuiWindowConfig {
    WindowConfig window;
    // Empty font paths resolve from TERMIN_UI_FONT and then from the SDK that
    // contains the loaded application-host library.
    std::string font_path;
    int font_size = 14;
    std::array<float, 4> clear_color{0.03f, 0.035f, 0.045f, 1.0f};
    bool enable_text_input = true;
    bool continuous_rendering = true;
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

// Narrow frame services for native extensions. RenderContext2 is intentionally
// absent so an extension cannot nest or take over the host-owned frame.
class TERMIN_GUI_NATIVE_HOST_API GuiWindowFrame {
public:
    GuiWindowHost& host() const;
    tgfx::GraphicsHost& graphics() const;
    tgfx::IRenderDevice& device() const;
    tgfx::TextureHandle color_target() const;
    int framebuffer_width() const;
    int framebuffer_height() const;

private:
    friend class GuiWindowHost;
    GuiWindowFrame(GuiWindowHost& host, int width, int height);

    GuiWindowHost* host_ = nullptr;
    int width_ = 0;
    int height_ = 0;
};

class TERMIN_GUI_NATIVE_HOST_API GuiWindowFrameExtension {
public:
    virtual ~GuiWindowFrameExtension() = default;
    virtual void before_ui_frame(GuiWindowFrame&) {}
    virtual void after_ui_frame(GuiWindowFrame&) {}
    virtual void detach(GuiWindowHost&) noexcept {}
};

// Per-window adapter. It borrows the canonical graphics domain and Document,
// and owns only this window plus its UI/frame resources. Both borrowed objects
// must outlive the host.
class TERMIN_GUI_NATIVE_HOST_API GuiWindowHost {
public:
    GuiWindowHost(
        WindowedGraphicsSession& graphics_session,
        Document& document,
        GuiWindowConfig config);
    GuiWindowHost(
        tgfx::GraphicsHost& graphics,
        Document& document,
        GuiWindowConfig config,
        BackendWindowPtr window);
    ~GuiWindowHost();

    GuiWindowHost(const GuiWindowHost&) = delete;
    GuiWindowHost& operator=(const GuiWindowHost&) = delete;
    GuiWindowHost(GuiWindowHost&&) = delete;
    GuiWindowHost& operator=(GuiWindowHost&&) = delete;

    BackendWindow& window();
    const BackendWindow& window() const;
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

    GuiWindowFrameExtension& install_frame_extension(
        std::unique_ptr<GuiWindowFrameExtension> extension);
    std::unique_ptr<GuiWindowFrameExtension> remove_frame_extension(
        GuiWindowFrameExtension& extension);

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
