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
        case TC_UI_DRAW_TEXT:
            if (canvas_.default_font()) {
                canvas_.draw_text(
                    command->text ? command->text : "",
                    command->p0.x,
                    command->p0.y,
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
    canvas_.end();
}

void UiDrawListRenderer::release_gpu() {
    canvas_.release_gpu();
    if (owned_font_) {
        owned_font_->release_gpu();
    }
}

} // namespace termin::gui_native
