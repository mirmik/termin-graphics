#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <termin/gui_native/document_renderer.hpp>
#include <tgfx2/enums.hpp>

namespace termin::gui_native {

struct OffscreenGuiCompositionConfig {
    DocumentRendererConfig renderer;
    int width = 1280;
    int height = 720;
    tgfx::BackendType backend = tgfx::BackendType::Vulkan;
    bool continuous_rendering = true;
    std::string sdk_root;
    std::string shader_compiler_path;
    std::string slang_compiler_path;
    std::string shader_cache_root;
    std::string shader_artifact_root;
    bool enable_shader_dev_compile = true;
};

class TERMIN_GUI_NATIVE_HOST_API InMemoryDocumentPlatformServices final
    : public DocumentPlatformServices {
  public:
    InMemoryDocumentPlatformServices();
    ~InMemoryDocumentPlatformServices() override;

    bool set_text_input_enabled(bool enabled) override;
    std::string clipboard_text() const override;
    bool set_clipboard_text(const std::string& text) override;
    bool set_cursor(tc_ui_cursor_intent cursor) override;

    bool text_input_enabled() const;
    tc_ui_cursor_intent cursor() const;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Owning no-display composition. It owns an isolated graphics domain,
// Document, renderer, synthetic input queue and in-memory services. It owns no
// application or OS-window lifecycle.
class TERMIN_GUI_NATIVE_HOST_API OffscreenGuiComposition {
  public:
    explicit OffscreenGuiComposition(OffscreenGuiCompositionConfig config);
    ~OffscreenGuiComposition();

    OffscreenGuiComposition(const OffscreenGuiComposition&) = delete;
    OffscreenGuiComposition& operator=(const OffscreenGuiComposition&) = delete;
    OffscreenGuiComposition(OffscreenGuiComposition&&) noexcept;
    OffscreenGuiComposition& operator=(OffscreenGuiComposition&&) noexcept;

    tgfx::GraphicsHost& graphics();
    const tgfx::GraphicsHost& graphics() const;
    Document& document();
    const Document& document() const;
    DocumentRenderer& renderer();
    const DocumentRenderer& renderer() const;
    InMemoryDocumentPlatformServices& platform_services();

    void push_pointer(tc_ui_pointer_event event);
    void push_key(tc_ui_key_event event);
    void push_text(std::string utf8);
    size_t pump_input();
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
    void request_close();
    bool should_close() const;
    void wait_idle();
    void close();
    bool is_open() const;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace termin::gui_native
