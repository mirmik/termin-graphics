#include <stdexcept>
#include <type_traits>

#include "termin/platform/backend_window.hpp"
#include "termin/window/window_manager.hpp"
#include "tgfx2/graphics_host.hpp"
#ifdef TERMIN_WINDOW_HAS_SDL
#include "termin/platform/sdl_backend_window.hpp"
#endif

namespace {

template <typename T>
concept ExposesGraphicsDevice = requires(T& value) {
    value.device();
};

template <typename T>
concept ExposesGraphicsContext = requires(T& value) {
    value.context();
};

class FakeWindow final : public termin::BackendWindow {
public:
    tgfx::BackendType backend_type() const override { return {}; }
    tgfx::GraphicsHost& graphics_host() const override {
        throw std::logic_error("fake window has no graphics domain");
    }
    tgfx::PresentationMode requested_presentation_mode() const override { return {}; }
    tgfx::PresentationMode presentation_mode() const override { return {}; }
    bool should_close() const override { return false; }
    void set_should_close(bool) override {}
    void maximize() override {}
    void set_title(const std::string&) override {}
    void set_size(int, int) override {}
    void set_fullscreen(bool) override {}
    void set_text_input_enabled(bool) override {}
    void set_cursor(termin::WindowCursor) override {}
    std::string clipboard_text() const override { return clipboard_; }
    bool set_clipboard_text(const std::string& text) override {
        clipboard_ = text;
        return true;
    }
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
    std::string clipboard_;
};

} // namespace

int main() {
#ifdef TERMIN_WINDOW_HAS_SDL
    static_assert(std::is_base_of_v<termin::BackendWindow, termin::SDLBackendWindow>);
    static_assert(std::is_base_of_v<termin::BackendWindowSystem, termin::SDLWindowSystem>);
    static_assert(!std::is_copy_constructible_v<termin::SDLBackendWindow>);
#endif
    static_assert(!ExposesGraphicsDevice<termin::BackendWindow>);
    static_assert(!ExposesGraphicsContext<termin::BackendWindow>);
    static_assert(!std::is_constructible_v<tgfx::GraphicsHost, tgfx::IRenderDevice&>);
    static_assert(!std::is_copy_constructible_v<termin::WindowManager>);
    static_assert(!std::is_move_constructible_v<termin::WindowManager>);
    static_assert(std::is_trivially_copyable_v<termin::WindowHandle>);
    static_assert(sizeof(termin::WindowHandle) == sizeof(uint32_t) * 2);
    if (termin::WindowHandle{}) return 1;
    if (!(termin::WindowHandle{1, 2} == termin::WindowHandle{1, 2})) return 1;
    if (termin::WindowHandleHash{}({1, 2}) != termin::WindowHandleHash{}({1, 2})) {
        return 1;
    }
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
