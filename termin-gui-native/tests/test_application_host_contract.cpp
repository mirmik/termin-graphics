#include <cassert>
#include <type_traits>

#include <termin/gui_native/application_host.hpp>

int main() {
    static_assert(!std::is_copy_constructible_v<termin::gui_native::ApplicationHost>);
    static_assert(std::is_move_constructible_v<termin::gui_native::ApplicationHost>);

    termin::gui_native::ApplicationHostConfig config;
    assert(config.window.width == 1280);
    assert(config.window.height == 720);
    assert(config.font_size == 14);
    assert(config.enable_text_input);
    assert(config.continuous_rendering);
    return 0;
}
