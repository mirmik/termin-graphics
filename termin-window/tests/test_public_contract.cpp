#include <type_traits>

#include "termin/platform/backend_window.hpp"
#ifdef TERMIN_WINDOW_HAS_SDL
#include "termin/platform/sdl_backend_window.hpp"
#endif

namespace {

class FakeWindow final : public termin::BackendWindow {
public:
    tgfx::IRenderDevice* device() override { return nullptr; }
    tgfx::RenderContext2* context() override { return nullptr; }
    tgfx::BackendType backend_type() const override { return {}; }
    tgfx::PresentationMode requested_presentation_mode() const override { return {}; }
    tgfx::PresentationMode presentation_mode() const override { return {}; }
    bool should_close() const override { return false; }
    void set_should_close(bool) override {}
    void maximize() override {}
    void set_title(const std::string&) override {}
    void set_fullscreen(bool) override {}
    void set_text_input_enabled(bool) override {}
    void close() override {}
    bool poll_event(termin::WindowEvent& event) override {
        if (emitted_) return false;
        emitted_ = true;
        event.type = termin::WindowEventType::KeyPressed;
        event.key.key = termin::WindowKey::A;
        return true;
    }
    std::pair<int, int> window_size() const override { return {320, 200}; }
    std::pair<int, int> framebuffer_size() const override { return {640, 400}; }
    void present(tgfx::TextureHandle) override {}

private:
    bool emitted_ = false;
};

} // namespace

int main() {
#ifdef TERMIN_WINDOW_HAS_SDL
    static_assert(std::is_base_of_v<termin::BackendWindow, termin::SDLBackendWindow>);
    static_assert(!std::is_copy_constructible_v<termin::SDLBackendWindow>);
#endif
    FakeWindow window;
    bool handled = false;
    window.set_event_handler([&handled](const termin::WindowEvent& event) {
        handled = event.type == termin::WindowEventType::KeyPressed &&
            event.key.key == termin::WindowKey::A;
    });
    window.poll_events();
    if (!handled) return 1;
    return 0;
}
