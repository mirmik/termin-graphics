#include "showcase_window.hpp"
#include "visual_scene_composition.hpp"

#include <string_view>

int main(int argc, char** argv) {
    using namespace termin::gui_native::examples;
    if (argc > 1 && std::string_view(argv[1]) == "--headless-smoke") {
        return run_visual_scene_composition_headless_smoke();
    }
    return run_document_window(
        "termin-gui-native: widgets + TcVisualScene",
        [](termin::gui_native::TcDocument document) {
            build_visual_scene_composition(document);
        });
}
