#include <termin/gui_native/offscreen_composition.hpp>

int main() {
    termin::gui_native::OffscreenGuiCompositionConfig config;
    config.width = 32;
    config.height = 24;
    config.continuous_rendering = false;

    termin::gui_native::OffscreenGuiComposition composition(std::move(config));
    if (!composition.render_frame() || composition.frame_generation() != 1)
        return 1;
    const auto pixels = composition.read_frame_rgba_float();
    if (pixels.size() != 32u * 24u * 4u)
        return 2;
    composition.close();
    return composition.is_open() ? 3 : 0;
}
