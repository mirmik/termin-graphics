#include <termin/gui_native/tc_ui_document.h>

#include <cassert>
#include <cstdlib>

struct TestWidget {
    tc_widget widget {};
    int* destroy_count = nullptr;
    int* delete_count = nullptr;
    int* paint_count = nullptr;
};

struct TextMeasureProbe {
    size_t last_length = 0;
    bool return_invalid_metrics = false;
};

static bool probe_text_measure(
    void* user_data,
    const char*,
    size_t byte_length,
    float font_size,
    tc_ui_text_metrics* out_metrics
) {
    auto* probe = static_cast<TextMeasureProbe*>(user_data);
    probe->last_length = byte_length;
    out_metrics->width = probe->return_invalid_metrics ? -1.0f : font_size * static_cast<float>(byte_length);
    out_metrics->height = font_size;
    out_metrics->ascent = font_size * 0.8f;
    out_metrics->descent = font_size * 0.2f;
    out_metrics->line_height = font_size;
    return true;
}

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

static tc_widget_handle adopt(tc_ui_document* document, TestWidget* widget) {
    tc_widget_handle handle = tc_ui_document_adopt_widget(document, &widget->widget);
    assert(!tc_widget_handle_is_invalid(handle));
    assert(widget->widget.document == document);
    assert(tc_widget_handle_eq(widget->widget.handle, handle));
    return handle;
}

static void test_init_defaults_and_common_state() {
    TestWidget borrowed;
    tc_widget_init(&borrowed.widget, &TEST_WIDGET_VTABLE, nullptr, TC_LANGUAGE_CXX, &borrowed);

    assert(borrowed.widget.document == nullptr);
    assert(tc_widget_handle_is_invalid(borrowed.widget.handle));
    assert(tc_widget_parent(&borrowed.widget) == nullptr);
    assert(tc_widget_child_count(&borrowed.widget) == 0);
    assert(tc_widget_child_at(&borrowed.widget, 0) == nullptr);
    assert(tc_widget_is_visible(&borrowed.widget));
    assert(tc_widget_is_enabled(&borrowed.widget));
    assert(!tc_widget_is_focusable(&borrowed.widget));
    assert(!tc_widget_is_mouse_transparent(&borrowed.widget));

    tc_widget_set_bounds(&borrowed.widget, tc_ui_rect {1.0f, 2.0f, 30.0f, 40.0f});
    tc_widget_set_min_size(&borrowed.widget, tc_ui_size {3.0f, 4.0f});
    tc_widget_set_preferred_size(&borrowed.widget, tc_ui_size {5.0f, 6.0f});
    tc_widget_set_max_size(&borrowed.widget, tc_ui_size {70.0f, 80.0f});
    assert(tc_widget_bounds(&borrowed.widget).x == 1.0f);
    assert(tc_widget_bounds(&borrowed.widget).height == 40.0f);
    assert(tc_widget_min_size(&borrowed.widget).width == 3.0f);
    assert(tc_widget_preferred_size(&borrowed.widget).height == 6.0f);
    assert(tc_widget_max_size(&borrowed.widget).width == 70.0f);
    assert(tc_widget_has_dirty_flags(
        &borrowed.widget,
        TC_WIDGET_DIRTY_LAYOUT | TC_WIDGET_DIRTY_PAINT
    ));

    tc_widget_clear_dirty(&borrowed.widget, TC_WIDGET_DIRTY_MASK);
    tc_widget_set_visible(&borrowed.widget, false);
    tc_widget_set_enabled(&borrowed.widget, false);
    tc_widget_set_mouse_transparent(&borrowed.widget, true);
    assert(!tc_widget_is_visible(&borrowed.widget));
    assert(!tc_widget_is_enabled(&borrowed.widget));
    assert(tc_widget_is_mouse_transparent(&borrowed.widget));
    assert(tc_widget_has_dirty_flags(&borrowed.widget, TC_WIDGET_DIRTY_STATE));
}

static void test_borrowed_widget_can_be_adopted_and_released() {
    int destroyed = 0;
    TestWidget borrowed;
    borrowed.destroy_count = &destroyed;
    tc_widget_init(&borrowed.widget, &TEST_WIDGET_VTABLE, nullptr, TC_LANGUAGE_CXX, &borrowed);

    tc_ui_document* document = tc_ui_document_create();
    tc_widget_handle handle = tc_ui_document_adopt_widget(document, &borrowed.widget);
    assert(!tc_widget_handle_is_invalid(handle));
    assert(tc_ui_document_destroy_widget(document, handle));
    assert(destroyed == 1);
    assert(borrowed.widget.document == nullptr);
    assert(tc_widget_handle_is_invalid(borrowed.widget.handle));
    tc_ui_document_destroy(document);
}

