#include <termin/gui_native/draw_list_renderer.hpp>
#include <termin/gui_native/widget.hpp>

#include <cassert>
#include <cstdlib>
#include <string>

using termin::gui_native::Document;
using termin::gui_native::UiDrawListRenderer;
using termin::gui_native::Widget;

namespace {

class TestPaintWidget final : public Widget {
public:
    TestPaintWidget() : Widget(&VTABLE, "TestPaintWidget") {}

    int paint_count = 0;

private:
    static void paint(tc_widget* widget, tc_ui_document*, tc_ui_paint_context* context) {
        auto* self = static_cast<TestPaintWidget*>(widget->body);
        self->paint_count += 1;

        tc_ui_painter_push_clip(context, tc_ui_rect {0.0f, 0.0f, 64.0f, 32.0f});
        tc_ui_painter_fill_rect(
            context,
            tc_ui_rect {1.0f, 2.0f, 30.0f, 10.0f},
            tc_ui_color {0.1f, 0.2f, 0.3f, 1.0f}
        );
        tc_ui_painter_draw_line(
            context,
            tc_ui_point {4.0f, 5.0f},
            tc_ui_point {6.0f, 7.0f},
            tc_ui_color {0.8f, 0.7f, 0.6f, 1.0f},
            2.0f
        );
        tc_ui_painter_draw_text(
            context,
            "Hello",
            tc_ui_point {8.0f, 9.0f},
            13.0f,
            tc_ui_color {0.9f, 0.9f, 0.7f, 1.0f}
        );
        tc_ui_painter_pop_clip(context);
    }

