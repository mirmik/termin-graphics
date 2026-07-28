#pragma once

#include "termin_visual_scene/scene_render2d.hpp"

struct tc_graphic_item_draw_sink {
    tgfx::DrawList2DBuilder* builder = nullptr;
    termin::visual::SceneRenderResourceResolver2D* resolver = nullptr;
};
