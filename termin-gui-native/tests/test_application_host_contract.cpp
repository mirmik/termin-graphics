#include <cassert>
#include <type_traits>

#include <termin/gui_native/application_host.hpp>

int main() {
    static_assert(std::is_abstract_v<termin::gui_native::GuiFrameEndpoint>);
    static_assert(!std::is_copy_constructible_v<termin::gui_native::GuiApplicationHost>);
    static_assert(!std::is_move_constructible_v<termin::gui_native::GuiApplicationHost>);
    static_assert(!std::is_copy_constructible_v<termin::gui_native::GuiWindowHost>);
    static_assert(!std::is_move_constructible_v<termin::gui_native::GuiWindowHost>);
    static_assert(std::is_move_constructible_v<termin::gui_native::Document>);
    static_assert(!std::is_copy_constructible_v<termin::gui_native::StandaloneGuiApplication>);
    static_assert(std::is_move_constructible_v<termin::gui_native::StandaloneGuiApplication>);

    termin::gui_native::GuiApplicationConfig application_config;
    assert(application_config.font_size == 14);
    assert(application_config.continuous_rendering);

    termin::gui_native::GuiWindowConfig config;
    assert(config.window.width == 1280);
    assert(config.window.height == 720);
    assert(config.font_size == 14);
    assert(config.enable_text_input);
    assert(config.continuous_rendering);

    termin::gui_native::StandaloneGuiApplicationConfig standalone;
    assert(standalone.gui.window.width == 1280);
    assert(standalone.enable_shader_dev_compile);
    return 0;
}
