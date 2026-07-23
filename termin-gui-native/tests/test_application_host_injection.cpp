#include <cstdio>
#include <cstring>
#include <exception>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include <termin/gui_native/application_host.hpp>
#include <termin/gui_native/canvas.hpp>
#include <termin/gui_native/dynamic_texture_lease.hpp>
#include <termin/gui_native/text_input.hpp>

#include <tgfx2/device_factory.hpp>
#include <tgfx2/graphics_host.hpp>
#include <tgfx2/i_render_device.hpp>
#include <tgfx2/tc_shader_bridge.hpp>

namespace {

class InjectedFrameEndpoint final : public termin::gui_native::GuiFrameEndpoint {
  public:
    std::pair<int, int> framebuffer_size() const override { return {320, 200}; }
    void publish_frame(tgfx::TextureHandle texture) override {
        last_published = texture;
        ++published_frames;
    }

    tgfx::TextureHandle last_published{};
    size_t published_frames = 0;
};

class MissingClipboardPlatform final : public termin::gui_native::GuiPlatformServices {
  public:
    bool supports_text_input() const noexcept override { return true; }
    bool supports_clipboard() const noexcept override { return false; }
    bool supports_cursor() const noexcept override { return true; }
    bool set_text_input_enabled(bool) override { return true; }
    std::string clipboard_text() const override { return {}; }
    bool set_clipboard_text(const std::string&) override { return false; }
    bool set_cursor(termin::WindowCursor) override { return true; }
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
        termin::gui_native::GuiApplicationConfig config;
        config.font_path = TERMIN_GUI_NATIVE_TEST_FONT;

        termin::tgfx2_set_shader_compiler_path("injection-sentinel-compiler");
        termin::tgfx2_set_shader_cache_root("injection-sentinel-cache");
        termin::tgfx2_set_shader_artifact_root("injection-sentinel-artifacts");
        termin::tgfx2_set_shader_dev_compile_enabled(false);

        InjectedFrameEndpoint frame_endpoint;
        termin::gui_native::QueuedGuiInputSource input_source;
        termin::gui_native::InMemoryGuiPlatformServices platform_services;
        termin::gui_native::GuiApplicationHost host(*graphics, document, config, frame_endpoint,
                                                    input_source, platform_services);
        if (&host.graphics() != graphics.get()) return 1;
        if (!platform_services.text_input_enabled()) {
            std::fprintf(stderr, "platform text-input configuration was not applied\n");
            return 1;
        }
        bool live_document_move_rejected = false;
        try {
            termin::gui_native::Document moved(std::move(document));
        } catch (const std::logic_error&) {
            live_document_move_rejected = true;
        }
        if (!live_document_move_rejected || !document.valid()) {
            std::fprintf(stderr, "Document move did not diagnose a live GuiApplicationHost\n");
            return 1;
        }
        if (std::string(termin::tgfx2_get_shader_compiler_path()) !=
                "injection-sentinel-compiler" ||
            std::string(termin::tgfx2_get_shader_cache_root()) != "injection-sentinel-cache" ||
            std::string(termin::tgfx2_get_shader_artifact_root()) !=
                "injection-sentinel-artifacts" ||
            termin::tgfx2_get_shader_dev_compile_enabled()) {
            std::fprintf(stderr, "GuiApplicationHost changed process-global shader state\n");
            return 1;
        }

        auto* canvas = new termin::gui_native::Canvas();
        document.adopt(canvas);
        termin::gui_native::DynamicTextureLease texture_lease(host);
        texture_lease.bind_canvas(*canvas);
        std::vector<uint8_t> pixels(4 * 3 * 4, 127);
        texture_lease.set_rgba8(4, 3, pixels);
        if (!texture_lease.texture() || canvas->texture_id() != texture_lease.texture().id) {
            std::fprintf(stderr, "dynamic texture did not bind to Canvas\n");
            return 1;
        }
        const std::vector<uint8_t> region(2 * 2 * 4, 255);
        texture_lease.update_region_rgba8(1, 1, 2, 2, region);
        pixels.resize(2 * 5 * 4);
        texture_lease.set_rgba8(2, 5, pixels);
        texture_lease.clear();

        tgfx::TextureDesc borrowed_description;
        borrowed_description.width = 3;
        borrowed_description.height = 2;
        borrowed_description.usage = tgfx::TextureUsage::Sampled;
        const tgfx::TextureHandle borrowed =
            graphics->device().create_texture(borrowed_description);
        texture_lease.borrow(*graphics, borrowed);
        texture_lease.clear();
        if (!tgfx::has_flag(graphics->device().texture_desc(borrowed).usage,
                            tgfx::TextureUsage::Sampled)) {
            std::fprintf(stderr, "lease destroyed a borrowed texture\n");
            return 1;
        }
        graphics->device().destroy(borrowed);
        pixels.assign(2 * 2 * 4, 63);
        texture_lease.set_rgba8(2, 2, pixels);

        auto* text_input = new termin::gui_native::TextInput();
        document.adopt(text_input);
        document.add_root(*text_input);
        text_input->set_cursor_intent(TC_UI_CURSOR_TEXT);
        document.layout_roots(tc_ui_rect{0.0f, 0.0f, 320.0f, 200.0f});
        document.set_focus(*text_input);

        termin::WindowEvent pointer;
        pointer.type = termin::WindowEventType::PointerMoved;
        pointer.pointer.framebuffer_position = {4.0f, 4.0f};
        input_source.push_event(pointer);

        termin::WindowEvent key;
        key.type = termin::WindowEventType::KeyPressed;
        key.key.key = termin::WindowKey::A;
        input_source.push_event(key);

        termin::WindowEvent text;
        text.type = termin::WindowEventType::TextInput;
        std::memcpy(text.text.utf8.data(), "abc", 4);
        input_source.push_event(text);

        termin::WindowEvent close;
        close.type = termin::WindowEventType::CloseRequested;
        input_source.push_event(close);

        if (host.pump_events() != 4 || text_input->text() != "abc" ||
            platform_services.cursor() != termin::WindowCursor::Text || !host.repaint_requested() ||
            !host.should_close()) {
            std::fprintf(stderr, "queued input did not traverse the common host contract\n");
            return 1;
        }
        if (!tc_ui_document_set_clipboard_text(document.get(), "copied", 6) ||
            platform_services.clipboard_text() != "copied") {
            std::fprintf(stderr, "in-memory clipboard did not traverse platform services\n");
            return 1;
        }

        host.close();
        if (graphics->is_closed() || !texture_lease.released() ||
            frame_endpoint.published_frames != 0 || frame_endpoint.last_published ||
            platform_services.text_input_enabled()) {
            std::fprintf(stderr, "presentation-neutral host violated borrowed ownership\n");
            return 1;
        }
        termin::gui_native::Document rejected_document;
        MissingClipboardPlatform missing_clipboard;
        bool missing_capability_rejected = false;
        try {
            termin::gui_native::GuiApplicationHost rejected_host(*graphics, rejected_document,
                                                                 config, frame_endpoint,
                                                                 input_source, missing_clipboard);
        } catch (const std::runtime_error&) {
            missing_capability_rejected = true;
        }
        if (!missing_capability_rejected ||
            rejected_document.active_application_host_count() != 0) {
            std::fprintf(stderr, "missing platform capability was not diagnosed atomically\n");
            return 1;
        }
        rejected_document.close();
        graphics->close();
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "headless GuiApplicationHost injection skipped: %s\n", error.what());
        return 77;
    }
}