static void test_tree_order_reparent_and_root_invariants() {
    tc_ui_document* document = tc_ui_document_create();
    TestWidget* parent_a = make_test_widget();
    TestWidget* parent_b = make_test_widget();
    TestWidget* child_a = make_test_widget();
    TestWidget* child_b = make_test_widget();
    TestWidget* child_c = make_test_widget();
    tc_widget_handle parent_a_handle = adopt(document, parent_a);
    adopt(document, parent_b);
    tc_widget_handle child_a_handle = adopt(document, child_a);
    adopt(document, child_b);
    adopt(document, child_c);

    assert(tc_ui_document_add_root(document, parent_a_handle));
    assert(tc_ui_document_add_root(document, child_a_handle));
    assert(tc_ui_document_root_count(document) == 2);

    assert(tc_widget_append_child(&parent_a->widget, &child_a->widget));
    assert(tc_widget_append_child(&parent_a->widget, &child_b->widget));
    assert(tc_widget_insert_child(&parent_a->widget, 1, &child_c->widget));
    assert(tc_ui_document_root_count(document) == 1);
    assert(tc_widget_child_count(&parent_a->widget) == 3);
    assert(tc_widget_child_at(&parent_a->widget, 0) == &child_a->widget);
    assert(tc_widget_child_at(&parent_a->widget, 1) == &child_c->widget);
    assert(tc_widget_child_at(&parent_a->widget, 2) == &child_b->widget);
    assert(tc_widget_parent(&child_c->widget) == &parent_a->widget);
    assert(!tc_ui_document_add_root(document, child_a_handle));

    assert(tc_widget_insert_child(&parent_a->widget, 0, &child_b->widget));
    assert(tc_widget_child_at(&parent_a->widget, 0) == &child_b->widget);
    assert(tc_widget_child_at(&parent_a->widget, 1) == &child_a->widget);
    assert(tc_widget_child_at(&parent_a->widget, 2) == &child_c->widget);

    tc_widget_clear_dirty(&parent_a->widget, TC_WIDGET_DIRTY_MASK);
    tc_widget_clear_dirty(&parent_b->widget, TC_WIDGET_DIRTY_MASK);
    assert(tc_widget_append_child(&parent_b->widget, &child_a->widget));
    assert(tc_widget_child_count(&parent_a->widget) == 2);
    assert(tc_widget_child_count(&parent_b->widget) == 1);
    assert(tc_widget_parent(&child_a->widget) == &parent_b->widget);
    assert(tc_widget_has_dirty_flags(
        &parent_a->widget,
        TC_WIDGET_DIRTY_LAYOUT | TC_WIDGET_DIRTY_PAINT
    ));
    assert(tc_widget_has_dirty_flags(
        &parent_b->widget,
        TC_WIDGET_DIRTY_LAYOUT | TC_WIDGET_DIRTY_PAINT
    ));
    assert(tc_widget_remove_child(&parent_b->widget, &child_a->widget));
    assert(tc_widget_parent(&child_a->widget) == nullptr);
    assert(!tc_widget_detach(&child_a->widget));

    tc_ui_document_destroy(document);
}

static void test_tree_rejects_self_cycles_and_cross_document_links() {
    tc_ui_document* first_document = tc_ui_document_create();
    tc_ui_document* second_document = tc_ui_document_create();
    TestWidget* parent = make_test_widget();
    TestWidget* child = make_test_widget();
    TestWidget* foreign = make_test_widget();
    adopt(first_document, parent);
    adopt(first_document, child);
    adopt(second_document, foreign);

    assert(!tc_widget_append_child(&parent->widget, &parent->widget));
    assert(tc_widget_append_child(&parent->widget, &child->widget));
    assert(!tc_widget_append_child(&child->widget, &parent->widget));
    assert(!tc_widget_append_child(&parent->widget, &foreign->widget));
    assert(tc_widget_child_count(&parent->widget) == 1);
    assert(tc_widget_parent(&child->widget) == &parent->widget);
    assert(tc_widget_parent(&foreign->widget) == nullptr);

    tc_ui_document_destroy(second_document);
    tc_ui_document_destroy(first_document);
}

