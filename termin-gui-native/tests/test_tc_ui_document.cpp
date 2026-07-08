#include <termin/gui_native/tc_ui_document.h>

#include <cassert>
#include <cstdlib>
#include <vector>

struct TestWidget {
    tc_widget widget {};
    std::vector<tc_widget_handle> recursive_destroy_targets;
    int* destroy_count = nullptr;
    int* delete_count = nullptr;
    int* paint_count = nullptr;
    bool emit_draw_commands = false;
};

static TestWidget* from_widget(tc_widget* widget) {
    return static_cast<TestWidget*>(widget->body);
}

static void test_widget_delete(tc_widget* widget) {
    TestWidget* self = from_widget(widget);
    if (self->delete_count) {
        *self->delete_count += 1;
    }
    delete self;
}

static void test_widget_on_destroy(tc_widget* widget, tc_ui_document*) {
    TestWidget* self = from_widget(widget);
    if (self->destroy_count) {
        *self->destroy_count += 1;
    }
}

static void test_widget_visit_recursive_targets(
    tc_widget* widget,
    tc_ui_document*,
    void* user_data,
    tc_widget_visit_fn visit
) {
    TestWidget* self = from_widget(widget);
    for (tc_widget_handle target : self->recursive_destroy_targets) {
        visit(user_data, target);
    }
}

static void test_widget_paint(tc_widget* widget, tc_ui_document*, tc_ui_paint_context*) {
    TestWidget* self = from_widget(widget);
    if (self->paint_count) {
        *self->paint_count += 1;
    }
}

static void draw_widget_paint(tc_widget* widget, tc_ui_document*, tc_ui_paint_context* context) {
    TestWidget* self = from_widget(widget);
    if (self->paint_count) {
        *self->paint_count += 1;
    }
    if (!self->emit_draw_commands) {
        return;
    }

    tc_ui_painter_push_clip(context, tc_ui_rect {0.0f, 0.0f, 128.0f, 64.0f});
    tc_ui_painter_fill_rect(
        context,
        tc_ui_rect {1.0f, 2.0f, 3.0f, 4.0f},
        tc_ui_color {0.1f, 0.2f, 0.3f, 1.0f}
    );
    tc_ui_painter_stroke_rect(
        context,
        tc_ui_rect {5.0f, 6.0f, 7.0f, 8.0f},
        tc_ui_color {0.4f, 0.5f, 0.6f, 1.0f},
        2.0f
    );
    tc_ui_painter_draw_line(
        context,
        tc_ui_point {9.0f, 10.0f},
        tc_ui_point {11.0f, 12.0f},
        tc_ui_color {0.7f, 0.8f, 0.9f, 1.0f},
        3.0f
    );
    tc_ui_painter_pop_clip(context);
}

static const tc_widget_vtable TEST_WIDGET_VTABLE {
    "TestWidget",
    nullptr,
    nullptr,
    test_widget_paint,
    nullptr,
    test_widget_visit_recursive_targets,
    test_widget_on_destroy,
};

static const tc_widget_vtable DRAW_WIDGET_VTABLE {
    "DrawWidget",
    nullptr,
    nullptr,
    draw_widget_paint,
    nullptr,
    test_widget_visit_recursive_targets,
    test_widget_on_destroy,
};

static TestWidget* make_test_widget(
    int* destroy_count = nullptr,
    int* delete_count = nullptr,
    int* paint_count = nullptr
) {
    auto* widget = new TestWidget();
    widget->destroy_count = destroy_count;
    widget->delete_count = delete_count;
    widget->paint_count = paint_count;
    tc_widget_init(
        &widget->widget,
        &TEST_WIDGET_VTABLE,
        test_widget_delete,
        TC_LANGUAGE_CXX,
        widget
    );
    return widget;
}

static TestWidget* make_draw_widget(int* paint_count = nullptr) {
    auto* widget = new TestWidget();
    widget->paint_count = paint_count;
    widget->emit_draw_commands = true;
    tc_widget_init(
        &widget->widget,
        &DRAW_WIDGET_VTABLE,
        test_widget_delete,
        TC_LANGUAGE_CXX,
        widget
    );
    return widget;
}

static void test_adopt_destroy_invalidates_handle() {
    int destroyed = 0;
    int deleted = 0;
    tc_ui_document* document = tc_ui_document_create();

    TestWidget* widget = make_test_widget(&destroyed, &deleted);
    tc_widget_handle handle = tc_ui_document_adopt_widget(document, &widget->widget);

    assert(!tc_widget_handle_is_invalid(handle));
    assert(tc_ui_document_live_widget_count(document) == 1);
    assert(tc_ui_document_is_alive(document, handle));
    assert(tc_ui_document_resolve_widget(document, handle) != nullptr);

    assert(tc_ui_document_destroy_widget(document, handle));
    assert(destroyed == 1);
    assert(deleted == 1);
    assert(tc_ui_document_live_widget_count(document) == 0);
    assert(!tc_ui_document_is_alive(document, handle));
    assert(tc_ui_document_resolve_widget(document, handle) == nullptr);

    tc_ui_document_destroy(document);
}

static void test_plain_destroy_does_not_destroy_referenced_widgets() {
    int destroyed = 0;
    int deleted = 0;
    tc_ui_document* document = tc_ui_document_create();

    TestWidget* parent = make_test_widget(&destroyed, &deleted);
    TestWidget* child = make_test_widget(&destroyed, &deleted);
    tc_widget_handle parent_handle = tc_ui_document_adopt_widget(document, &parent->widget);
    tc_widget_handle child_handle = tc_ui_document_adopt_widget(document, &child->widget);
    parent->recursive_destroy_targets.push_back(child_handle);

    assert(tc_ui_document_destroy_widget(document, parent_handle));
    assert(destroyed == 1);
    assert(deleted == 1);
    assert(tc_ui_document_live_widget_count(document) == 1);
    assert(tc_ui_document_is_alive(document, child_handle));

    assert(tc_ui_document_destroy_widget(document, child_handle));
    assert(destroyed == 2);
    assert(deleted == 2);

    tc_ui_document_destroy(document);
}

