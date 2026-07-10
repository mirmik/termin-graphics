#include <termin/gui_native/draw_list_renderer.hpp>

#include <exception>
#include <string_view>

#include <tcbase/tc_log.h>

namespace termin::gui_native {
namespace {

tgfx::CanvasColor canvas_color(tc_ui_color color) {
    return tgfx::CanvasColor {color.r, color.g, color.b, color.a};
}

} // namespace

bool UiDrawListRenderer::set_default_font_path(const std::string& path, int default_size_px) {
    try {
        owned_font_ = std::make_unique<tgfx::FontAtlas>(path, default_size_px);
        canvas_.set_default_font(owned_font_.get());
        missing_font_logged_ = false;
        return true;
    } catch (const std::exception& e) {
        tc_log_error("[termin-gui-native] failed to load UI font '%s': %s", path.c_str(), e.what());
        owned_font_.reset();
        canvas_.set_default_font(nullptr);
        return false;
    }
}

void UiDrawListRenderer::bind_text_measurer(tc_ui_document* document) {
    tc_ui_document_set_text_measurer(document, &UiDrawListRenderer::measure_text, this);
}

bool UiDrawListRenderer::measure_text(
    void* user_data,
    const char* text_utf8,
    size_t text_byte_length,
    float font_size,
    tc_ui_text_metrics* out_metrics
) {
    auto* self = static_cast<UiDrawListRenderer*>(user_data);
    if (!self || !self->owned_font_ || !out_metrics) {
        return false;
    }
    const std::string_view text(text_utf8 ? text_utf8 : "", text_byte_length);
    self->owned_font_->ensure_glyphs(text, font_size);
    const tgfx::FontAtlas::Size2f measured = self->owned_font_->measure_text(text, font_size);
    out_metrics->width = measured.width;
    out_metrics->height = measured.height;
    out_metrics->ascent = static_cast<float>(self->owned_font_->ascent_px(font_size));
    out_metrics->descent = static_cast<float>(self->owned_font_->descent_px(font_size));
    out_metrics->line_height = static_cast<float>(self->owned_font_->line_height_px(font_size));
    return true;
}

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
    size_t clip_depth = 0;
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
        case TC_UI_DRAW_FILL_ROUNDED_RECT:
            canvas_.draw_rect(
                command->rect.x,
                command->rect.y,
                command->rect.width,
                command->rect.height,
                canvas_color(command->color),
                command->radius
            );
            break;
        case TC_UI_DRAW_STROKE_ROUNDED_RECT:
            canvas_.draw_rounded_rect_outline(
                command->rect.x,
                command->rect.y,
                command->rect.width,
                command->rect.height,
                command->radius,
                canvas_color(command->color),
                command->thickness,
                command->segments > 0 ? command->segments : 6
            );
            break;
        case TC_UI_DRAW_FILL_CIRCLE:
            canvas_.draw_circle(
                command->p0.x,
                command->p0.y,
                command->radius,
                canvas_color(command->color),
                command->segments > 0 ? command->segments : 24
            );
            break;
        case TC_UI_DRAW_STROKE_CIRCLE:
            canvas_.draw_circle_outline(
                command->p0.x,
                command->p0.y,
                command->radius,
                canvas_color(command->color),
                command->thickness,
                command->segments > 0 ? command->segments : 24
            );
            break;
        case TC_UI_DRAW_ARC:
            canvas_.draw_arc(
                command->p0.x,
                command->p0.y,
                command->radius,
                command->start_radians,
                command->end_radians,
                canvas_color(command->color),
                command->thickness,
                command->segments
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
        case TC_UI_DRAW_POLYLINE:
            for (size_t point_index = 1; point_index < command->point_count; ++point_index) {
                canvas_.draw_line(
                    command->points[point_index - 1].x,
                    command->points[point_index - 1].y,
                    command->points[point_index].x,
                    command->points[point_index].y,
                    canvas_color(command->color),
                    command->thickness
                );
            }
            break;
        case TC_UI_DRAW_TEXTURE:
            canvas_.draw_texture(
                tgfx::TextureHandle {command->texture_id},
                command->rect.x,
                command->rect.y,
                command->rect.width,
                command->rect.height,
                canvas_color(command->color),
                command->flip_v
            );
            break;
        case TC_UI_DRAW_PUSH_CLIP:
            canvas_.begin_clip(
                command->rect.x,
                command->rect.y,
                command->rect.width,
                command->rect.height
            );
            clip_depth += 1;
            break;
        case TC_UI_DRAW_POP_CLIP:
            if (clip_depth == 0) {
                tc_log_error("[termin-gui-native] ignoring unmatched UI pop-clip command");
            } else {
                canvas_.end_clip();
                clip_depth -= 1;
            }
            break;
        case TC_UI_DRAW_TEXT:
            if (canvas_.default_font()) {
                // tc_ui draw commands use a baseline origin, while Canvas2DRenderer's
                // left anchor uses the top of the font line. Keep that difference at
                // this backend boundary instead of leaking canvas semantics into every
                // widget's layout code.
                const float line_top = command->p0.y
                    - static_cast<float>(owned_font_->ascent_px(command->font_size));
                canvas_.draw_text(
                    command->text ? command->text : "",
                    command->p0.x,
                    line_top,
                    command->font_size,
                    canvas_color(command->color)
                );
            } else if (!missing_font_logged_) {
                tc_log_error("[termin-gui-native] skipping UI text commands because no default font is configured");
                missing_font_logged_ = true;
            }
            break;
        default:
            tc_log_error("[termin-gui-native] unknown UI draw command type %d", static_cast<int>(command->type));
            break;
        }
    }
    if (clip_depth != 0) {
        tc_log_error(
            "[termin-gui-native] UI draw list ended with %zu unclosed clip command(s)",
            clip_depth
        );
    }
    canvas_.end();
}

void UiDrawListRenderer::release_gpu() {
    canvas_.release_gpu();
    if (owned_font_) {
        owned_font_->release_gpu();
    }
}

} // namespace termin::gui_native
