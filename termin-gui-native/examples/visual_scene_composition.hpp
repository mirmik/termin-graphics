#pragma once

#include <memory>

#include <termin/gui_native/tc_document.hpp>
#include <termin/gui_native/widgets.hpp>
#include <termin_visual_scene/builtin_items2d.hpp>

namespace termin::gui_native::examples {

    struct VisualSceneCompositionState {
        ~VisualSceneCompositionState() {
            tc_visual_scene_destroy(scene);
        }
        tc_visual_scene_handle scene = tc_visual_scene_handle_invalid();
        int reset_clicks = 0;
        int portal_clicks = 0;
        int item_moves = 0;
        termin::visual::GraphicItem2D* drag_item = nullptr;
        tc_ui_point drag_start_world{};
        tc_ui_point drag_start_position{};
    };

    struct VisualSceneCompositionRefs {
        BoxLayout* root = nullptr;
        BoxLayout* controls = nullptr;
        Label* status = nullptr;
        Button* reset = nullptr;
        SceneView* view = nullptr;
        Button* portal_button = nullptr;
        termin::visual::TcVisualScene scene;
        termin::visual::RoundedRectItem2D* card = nullptr;
        termin::visual::EllipseItem2D* ellipse = nullptr;
        termin::visual::HitRegionItem2D* portal = nullptr;
        std::shared_ptr<VisualSceneCompositionState> state;
    };

    VisualSceneCompositionRefs build_visual_scene_composition(TcDocument document);
    int run_visual_scene_composition_headless_smoke();

} // namespace termin::gui_native::examples
