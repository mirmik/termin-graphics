#include <termin/gui_native/tc_ui_document.h>

#include <memory>
#include <new>
#include <string>
#include <vector>

#include <tcbase/tc_log.h>

struct tc_ui_draw_list {
    std::vector<tc_ui_draw_command> commands;
    std::vector<std::unique_ptr<std::string>> text_storage;
};

struct tc_ui_paint_context {
    tc_ui_draw_list* draw_list = nullptr;
};

extern "C" {

tc_ui_draw_list* tc_ui_draw_list_create(void) {
    return new (std::nothrow) tc_ui_draw_list();
}

void tc_ui_draw_list_destroy(tc_ui_draw_list* draw_list) {
    delete draw_list;
}

void tc_ui_draw_list_clear(tc_ui_draw_list* draw_list) {
    if (!draw_list) {
        tc_log_error("[termin-gui-native] cannot clear null UI draw list");
        return;
    }
    draw_list->commands.clear();
    draw_list->text_storage.clear();
}

size_t tc_ui_draw_list_command_count(const tc_ui_draw_list* draw_list) {
    return draw_list ? draw_list->commands.size() : 0;
}

const tc_ui_draw_command* tc_ui_draw_list_command_at(
    const tc_ui_draw_list* draw_list,
    size_t index
) {
    if (!draw_list || index >= draw_list->commands.size()) {
        return nullptr;
    }
    return &draw_list->commands[index];
}

tc_ui_paint_context* tc_ui_paint_context_create(tc_ui_draw_list* draw_list) {
    if (!draw_list) {
        tc_log_error("[termin-gui-native] cannot create paint context with null draw list");
        return nullptr;
    }
    auto* context = new (std::nothrow) tc_ui_paint_context();
    if (!context) {
        tc_log_error("[termin-gui-native] failed to allocate UI paint context");
        return nullptr;
    }
    context->draw_list = draw_list;
    return context;
}

void tc_ui_paint_context_destroy(tc_ui_paint_context* context) {
    delete context;
}

tc_ui_draw_list* tc_ui_paint_context_draw_list(tc_ui_paint_context* context) {
    return context ? context->draw_list : nullptr;
}

static bool append_draw_command(tc_ui_paint_context* context, tc_ui_draw_command command) {
    if (!context || !context->draw_list) {
        tc_log_error("[termin-gui-native] cannot append UI draw command without paint context");
        return false;
    }
    context->draw_list->commands.push_back(command);
    return true;
}

void tc_ui_painter_fill_rect(tc_ui_paint_context* context, tc_ui_rect rect, tc_ui_color color) {
    tc_ui_draw_command command {};
    command.type = TC_UI_DRAW_FILL_RECT;
    command.rect = rect;
    command.color = color;
    append_draw_command(context, command);
}

void tc_ui_painter_stroke_rect(
    tc_ui_paint_context* context,
    tc_ui_rect rect,
    tc_ui_color color,
    float thickness
) {
    tc_ui_draw_command command {};
    command.type = TC_UI_DRAW_STROKE_RECT;
    command.rect = rect;
    command.color = color;
    command.thickness = thickness;
    append_draw_command(context, command);
}

void tc_ui_painter_draw_line(
    tc_ui_paint_context* context,
    tc_ui_point p0,
    tc_ui_point p1,
    tc_ui_color color,
    float thickness
) {
    tc_ui_draw_command command {};
    command.type = TC_UI_DRAW_LINE;
    command.p0 = p0;
    command.p1 = p1;
    command.color = color;
    command.thickness = thickness;
    append_draw_command(context, command);
}

void tc_ui_painter_draw_text(
    tc_ui_paint_context* context,
    const char* text,
    tc_ui_point position,
    float font_size,
    tc_ui_color color
) {
    if (!context || !context->draw_list) {
        tc_log_error("[termin-gui-native] cannot append UI text command without paint context");
        return;
    }
    if (!text || text[0] == '\0' || font_size <= 0.0f) {
        return;
    }

    auto owned_text = std::make_unique<std::string>(text);
    const char* stable_text = owned_text->c_str();
    context->draw_list->text_storage.push_back(std::move(owned_text));

    tc_ui_draw_command command {};
    command.type = TC_UI_DRAW_TEXT;
    command.p0 = position;
    command.color = color;
    command.text = stable_text;
    command.font_size = font_size;
    append_draw_command(context, command);
}

void tc_ui_painter_push_clip(tc_ui_paint_context* context, tc_ui_rect rect) {
    tc_ui_draw_command command {};
    command.type = TC_UI_DRAW_PUSH_CLIP;
    command.rect = rect;
    append_draw_command(context, command);
}

void tc_ui_painter_pop_clip(tc_ui_paint_context* context) {
    tc_ui_draw_command command {};
    command.type = TC_UI_DRAW_POP_CLIP;
    append_draw_command(context, command);
}

} // extern "C"