static void test_plain_destroy_unlinks_tree_without_destroying_relatives() {
    int destroyed = 0;
    int deleted = 0;
    tc_ui_document* document = tc_ui_document_create();
    TestWidget* parent = make_test_widget(&destroyed, &deleted);
    TestWidget* child_a = make_test_widget(&destroyed, &deleted);
    TestWidget* child_b = make_test_widget(&destroyed, &deleted);
    tc_widget_handle parent_handle = adopt(document, parent);
    tc_widget_handle child_a_handle = adopt(document, child_a);
    tc_widget_handle child_b_handle = adopt(document, child_b);
    assert(tc_widget_append_child(&parent->widget, &child_a->widget));
    assert(tc_widget_append_child(&parent->widget, &child_b->widget));

    assert(tc_ui_document_destroy_widget(document, child_a_handle));
    assert(tc_widget_child_count(&parent->widget) == 1);
    assert(tc_widget_child_at(&parent->widget, 0) == &child_b->widget);
    assert(destroyed == 1 && deleted == 1);

    assert(tc_ui_document_destroy_widget(document, parent_handle));
    assert(tc_ui_document_is_alive(document, child_b_handle));
    assert(tc_widget_parent(&child_b->widget) == nullptr);
    assert(destroyed == 2 && deleted == 2);

    assert(tc_ui_document_destroy_widget(document, child_b_handle));
    assert(destroyed == 3 && deleted == 3);
    tc_ui_document_destroy(document);
}

static void test_recursive_destroy_uses_canonical_tree() {
    int destroyed = 0;
    int deleted = 0;
    tc_ui_document* document = tc_ui_document_create();
    TestWidget* root = make_test_widget(&destroyed, &deleted);
    TestWidget* child = make_test_widget(&destroyed, &deleted);
    TestWidget* grandchild = make_test_widget(&destroyed, &deleted);
    tc_widget_handle root_handle = adopt(document, root);
    tc_widget_handle child_handle = adopt(document, child);
    tc_widget_handle grandchild_handle = adopt(document, grandchild);
    assert(tc_widget_append_child(&root->widget, &child->widget));
    assert(tc_widget_append_child(&child->widget, &grandchild->widget));

    assert(tc_ui_document_destroy_widget_recursive(document, root_handle));
    assert(destroyed == 3 && deleted == 3);
    assert(tc_ui_document_live_widget_count(document) == 0);
    assert(!tc_ui_document_is_alive(document, root_handle));
    assert(!tc_ui_document_is_alive(document, child_handle));
    assert(!tc_ui_document_is_alive(document, grandchild_handle));
    tc_ui_document_destroy(document);
}

static void test_roots_are_explicit_visible_paint_entrypoints() {
    int deleted = 0;
    int painted_a = 0;
    int painted_b = 0;
    tc_ui_document* document = tc_ui_document_create();
    TestWidget* root_a = make_test_widget(nullptr, &deleted, &painted_a);
    TestWidget* root_b = make_test_widget(nullptr, &deleted, &painted_b);
    tc_widget_handle root_a_handle = adopt(document, root_a);
    tc_widget_handle root_b_handle = adopt(document, root_b);
    assert(tc_ui_document_add_root(document, root_a_handle));
    assert(tc_ui_document_add_root(document, root_b_handle));

    tc_widget_set_visible(&root_b->widget, false);
    tc_ui_document_paint_roots(document, nullptr);
    assert(painted_a == 1);
    assert(painted_b == 0);

    assert(tc_ui_document_destroy_widget(document, root_a_handle));
    assert(tc_ui_document_root_count(document) == 1);
    assert(tc_widget_handle_eq(tc_ui_document_root_at(document, 0), root_b_handle));
    tc_ui_document_destroy(document);
    assert(deleted == 2);
}

static void test_document_text_measurement_service_contract() {
    tc_ui_document* document = tc_ui_document_create();
    tc_ui_text_metrics metrics {};
    assert(!tc_ui_document_measure_text(document, "abc", 3, 12.0f, &metrics));

    TextMeasureProbe probe;
    tc_ui_document_set_text_measurer(document, &probe_text_measure, &probe);
    const char bytes[] = {'a', '\0', 'b'};
    assert(tc_ui_document_measure_text(document, bytes, sizeof(bytes), 10.0f, &metrics));
    assert(probe.last_length == sizeof(bytes));
    assert(metrics.width == 30.0f);
    assert(metrics.ascent == 8.0f);

    probe.return_invalid_metrics = true;
    assert(!tc_ui_document_measure_text(document, "x", 1, 10.0f, &metrics));
    assert(metrics.width == 0.0f);

    tc_ui_document_set_text_measurer(document, nullptr, nullptr);
    assert(!tc_ui_document_measure_text(document, "x", 1, 10.0f, &metrics));
    tc_ui_document_destroy(document);
}

int main() {
    test_init_defaults_and_common_state();
    test_borrowed_widget_can_be_adopted_and_released();
    test_tree_order_reparent_and_root_invariants();
    test_tree_rejects_self_cycles_and_cross_document_links();
    test_plain_destroy_unlinks_tree_without_destroying_relatives();
    test_recursive_destroy_uses_canonical_tree();
    test_roots_are_explicit_visible_paint_entrypoints();
    test_document_text_measurement_service_contract();
    return EXIT_SUCCESS;
}
