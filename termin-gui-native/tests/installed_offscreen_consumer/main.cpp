#include <termin/gui_native/application_host.hpp>

int main() {
    termin::gui_native::OffscreenGuiApplicationConfig config;
    config.width = 32;
    config.height = 24;
    config.gui.continuous_rendering = false;

    termin::gui_native::OffscreenGuiApplication application(std::move(config));
    if (!application.render_frame() || application.frame_generation() != 1)
        return 1;
    const auto pixels = application.read_frame_rgba_float();
    if (pixels.size() != 32u * 24u * 4u)
        return 2;
    application.close();
    return application.is_open() ? 3 : 0;
}