    static const tc_widget_vtable VTABLE;
};

const tc_widget_vtable TestPaintWidget::VTABLE {
    "TestPaintWidget",
    nullptr,
    nullptr,
    &TestPaintWidget::paint,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
};

void test_widget_paint_builds_draw_list() {
    Document document;
    tc_ui_draw_list* draw_list = tc_ui_draw_list_create();
    tc_ui_paint_context* paint_context = tc_ui_paint_context_create(draw_list);

    auto* widget = new TestPaintWidget();
    tc_widget_handle handle = document.adopt(widget);
    assert(!tc_widget_handle_is_invalid(handle));
    assert(tc_ui_document_add_root(document.get(), handle));

    tc_ui_document_paint_roots(document.get(), paint_context);

    assert(widget->paint_count == 1);
    assert(tc_ui_draw_list_command_count(draw_list) == 5);

    const tc_ui_draw_command* clip = tc_ui_draw_list_command_at(draw_list, 0);
    const tc_ui_draw_command* fill = tc_ui_draw_list_command_at(draw_list, 1);
    const tc_ui_draw_command* line = tc_ui_draw_list_command_at(draw_list, 2);
    const tc_ui_draw_command* text = tc_ui_draw_list_command_at(draw_list, 3);
    const tc_ui_draw_command* pop = tc_ui_draw_list_command_at(draw_list, 4);

    assert(clip && clip->type == TC_UI_DRAW_PUSH_CLIP);
    assert(clip->rect.width == 64.0f);
    assert(fill && fill->type == TC_UI_DRAW_FILL_RECT);
    assert(fill->rect.x == 1.0f);
    assert(fill->rect.width == 30.0f);
    assert(fill->color.g == 0.2f);
    assert(line && line->type == TC_UI_DRAW_LINE);
    assert(line->p0.x == 4.0f);
    assert(line->p1.y == 7.0f);
    assert(line->thickness == 2.0f);
    assert(text && text->type == TC_UI_DRAW_TEXT);
    assert(text->text && std::string(text->text) == "Hello");
    assert(text->font_size == 13.0f);
    assert(text->p0.x == 8.0f);
    assert(pop && pop->type == TC_UI_DRAW_POP_CLIP);

    tc_ui_draw_list_clear(draw_list);
    assert(tc_ui_draw_list_command_count(draw_list) == 0);

    tc_ui_paint_context_destroy(paint_context);
    tc_ui_draw_list_destroy(draw_list);
}

void test_renderer_font_binds_document_text_measurement() {
    Document document;
    UiDrawListRenderer renderer;
    const std::string font_path = std::string(TERMIN_GUI_NATIVE_SOURCE_DIR) +
        "/../termin-thirdparty/recastnavigation/RecastDemo/Bin/DroidSans.ttf";
    assert(renderer.set_default_font_path(font_path, 14));
    renderer.bind_text_measurer(document.get());

    tc_ui_text_metrics full {};
    tc_ui_text_metrics prefix {};
    assert(document.measure_text("Wi", 2, 18.0f, full));
    assert(document.measure_text("Wi", 1, 18.0f, prefix));
    assert(full.width > prefix.width);
    assert(prefix.width > 0.0f);
    assert(full.ascent > 0.0f);
    assert(full.line_height >= full.ascent);
}

void test_extended_commands_own_variable_data_and_preserve_legacy_values() {
    static_assert(TC_UI_DRAW_FILL_RECT == 0);
    static_assert(TC_UI_DRAW_TEXT == 5);
    tc_ui_draw_list* draw_list = tc_ui_draw_list_create();
    tc_ui_paint_context* context = tc_ui_paint_context_create(draw_list);
    char text[] = "owned text";
    tc_ui_point points[] {
        {1.0f, 2.0f},
        {3.0f, 4.0f},
        {5.0f, 6.0f},
    };
    const tc_ui_color white {1.0f, 1.0f, 1.0f, 1.0f};

    tc_ui_painter_fill_rounded_rect(
        context,
        tc_ui_rect {1.0f, 2.0f, 30.0f, 20.0f},
        4.0f,
        white
    );
    tc_ui_painter_stroke_rounded_rect(
        context,
        tc_ui_rect {2.0f, 3.0f, 28.0f, 18.0f},
        3.0f,
        white,
        2.0f
    );
    tc_ui_painter_fill_circle(context, tc_ui_point {10.0f, 11.0f}, 5.0f, white, 20);
    tc_ui_painter_stroke_circle(context, tc_ui_point {12.0f, 13.0f}, 6.0f, white, 1.5f, 24);
    const tc_ui_arc_draw_desc arc{
        tc_ui_point {14.0f, 15.0f}, 7.0f, 0.25f, 2.5f, white, 2.0f, 12};
    tc_ui_painter_draw_arc(context, &arc);
    tc_ui_painter_draw_polyline(context, points, 3, white, 3.0f);
    tc_ui_painter_draw_texture(
        context,
        42,
        tc_ui_rect {16.0f, 17.0f, 8.0f, 9.0f},
        tc_ui_color {0.5f, 0.6f, 0.7f, 0.8f},
        true
    );
    tc_ui_painter_draw_text(context, text, tc_ui_point {18.0f, 19.0f}, 14.0f, white);

    text[0] = 'X';
    points[1] = tc_ui_point {99.0f, 100.0f};
    assert(tc_ui_draw_list_command_count(draw_list) == 8);
    const tc_ui_draw_command* rounded = tc_ui_draw_list_command_at(draw_list, 0);
    const tc_ui_draw_command* polyline = tc_ui_draw_list_command_at(draw_list, 5);
    const tc_ui_draw_command* texture = tc_ui_draw_list_command_at(draw_list, 6);
    const tc_ui_draw_command* owned_text = tc_ui_draw_list_command_at(draw_list, 7);
    assert(rounded && rounded->type == TC_UI_DRAW_FILL_ROUNDED_RECT);
    assert(rounded->radius == 4.0f);
    assert(polyline && polyline->type == TC_UI_DRAW_POLYLINE);
    assert(polyline->point_count == 3);
    assert(polyline->points && polyline->points[1].x == 3.0f);
    assert(texture && texture->type == TC_UI_DRAW_TEXTURE);
    assert(texture->texture_id == 42 && texture->flip_v);
    assert(owned_text && owned_text->type == TC_UI_DRAW_TEXT);
    assert(std::string(owned_text->text) == "owned text");

    tc_ui_draw_list_clear(draw_list);
    assert(tc_ui_draw_list_command_count(draw_list) == 0);
    tc_ui_paint_context_destroy(context);
    tc_ui_draw_list_destroy(draw_list);
}

} // namespace

int main() {
    test_widget_paint_builds_draw_list();
    test_renderer_font_binds_document_text_measurement();
    test_extended_commands_own_variable_data_and_preserve_legacy_values();
    return EXIT_SUCCESS;
}
