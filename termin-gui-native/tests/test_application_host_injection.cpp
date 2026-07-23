#include <cstdio>
#include <exception>
#include <memory>
#include <stdexcept>
#include <string>

#include <termin/gui_native/application_host.hpp>

#include <tgfx2/device_factory.hpp>
#include <tgfx2/graphics_host.hpp>
#include <tgfx2/i_render_device.hpp>
#include <tgfx2/tc_shader_bridge.hpp>

namespace {

class InjectedWindow final : public termin::BackendWindow {
public:
    InjectedWindow(tgfx::GraphicsHost& graphics, bool& closed)
        : graphics_(&graphics), closed_(&closed) {}

    tgfx::BackendType backend_type() const override {
        return graphics_->device().backend_type();
    }
    tgfx::GraphicsHost& graphics_host() const override { return *graphics_; }
    tgfx::PresentationMode requested_presentation_mode() const override {
        return tgfx::PresentationMode::Immediate;
    }
    tgfx::PresentationMode presentation_mode() const override {
        return tgfx::PresentationMode::Immediate;
    }
    bool should_close() const override { return should_close_; }
    void set_should_close(bool value) override { should_close_ = value; }
    void maximize() override {}
    void set_title(const std::string&) override {}
    void set_size(int, int) override {}
    void set_fullscreen(bool) override {}
    void set_text_input_enabled(bool) override {}
    void set_cursor(termin::WindowCursor) override {}
    std::string clipboard_text() const override { return {}; }
    bool set_clipboard_text(const std::string&) override { return true; }
    void close() override { *closed_ = true; }
    bool poll_event(termin::WindowEvent&) override { return false; }
    std::pair<int, int> window_size() const override { return {320, 200}; }
    std::pair<int, int> framebuffer_size() const override { return {320, 200}; }
    void present(tgfx::TextureHandle) override {}

private:
    tgfx::GraphicsHost* graphics_ = nullptr;
    bool* closed_ = nullptr;
    bool should_close_ = false;
};

tgfx::BackendType headless_backend() {
    if (tgfx::backend_is_compiled(tgfx::BackendType::Vulkan)) {
        return tgfx::BackendType::Vulkan;
    }
    if (tgfx::backend_is_compiled(tgfx::BackendType::D3D11)) {
        return tgfx::BackendType::D3D11;
    }
    return tgfx::BackendType::Null;
}

} // namespace

int main() {
    try {
        const tgfx::BackendType backend = headless_backend();
        if (backend == tgfx::BackendType::Null) return 77;
        auto graphics = tgfx::GraphicsHost::create_isolated(backend);
        termin::gui_native::Document document;
        termin::gui_native::GuiWindowConfig config;
        config.font_path = TERMIN_GUI_NATIVE_TEST_FONT;

        termin::tgfx2_set_shader_compiler_path("injection-sentinel-compiler");
        termin::tgfx2_set_shader_cache_root("injection-sentinel-cache");
        termin::tgfx2_set_shader_artifact_root("injection-sentinel-artifacts");
        termin::tgfx2_set_shader_dev_compile_enabled(false);

        bool window_closed = false;
        auto window = std::make_unique<InjectedWindow>(*graphics, window_closed);
        termin::gui_native::GuiWindowHost host(
            *graphics, document, config, std::move(window));
        if (&host.graphics() != graphics.get()) return 1;
        bool live_document_move_rejected = false;
        try {
            termin::gui_native::Document moved(std::move(document));
        } catch (const std::logic_error&) {
            live_document_move_rejected = true;
        }
        if (!live_document_move_rejected || !document.valid()) {
            std::fprintf(stderr, "Document move did not diagnose a live GuiWindowHost\n");
            return 1;
        }
        if (std::string(termin::tgfx2_get_shader_compiler_path()) !=
                "injection-sentinel-compiler" ||
            std::string(termin::tgfx2_get_shader_cache_root()) !=
                "injection-sentinel-cache" ||
            std::string(termin::tgfx2_get_shader_artifact_root()) !=
                "injection-sentinel-artifacts" ||
            termin::tgfx2_get_shader_dev_compile_enabled()) {
            std::fprintf(stderr, "GuiWindowHost changed process-global shader state\n");
            return 1;
        }
        host.close();
        if (!window_closed || graphics->is_closed()) {
            std::fprintf(stderr, "injected host violated borrowed ownership\n");
            return 1;
        }
        graphics->close();
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "headless GuiWindowHost injection skipped: %s\n", error.what());
        return 77;
    }
}
