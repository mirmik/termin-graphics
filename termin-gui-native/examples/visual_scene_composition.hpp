#pragma once

#include <memory>

#include <termin/gui_native/tc_document.hpp>
#include <termin/gui_native/widgets.hpp>

namespace termin::gui_native::examples {

struct VisualSceneCompositionState {
    int reset_clicks = 0;
    int portal_clicks = 0;
    int item_moves = 0;
};

struct VisualSceneCompositionRefs {
    BoxLayout* root = nullptr;
    BoxLayout* controls = nullptr;
    Label* status = nullptr;
    Button* reset = nullptr;
    SceneView* view = nullptr;
    Button* portal_button = nullptr;
    std::shared_ptr<GraphicsScene> scene;
    GraphicItemRef card;
    GraphicItemRef ellipse;
    GraphicItemRef portal;
    std::shared_ptr<VisualSceneCompositionState> state;
};

VisualSceneCompositionRefs build_visual_scene_composition(TcDocument document);
int run_visual_scene_composition_headless_smoke();

} // namespace termin::gui_native::examples
