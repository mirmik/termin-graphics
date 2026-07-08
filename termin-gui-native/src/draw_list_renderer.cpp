#include <termin/gui_native/draw_list_renderer.hpp>

#include <tcbase/tc_log.h>

namespace termin::gui_native {
namespace {

tgfx::CanvasColor canvas_color(tc_ui_color color) {
    return tgfx::CanvasColor {color.r, color.g, color.b, color.a};
}

} // namespace

void UiDrawListRenderer::render(
    tgfx::RenderContext2& context,
    const tc_ui_draw_list* draw_list,
    int width,
    int height
) {
    if (!draw_list) {
        tc_log_error("[termin-gui-native] cannot render null UI draw list");
        return;
    }
    if (width <= 0 || height <= 0) {
        tc_log_error("[termin-gui-native] cannot render UI draw list into invalid viewport %dx%d", width, height);
        return;
    }

    canvas_.begin(context, width, height);
    const size_t count = tc_ui_draw_list_command_count(draw_list);
    for (size_t index = 0; index < count; ++index) {
        const tc_ui_draw_command* command = tc_ui_draw_list_command_at(draw_list, index);
        if (!command) {
            tc_log_error("[termin-gui-native] draw list command disappeared at index %zu", index);
            continue;
        }

        switch (command->type) {
        case TC_UI_DRAW_FILL_RECT:
            canvas_.draw_rect(
                command->rect.x,
                command->rect.y,
                command->rect.width,
                command->rect.height,
                canvas_color(command->color)
            );
            break;
        case TC_UI_DRAW_STROKE_RECT:
            canvas_.draw_rect_outline(
                command->rect.x,
                command->rect.y,
                command->rect.width,
                command->rect.height,
                canvas_color(command->color),
                command->thickness
            );
            break;
        case TC_UI_DRAW_LINE:
            canvas_.draw_line(
                command->p0.x,
                command->p0.y,
                command->p1.x,
                command->p1.y,
                canvas_color(command->color),
                command->thickness
            );
            break;
        case TC_UI_DRAW_PUSH_CLIP:
            canvas_.begin_clip(
                command->rect.x,
                command->rect.y,
                command->rect.width,
                command->rect.height
            );
            break;
        case TC_UI_DRAW_POP_CLIP:
            canvas_.end_clip();
            break;
        default:
            tc_log_error("[termin-gui-native] unknown UI draw command type %d", static_cast<int>(command->type));
            break;
        }
    }
    canvas_.end();
}

void UiDrawListRenderer::release_gpu() {
    canvas_.release_gpu();
}

} // namespace termin::gui_native
