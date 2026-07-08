#include <termin/gui_native/widget.hpp>

#include <cassert>
#include <cstdlib>

using termin::gui_native::Document;
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
    assert(tc_ui_draw_list_command_count(draw_list) == 4);

    const tc_ui_draw_command* clip = tc_ui_draw_list_command_at(draw_list, 0);
    const tc_ui_draw_command* fill = tc_ui_draw_list_command_at(draw_list, 1);
    const tc_ui_draw_command* line = tc_ui_draw_list_command_at(draw_list, 2);
    const tc_ui_draw_command* pop = tc_ui_draw_list_command_at(draw_list, 3);

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
    assert(pop && pop->type == TC_UI_DRAW_POP_CLIP);

    tc_ui_paint_context_destroy(paint_context);
    tc_ui_draw_list_destroy(draw_list);
}

} // namespace

int main() {
    test_widget_paint_builds_draw_list();
    return EXIT_SUCCESS;
}
