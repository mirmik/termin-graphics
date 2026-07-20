#pragma once

#include <array>
#include <cstddef>
#include <functional>
#include <memory>
#include <string>

#include <termin/gui_native/application_host_export.h>
#include <termin/gui_native/document.hpp>
#include <termin/platform/backend_window.hpp>

namespace termin::gui_native {

struct ApplicationHostConfig {
    WindowConfig window;
    // Empty paths are resolved from environment overrides and then from the
    // SDK that contains the loaded application-host library.
    std::string sdk_root;
    std::string font_path;
    std::string shader_compiler_path;
    std::string slang_compiler_path;
    std::string shader_cache_root;
    std::string shader_artifact_root;
    int font_size = 14;
    std::array<float, 4> clear_color{0.03f, 0.035f, 0.045f, 1.0f};
    bool enable_text_input = true;
    bool configure_shader_runtime = true;
    bool enable_shader_dev_compile = true;
    bool continuous_rendering = true;
};

// Owns one native GUI window and its frame resources. The Document is supplied
// by the application and must outlive the host.
class TERMIN_GUI_NATIVE_HOST_API ApplicationHost {
public:
    ApplicationHost(Document& document, ApplicationHostConfig config);
    ApplicationHost(
        Document& document,
        ApplicationHostConfig config,
        std::unique_ptr<WindowedGraphicsSession> graphics_session,
        BackendWindowPtr window);
    ~ApplicationHost();

    ApplicationHost(const ApplicationHost&) = delete;
    ApplicationHost& operator=(const ApplicationHost&) = delete;
    ApplicationHost(ApplicationHost&&) noexcept;
    ApplicationHost& operator=(ApplicationHost&&) noexcept;

    BackendWindow& window();
    const BackendWindow& window() const;
    tgfx::IRenderDevice& device();
    const tgfx::IRenderDevice& device() const;

    // Routes all currently queued events into the bound Document. Returns the
    // number of portable window events consumed by this host.
    size_t pump_events();

    // Renders and presents one frame. Returns false while the drawable is
    // minimized or otherwise has a zero-sized framebuffer.
    bool render_frame();

    // Convenience step: pump events, stop on close, then render if drawable.
    // Returns false once the window requested shutdown.
    bool tick();

    // Deferred callbacks run on the host thread at the beginning of tick().
    // They may safely request another frame or mutate the bound Document.
    void defer(std::function<void()> callback);
    void request_repaint();
    bool repaint_requested() const;

    bool should_close() const;
    void request_close();
    void wait_idle();

    size_t rendered_frame_count() const;
    int framebuffer_width() const;
    int framebuffer_height() const;
    tgfx::TextureHandle color_target() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace termin::gui_native
