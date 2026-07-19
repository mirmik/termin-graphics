#include <termin/gui_native/tc_ui_document.h>

#include <cmath>
#include <exception>
#include <memory>
#include <new>
#include <string>
#include <vector>

#include <tcbase/tc_log.h>

struct tc_ui_draw_list {
    std::vector<tc_ui_draw_command> commands;
    std::vector<std::unique_ptr<std::string>> text_storage;
    std::vector<std::unique_ptr<std::vector<tc_ui_point>>> point_storage;
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
    draw_list->point_storage.clear();
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
    try {
        context->draw_list->commands.push_back(command);
        return true;
    } catch (const std::exception& error) {
        tc_log_error(
            "[termin-gui-native] failed to append UI draw command: %s",
            error.what()
        );
        return false;
    }
}

static bool finite_point(tc_ui_point point) {
    return std::isfinite(point.x) && std::isfinite(point.y);
}

static bool finite_rect(tc_ui_rect rect) {
    return std::isfinite(rect.x) && std::isfinite(rect.y) &&
        std::isfinite(rect.width) && std::isfinite(rect.height);
}

void tc_ui_painter_fill_rect(tc_ui_paint_context* context, tc_ui_rect rect, tc_ui_color color) {
    tc_ui_draw_command command {};
    command.type = TC_UI_DRAW_FILL_RECT;
    command.rect = rect;
    command.color = color;
    append_draw_command(context, command);
}

void tc_ui_painter_fill_rounded_rect(
    tc_ui_paint_context* context,
    tc_ui_rect rect,
    float radius,
    tc_ui_color color
) {
    if (!finite_rect(rect) || !std::isfinite(radius) || radius < 0.0f) {
        tc_log_error("[termin-gui-native] rejected invalid rounded rectangle command");
        return;
    }
    tc_ui_draw_command command {};
    command.type = TC_UI_DRAW_FILL_ROUNDED_RECT;
    command.rect = rect;
    command.radius = radius;
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

void tc_ui_painter_stroke_rounded_rect(
    tc_ui_paint_context* context,
    tc_ui_rect rect,
    float radius,
    tc_ui_color color,
    float thickness
) {
    if (!finite_rect(rect) || !std::isfinite(radius) || radius < 0.0f ||
        !std::isfinite(thickness) || thickness <= 0.0f) {
        tc_log_error("[termin-gui-native] rejected invalid rounded rectangle stroke command");
        return;
    }
    tc_ui_draw_command command {};
    command.type = TC_UI_DRAW_STROKE_ROUNDED_RECT;
    command.rect = rect;
    command.radius = radius;
    command.color = color;
    command.thickness = thickness;
    append_draw_command(context, command);
}

void tc_ui_painter_fill_circle(
    tc_ui_paint_context* context,
    tc_ui_point center,
    float radius,
    tc_ui_color color,
    int32_t segments
) {
    if (!finite_point(center) || !std::isfinite(radius) || radius <= 0.0f || segments < 0) {
        tc_log_error("[termin-gui-native] rejected invalid circle command");
        return;
    }
    tc_ui_draw_command command {};
    command.type = TC_UI_DRAW_FILL_CIRCLE;
    command.p0 = center;
    command.radius = radius;
    command.color = color;
    command.segments = segments;
    append_draw_command(context, command);
}

void tc_ui_painter_stroke_circle(
    tc_ui_paint_context* context,
    tc_ui_point center,
    float radius,
    tc_ui_color color,
    float thickness,
    int32_t segments
) {
    if (!finite_point(center) || !std::isfinite(radius) || radius <= 0.0f ||
        !std::isfinite(thickness) || thickness <= 0.0f || segments < 0) {
        tc_log_error("[termin-gui-native] rejected invalid circle stroke command");
        return;
    }
    tc_ui_draw_command command {};
    command.type = TC_UI_DRAW_STROKE_CIRCLE;
    command.p0 = center;
    command.radius = radius;
    command.color = color;
    command.thickness = thickness;
    command.segments = segments;
    append_draw_command(context, command);
}

void tc_ui_painter_draw_arc(
    tc_ui_paint_context* context,
    const tc_ui_arc_draw_desc* desc
) {
    if (!desc || !finite_point(desc->center) || !std::isfinite(desc->radius) || desc->radius <= 0.0f ||
        !std::isfinite(desc->start_radians) || !std::isfinite(desc->end_radians) ||
        !std::isfinite(desc->thickness) || desc->thickness <= 0.0f || desc->segments < 0) {
        tc_log_error("[termin-gui-native] rejected invalid arc command");
        return;
    }
    tc_ui_draw_command command {};
    command.type = TC_UI_DRAW_ARC;
    command.p0 = desc->center;
    command.radius = desc->radius;
    command.start_radians = desc->start_radians;
    command.end_radians = desc->end_radians;
    command.color = desc->color;
    command.thickness = desc->thickness;
    command.segments = desc->segments;
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

void tc_ui_painter_draw_polyline(
    tc_ui_paint_context* context,
    const tc_ui_point* points,
    size_t point_count,
    tc_ui_color color,
    float thickness
) {
    if (!context || !context->draw_list) {
        tc_log_error("[termin-gui-native] cannot append polyline without paint context");
        return;
    }
    if (!points || point_count < 2 || !std::isfinite(thickness) || thickness <= 0.0f) {
        tc_log_error("[termin-gui-native] rejected invalid polyline command");
        return;
    }
    for (size_t index = 0; index < point_count; ++index) {
        if (!finite_point(points[index])) {
            tc_log_error("[termin-gui-native] rejected polyline with non-finite point");
            return;
        }
    }
    try {
        auto owned_points = std::make_unique<std::vector<tc_ui_point>>(points, points + point_count);
        const tc_ui_point* stable_points = owned_points->data();
        context->draw_list->point_storage.push_back(std::move(owned_points));
        tc_ui_draw_command command {};
        command.type = TC_UI_DRAW_POLYLINE;
        command.points = stable_points;
        command.point_count = point_count;
        command.color = color;
        command.thickness = thickness;
        append_draw_command(context, command);
    } catch (const std::exception& error) {
        tc_log_error("[termin-gui-native] failed to own polyline points: %s", error.what());
    }
}

void tc_ui_painter_draw_texture(
    tc_ui_paint_context* context,
    uint32_t texture_id,
    tc_ui_rect rect,
    tc_ui_color tint,
    tc_ui_texture_sampling sampling,
    bool flip_v
) {
    if (texture_id == 0 || !finite_rect(rect) ||
        (sampling != TC_UI_TEXTURE_SAMPLING_LINEAR &&
         sampling != TC_UI_TEXTURE_SAMPLING_NEAREST)) {
        tc_log_error("[termin-gui-native] rejected invalid texture command");
        return;
    }
    tc_ui_draw_command command {};
    command.type = TC_UI_DRAW_TEXTURE;
    command.texture_id = texture_id;
    command.rect = rect;
    command.color = tint;
    command.texture_sampling = sampling;
    command.flip_v = flip_v;
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

    try {
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
    } catch (const std::exception& error) {
        tc_log_error("[termin-gui-native] failed to own UI text: %s", error.what());
    }
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
