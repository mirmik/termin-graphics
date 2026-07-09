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

static const tc_widget_vtable TEST_WIDGET_VTABLE {
    "TestWidget",
    nullptr,
    nullptr,
    test_widget_paint,
    nullptr,
    nullptr,
    nullptr,
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

int main() {
    test_adopt_destroy_invalidates_handle();
    test_plain_destroy_does_not_destroy_referenced_widgets();
    test_recursive_destroy_uses_explicit_widget_policy();
    test_roots_are_explicit_paint_entrypoints();
    return EXIT_SUCCESS;
}
