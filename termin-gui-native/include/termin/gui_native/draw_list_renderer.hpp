#pragma once

#include <memory>
#include <string>

#include <termin/gui_native/tc_ui_document.h>

#include <tgfx2/canvas2d_renderer.hpp>

namespace termin::gui_native {

class UiDrawListRenderer {
public:
    bool set_default_font_path(const std::string& path, int default_size_px = 14);
    void render(tgfx::RenderContext2& context, const tc_ui_draw_list* draw_list, int width, int height);
    void release_gpu();

private:
    std::unique_ptr<tgfx::FontAtlas> owned_font_;
    tgfx::Canvas2DRenderer canvas_;
    bool missing_font_logged_ = false;
};

} // namespace termin::gui_native