static void test_recursive_destroy_uses_explicit_widget_policy() {
    int destroyed = 0;
    int deleted = 0;
    tc_ui_document* document = tc_ui_document_create();

    TestWidget* parent = make_test_widget(&destroyed, &deleted);
    TestWidget* child_a = make_test_widget(&destroyed, &deleted);
    TestWidget* child_b = make_test_widget(&destroyed, &deleted);

    tc_widget_handle parent_handle = tc_ui_document_adopt_widget(document, &parent->widget);
    tc_widget_handle child_a_handle = tc_ui_document_adopt_widget(document, &child_a->widget);
    tc_widget_handle child_b_handle = tc_ui_document_adopt_widget(document, &child_b->widget);

    parent->recursive_destroy_targets.push_back(child_a_handle);
    parent->recursive_destroy_targets.push_back(child_b_handle);

    assert(tc_ui_document_destroy_widget_recursive(document, parent_handle));
    assert(destroyed == 3);
    assert(deleted == 3);
    assert(tc_ui_document_live_widget_count(document) == 0);
    assert(!tc_ui_document_is_alive(document, parent_handle));
    assert(!tc_ui_document_is_alive(document, child_a_handle));
    assert(!tc_ui_document_is_alive(document, child_b_handle));

    tc_ui_document_destroy(document);
}

static void test_roots_are_explicit_paint_entrypoints() {
    int deleted = 0;
    int painted_a = 0;
    int painted_b = 0;
    tc_ui_document* document = tc_ui_document_create();

    TestWidget* root_a = make_test_widget(nullptr, &deleted, &painted_a);
    TestWidget* root_b = make_test_widget(nullptr, &deleted, &painted_b);
    tc_widget_handle root_a_handle = tc_ui_document_adopt_widget(document, &root_a->widget);
    tc_widget_handle root_b_handle = tc_ui_document_adopt_widget(document, &root_b->widget);

    assert(tc_ui_document_add_root(document, root_a_handle));
    assert(tc_ui_document_add_root(document, root_b_handle));
    assert(tc_ui_document_root_count(document) == 2);

    tc_ui_document_paint_roots(document, nullptr);
    assert(painted_a == 1);
    assert(painted_b == 1);

    assert(tc_ui_document_destroy_widget(document, root_a_handle));
    assert(tc_ui_document_root_count(document) == 1);
    assert(tc_widget_handle_eq(tc_ui_document_root_at(document, 0), root_b_handle));

    tc_ui_document_destroy(document);
    assert(deleted == 2);
}

static void test_paint_context_collects_draw_commands() {
    int painted = 0;
    tc_ui_document* document = tc_ui_document_create();
    tc_ui_draw_list* draw_list = tc_ui_draw_list_create();
    tc_ui_paint_context* paint_context = tc_ui_paint_context_create(draw_list);

    TestWidget* root = make_draw_widget(&painted);
    tc_widget_handle root_handle = tc_ui_document_adopt_widget(document, &root->widget);
    assert(tc_ui_document_add_root(document, root_handle));

    tc_ui_document_paint_roots(document, paint_context);
    assert(painted == 1);
    assert(tc_ui_draw_list_command_count(draw_list) == 5);

    const tc_ui_draw_command* push_clip = tc_ui_draw_list_command_at(draw_list, 0);
    const tc_ui_draw_command* fill_rect = tc_ui_draw_list_command_at(draw_list, 1);
    const tc_ui_draw_command* stroke_rect = tc_ui_draw_list_command_at(draw_list, 2);
    const tc_ui_draw_command* line = tc_ui_draw_list_command_at(draw_list, 3);
    const tc_ui_draw_command* pop_clip = tc_ui_draw_list_command_at(draw_list, 4);

    assert(push_clip && push_clip->type == TC_UI_DRAW_PUSH_CLIP);
    assert(push_clip->rect.width == 128.0f);
    assert(fill_rect && fill_rect->type == TC_UI_DRAW_FILL_RECT);
    assert(fill_rect->rect.x == 1.0f);
    assert(fill_rect->color.g == 0.2f);
    assert(stroke_rect && stroke_rect->type == TC_UI_DRAW_STROKE_RECT);
    assert(stroke_rect->thickness == 2.0f);
    assert(line && line->type == TC_UI_DRAW_LINE);
    assert(line->p0.x == 9.0f);
    assert(line->p1.y == 12.0f);
    assert(line->thickness == 3.0f);
    assert(pop_clip && pop_clip->type == TC_UI_DRAW_POP_CLIP);

    tc_ui_draw_list_clear(draw_list);
    assert(tc_ui_draw_list_command_count(draw_list) == 0);

    tc_ui_paint_context_destroy(paint_context);
    tc_ui_draw_list_destroy(draw_list);
    tc_ui_document_destroy(document);
}

int main() {
    test_adopt_destroy_invalidates_handle();
    test_plain_destroy_does_not_destroy_referenced_widgets();
    test_recursive_destroy_uses_explicit_widget_policy();
    test_roots_are_explicit_paint_entrypoints();
    test_paint_context_collects_draw_commands();
    return EXIT_SUCCESS;
}
