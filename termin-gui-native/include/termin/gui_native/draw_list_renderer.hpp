#pragma once

#include <termin/gui_native/tc_ui_document.h>

#include <tgfx2/canvas2d_renderer.hpp>

namespace termin::gui_native {

class UiDrawListRenderer {
public:
    void render(tgfx::RenderContext2& context, const tc_ui_draw_list* draw_list, int width, int height);
    void release_gpu();

private:
    tgfx::Canvas2DRenderer canvas_;
};

} // namespace termin::gui_native
