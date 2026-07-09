#include <termin/gui_native/tc_ui_document.h>

#include <cassert>
#include <cstdlib>
#include <vector>

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

struct RouteWidget {
    tc_widget widget {};
    int id = 0;
    std::vector<int>* pointer_log = nullptr;
    std::vector<int>* key_log = nullptr;
    std::vector<int>* focus_log = nullptr;
    std::vector<int>* paint_log = nullptr;
    std::vector<int>* dismiss_log = nullptr;
    tc_widget_handle hit_target = tc_widget_handle_invalid();
    tc_widget_handle destroy_target = tc_widget_handle_invalid();
    tc_widget_handle destroy_on_leave = tc_widget_handle_invalid();
    tc_widget_handle destroy_on_focus_loss = tc_widget_handle_invalid();
    tc_ui_pointer_event_type handled_pointer = static_cast<tc_ui_pointer_event_type>(-1);
    bool destroy_recursive = false;
    bool handle_key = false;
};

static RouteWidget* route_widget_from(tc_widget* widget) {
    return static_cast<RouteWidget*>(widget->body);
}

static tc_widget_handle route_widget_hit_test(
    tc_widget* widget,
    tc_ui_document*,
    float x,
    float
) {
    RouteWidget* self = route_widget_from(widget);
    if (x >= 50.0f) {
        return tc_widget_handle_invalid();
    }
    return tc_widget_handle_is_invalid(self->hit_target) ? widget->handle : self->hit_target;
}

static tc_ui_event_result route_widget_pointer_event(
    tc_widget* widget,
    tc_ui_document* document,
    const tc_ui_pointer_event* event
) {
    RouteWidget* self = route_widget_from(widget);
    if (self->pointer_log) {
        self->pointer_log->push_back(self->id * 10 + static_cast<int>(event->type));
    }
    if (event->type == TC_UI_POINTER_DOWN &&
        !tc_widget_handle_is_invalid(self->destroy_target)) {
        if (self->destroy_recursive) {
            tc_ui_document_destroy_widget_recursive(document, self->destroy_target);
        } else {
            tc_ui_document_destroy_widget(document, self->destroy_target);
        }
    }
    if (event->type == TC_UI_POINTER_LEAVE &&
        !tc_widget_handle_is_invalid(self->destroy_on_leave)) {
        tc_ui_document_destroy_widget(document, self->destroy_on_leave);
    }
    return event->type == self->handled_pointer ? TC_UI_EVENT_HANDLED : TC_UI_EVENT_IGNORED;
}

static void route_widget_paint(tc_widget* widget, tc_ui_document*, tc_ui_paint_context*) {
    RouteWidget* self = route_widget_from(widget);
    if (self->paint_log) {
        self->paint_log->push_back(self->id);
    }
}

static tc_ui_event_result route_widget_key_event(
    tc_widget* widget,
    tc_ui_document*,
    const tc_ui_key_event*
) {
    RouteWidget* self = route_widget_from(widget);
    if (self->key_log) {
        self->key_log->push_back(self->id);
    }
    return self->handle_key ? TC_UI_EVENT_HANDLED : TC_UI_EVENT_IGNORED;
}

static void route_widget_focus_event(tc_widget* widget, tc_ui_document* document, bool focused) {
    RouteWidget* self = route_widget_from(widget);
    if (self->focus_log) {
        self->focus_log->push_back(self->id * 10 + (focused ? 1 : 0));
    }
    if (!focused && !tc_widget_handle_is_invalid(self->destroy_on_focus_loss)) {
        tc_ui_document_destroy_widget(document, self->destroy_on_focus_loss);
    }
}

static void route_widget_overlay_dismissed(
    tc_widget* widget,
    tc_ui_document*,
    tc_ui_overlay_dismiss_reason reason
) {
    RouteWidget* self = route_widget_from(widget);
    if (self->dismiss_log) {
        self->dismiss_log->push_back(self->id * 10 + static_cast<int>(reason));
    }
}

static const tc_widget_vtable ROUTE_WIDGET_VTABLE {
    "RouteWidget",
    nullptr,
    nullptr,
    route_widget_paint,
    route_widget_pointer_event,
    route_widget_hit_test,
    route_widget_key_event,
    nullptr,
    route_widget_focus_event,
    route_widget_overlay_dismissed,
    nullptr,
};

static tc_widget_handle adopt_route_widget(
    tc_ui_document* document,
    RouteWidget& widget,
    int id
) {
    widget.id = id;
    tc_widget_init(
        &widget.widget,
        &ROUTE_WIDGET_VTABLE,
        nullptr,
        TC_LANGUAGE_CXX,
        &widget
    );
    return tc_ui_document_adopt_widget(document, &widget.widget);
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

static void test_pointer_routing_hover_pressed_and_bubbling() {
    tc_ui_document* document = tc_ui_document_create();
    RouteWidget root;
    RouteWidget child;
    std::vector<int> log;
    tc_widget_handle root_handle = adopt_route_widget(document, root, 1);
    tc_widget_handle child_handle = adopt_route_widget(document, child, 2);
    root.pointer_log = &log;
    child.pointer_log = &log;
    root.hit_target = child_handle;
    root.handled_pointer = TC_UI_POINTER_DOWN;
    assert(tc_widget_append_child(&root.widget, &child.widget));
    assert(tc_ui_document_add_root(document, root_handle));

    tc_ui_pointer_event event {};
    event.type = TC_UI_POINTER_MOVE;
    event.x = 10.0f;
    assert(tc_ui_document_dispatch_pointer_event(document, &event) == TC_UI_EVENT_IGNORED);
    assert((log == std::vector<int> {24, 20, 10}));
    assert(tc_widget_handle_eq(tc_ui_document_hovered_widget(document), child_handle));

    log.clear();
    event.type = TC_UI_POINTER_DOWN;
    assert(tc_ui_document_dispatch_pointer_event(document, &event) == TC_UI_EVENT_HANDLED);
    assert((log == std::vector<int> {21, 11}));
    assert(tc_widget_handle_eq(tc_ui_document_pressed_widget(document), root_handle));

    log.clear();
    event.type = TC_UI_POINTER_MOVE;
    event.x = 100.0f;
    assert(tc_ui_document_dispatch_pointer_event(document, &event) == TC_UI_EVENT_IGNORED);
    assert((log == std::vector<int> {25, 10}));
    assert(tc_widget_handle_is_invalid(tc_ui_document_hovered_widget(document)));

    log.clear();
    event.type = TC_UI_POINTER_UP;
    assert(tc_ui_document_dispatch_pointer_event(document, &event) == TC_UI_EVENT_IGNORED);
    assert((log == std::vector<int> {12}));
    assert(tc_widget_handle_is_invalid(tc_ui_document_pressed_widget(document)));
    tc_ui_document_destroy(document);
}

static void test_routing_snapshot_survives_destroyed_target() {
    tc_ui_document* document = tc_ui_document_create();
    RouteWidget root;
    RouteWidget child;
    std::vector<int> log;
    tc_widget_handle root_handle = adopt_route_widget(document, root, 1);
    tc_widget_handle child_handle = adopt_route_widget(document, child, 2);
    root.pointer_log = &log;
    child.pointer_log = &log;
    root.hit_target = child_handle;
    root.handled_pointer = TC_UI_POINTER_DOWN;
    child.destroy_target = child_handle;
    assert(tc_widget_append_child(&root.widget, &child.widget));
    assert(tc_ui_document_add_root(document, root_handle));

    tc_ui_pointer_event event {};
    event.type = TC_UI_POINTER_DOWN;
    event.x = 10.0f;
    assert(tc_ui_document_dispatch_pointer_event(document, &event) == TC_UI_EVENT_HANDLED);
    assert((log == std::vector<int> {24, 21, 11}));
    assert(!tc_ui_document_is_alive(document, child_handle));
    assert(tc_ui_document_is_alive(document, root_handle));
    assert(tc_widget_handle_eq(tc_ui_document_pressed_widget(document), root_handle));
    tc_ui_document_destroy(document);
}

static void test_keyboard_bubbling_focus_events_and_tab_traversal() {
    tc_ui_document* document = tc_ui_document_create();
    RouteWidget root;
    RouteWidget first;
    RouteWidget skipped;
    RouteWidget third;
    std::vector<int> key_log;
    std::vector<int> focus_log;
    tc_widget_handle root_handle = adopt_route_widget(document, root, 1);
    tc_widget_handle first_handle = adopt_route_widget(document, first, 2);
    tc_widget_handle skipped_handle = adopt_route_widget(document, skipped, 3);
    tc_widget_handle third_handle = adopt_route_widget(document, third, 4);
    root.key_log = &key_log;
    first.key_log = &key_log;
    third.key_log = &key_log;
    first.focus_log = &focus_log;
    skipped.focus_log = &focus_log;
    third.focus_log = &focus_log;
    root.handle_key = true;
    tc_widget_set_focusable(&first.widget, true);
    tc_widget_set_focusable(&skipped.widget, true);
    tc_widget_set_focusable(&third.widget, true);
    tc_widget_set_enabled(&skipped.widget, false);
    assert(tc_widget_append_child(&root.widget, &first.widget));
    assert(tc_widget_append_child(&root.widget, &skipped.widget));
    assert(tc_widget_append_child(&root.widget, &third.widget));
    assert(tc_ui_document_add_root(document, root_handle));

    assert(tc_ui_document_focus_next(document));
    assert(tc_widget_handle_eq(tc_ui_document_focused_widget(document), first_handle));
    assert((focus_log == std::vector<int> {21}));
    assert(tc_ui_document_focus_next(document));
    assert(tc_widget_handle_eq(tc_ui_document_focused_widget(document), third_handle));
    assert((focus_log == std::vector<int> {21, 20, 41}));
    assert(tc_ui_document_focus_previous(document));
    assert(tc_widget_handle_eq(tc_ui_document_focused_widget(document), first_handle));

    tc_ui_key_event event {};
    event.type = TC_UI_KEY_DOWN;
    event.key = TC_UI_KEY_ENTER;
    assert(tc_ui_document_dispatch_key_event(document, &event) == TC_UI_EVENT_HANDLED);
    assert((key_log == std::vector<int> {2, 1}));

    root.handle_key = false;
    key_log.clear();
    event.key = TC_UI_KEY_TAB;
    event.modifiers = TC_UI_MOD_SHIFT;
    assert(tc_ui_document_dispatch_key_event(document, &event) == TC_UI_EVENT_HANDLED);
    assert((key_log == std::vector<int> {2, 1}));
    assert(tc_widget_handle_eq(tc_ui_document_focused_widget(document), third_handle));

    assert(tc_ui_document_destroy_widget(document, third_handle));
    assert(focus_log.back() == 40);
    assert(tc_widget_handle_is_invalid(tc_ui_document_focused_widget(document)));
    (void)skipped_handle;
    tc_ui_document_destroy(document);
}

static void test_state_setters_survive_lifecycle_callback_destroy() {
    {
        tc_ui_document* document = tc_ui_document_create();
        RouteWidget hovered;
        tc_widget_handle handle = adopt_route_widget(document, hovered, 1);
        hovered.destroy_on_leave = handle;
        assert(tc_ui_document_add_root(document, handle));
        tc_ui_pointer_event event {};
        event.type = TC_UI_POINTER_MOVE;
        event.x = 10.0f;
        assert(tc_ui_document_dispatch_pointer_event(document, &event) == TC_UI_EVENT_IGNORED);
        tc_widget_set_visible(&hovered.widget, false);
        assert(!tc_ui_document_is_alive(document, handle));
        assert(tc_widget_handle_is_invalid(tc_ui_document_hovered_widget(document)));
        tc_ui_document_destroy(document);
    }
    {
        tc_ui_document* document = tc_ui_document_create();
        RouteWidget focused;
        tc_widget_handle handle = adopt_route_widget(document, focused, 1);
        tc_widget_set_focusable(&focused.widget, true);
        focused.destroy_on_focus_loss = handle;
        assert(tc_ui_document_add_root(document, handle));
        assert(tc_ui_document_set_focus(document, handle));
        tc_widget_set_enabled(&focused.widget, false);
        assert(!tc_ui_document_is_alive(document, handle));
        assert(tc_widget_handle_is_invalid(tc_ui_document_focused_widget(document)));
        tc_ui_document_destroy(document);
    }
}

static void test_overlay_paint_hit_order_and_tooltip_transparency() {
    tc_ui_document* document = tc_ui_document_create();
    RouteWidget root;
    RouteWidget popup;
    RouteWidget tooltip;
    std::vector<int> paint_log;
    tc_widget_handle root_handle = adopt_route_widget(document, root, 1);
    tc_widget_handle popup_handle = adopt_route_widget(document, popup, 2);
    tc_widget_handle tooltip_handle = adopt_route_widget(document, tooltip, 3);
    root.paint_log = &paint_log;
    popup.paint_log = &paint_log;
    tooltip.paint_log = &paint_log;
    assert(tc_ui_document_add_root(document, root_handle));
    assert(tc_ui_document_show_overlay(document, popup_handle, 0));
    assert(tc_ui_document_show_overlay(document, tooltip_handle, TC_UI_OVERLAY_TOOLTIP));
    assert(tc_ui_document_overlay_count(document) == 2);
    assert(tc_widget_handle_eq(tc_ui_document_overlay_at(document, 0), popup_handle));
    assert((tc_ui_document_overlay_flags_at(document, 1) &
            TC_UI_OVERLAY_POINTER_TRANSPARENT) != 0);

    tc_ui_document_paint(document, nullptr);
    assert((paint_log == std::vector<int> {1, 2, 3}));
    assert(tc_widget_handle_eq(tc_ui_document_hit_test(document, 10.0f, 10.0f), popup_handle));
    assert(!tc_ui_document_add_root(document, popup_handle));
    assert(!tc_widget_append_child(&root.widget, &popup.widget));

    assert(tc_ui_document_destroy_widget(document, tooltip_handle));
    assert(tc_ui_document_overlay_count(document) == 1);
    assert(tc_ui_document_destroy_widget(document, popup_handle));
    assert(tc_ui_document_overlay_count(document) == 0);
    tc_ui_document_destroy(document);
}

static void test_overlay_outside_escape_and_programmatic_dismissal() {
    tc_ui_document* document = tc_ui_document_create();
    RouteWidget root;
    RouteWidget popup;
    std::vector<int> pointer_log;
    std::vector<int> dismiss_log;
    tc_widget_handle root_handle = adopt_route_widget(document, root, 1);
    tc_widget_handle popup_handle = adopt_route_widget(document, popup, 2);
    root.pointer_log = &pointer_log;
    popup.dismiss_log = &dismiss_log;
    assert(tc_ui_document_add_root(document, root_handle));
    assert(tc_ui_document_show_overlay(
        document,
        popup_handle,
        TC_UI_OVERLAY_DISMISS_ON_OUTSIDE
    ));

    tc_ui_pointer_event pointer {};
    pointer.type = TC_UI_POINTER_DOWN;
    pointer.x = 100.0f;
    assert(tc_ui_document_dispatch_pointer_event(document, &pointer) == TC_UI_EVENT_HANDLED);
    assert(pointer_log.empty());
    assert((dismiss_log == std::vector<int> {21}));
    assert(tc_ui_document_overlay_count(document) == 0);

    assert(tc_ui_document_show_overlay(document, popup_handle, 0));
    tc_ui_key_event key {};
    key.type = TC_UI_KEY_DOWN;
    key.key = TC_UI_KEY_ESCAPE;
    assert(tc_ui_document_dispatch_key_event(document, &key) == TC_UI_EVENT_HANDLED);
    assert((dismiss_log == std::vector<int> {21, 22}));

    assert(tc_ui_document_show_overlay(document, popup_handle, 0));
    assert(tc_ui_document_dismiss_overlay(
        document,
        popup_handle,
        TC_UI_OVERLAY_DISMISS_PROGRAMMATIC
    ));
    assert((dismiss_log == std::vector<int> {21, 22, 20}));
    tc_ui_document_destroy(document);
}

static void test_modal_overlay_blocks_lower_input_and_scopes_focus() {
    tc_ui_document* document = tc_ui_document_create();
    RouteWidget root;
    RouteWidget modal;
    RouteWidget modal_focus;
    std::vector<int> pointer_log;
    std::vector<int> key_log;
    tc_widget_handle root_handle = adopt_route_widget(document, root, 1);
    tc_widget_handle modal_handle = adopt_route_widget(document, modal, 2);
    tc_widget_handle modal_focus_handle = adopt_route_widget(document, modal_focus, 3);
    root.pointer_log = &pointer_log;
    root.handled_pointer = TC_UI_POINTER_DOWN;
    root.key_log = &key_log;
    modal.key_log = &key_log;
    modal.handle_key = true;
    tc_widget_set_focusable(&root.widget, true);
    tc_widget_set_focusable(&modal_focus.widget, true);
    assert(tc_widget_append_child(&modal.widget, &modal_focus.widget));
    assert(tc_ui_document_add_root(document, root_handle));
    assert(tc_ui_document_set_focus(document, root_handle));
    assert(tc_ui_document_show_overlay(document, modal_handle, TC_UI_OVERLAY_MODAL));
    assert(tc_widget_handle_is_invalid(tc_ui_document_focused_widget(document)));

    tc_ui_pointer_event pointer {};
    pointer.type = TC_UI_POINTER_DOWN;
    pointer.x = 100.0f;
    assert(tc_ui_document_dispatch_pointer_event(document, &pointer) == TC_UI_EVENT_HANDLED);
    assert(pointer_log.empty());

    tc_ui_key_event key {};
    key.type = TC_UI_KEY_DOWN;
    key.key = TC_UI_KEY_ENTER;
    assert(tc_ui_document_dispatch_key_event(document, &key) == TC_UI_EVENT_HANDLED);
    assert((key_log == std::vector<int> {2}));
    assert(tc_ui_document_focus_next(document));
    assert(tc_widget_handle_eq(tc_ui_document_focused_widget(document), modal_focus_handle));
    assert(tc_ui_document_focus_next(document));
    assert(tc_widget_handle_eq(tc_ui_document_focused_widget(document), modal_focus_handle));
    tc_ui_document_destroy(document);
}

static void test_tooltip_rect_is_host_driven_and_clamped() {
    tc_ui_rect rect = tc_ui_tooltip_rect(
        tc_ui_rect {0.0f, 0.0f, 100.0f, 80.0f},
        tc_ui_point {95.0f, 75.0f},
        tc_ui_size {30.0f, 20.0f},
        tc_ui_point {12.0f, 18.0f},
        4.0f
    );
    assert(rect.x == 66.0f);
    assert(rect.y == 56.0f);
    assert(rect.width == 30.0f);
    assert(rect.height == 20.0f);
    rect = tc_ui_tooltip_rect(
        tc_ui_rect {10.0f, 20.0f, 20.0f, 10.0f},
        tc_ui_point {0.0f, 0.0f},
        tc_ui_size {100.0f, 100.0f},
        tc_ui_point {0.0f, 0.0f},
        3.0f
    );
    assert(rect.x == 13.0f && rect.y == 23.0f);
    assert(rect.width == 14.0f && rect.height == 4.0f);
}

static void test_theme_style_resolution_inheritance_and_invalidation() {
    tc_ui_document* document = tc_ui_document_create();
    RouteWidget parent_a;
    RouteWidget parent_b;
    RouteWidget child;
    tc_ui_style style {};
    tc_ui_style_override inherited {};
    tc_ui_style_override local {};
    tc_ui_theme theme = *tc_ui_document_theme(document);
    const uint64_t initial_revision = tc_ui_document_theme_revision(document);
    const tc_widget_handle parent_a_handle = adopt_route_widget(document, parent_a, 1);
    const tc_widget_handle parent_b_handle = adopt_route_widget(document, parent_b, 2);
    const tc_widget_handle child_handle = adopt_route_widget(document, child, 3);
    (void)parent_b_handle;

    tc_widget_set_style_role(&child.widget, TC_UI_STYLE_BUTTON);
    assert(tc_widget_append_child(&parent_a.widget, &child.widget));
    assert(tc_ui_document_add_root(document, parent_a_handle));
    assert(tc_ui_document_resolve_style(document, &child.widget, 0, &style));
    assert(style.background.r == 0.20f);
    assert(style.font_size == 14.0f);

    inherited.fields = TC_UI_STYLE_FONT_SIZE | TC_UI_STYLE_FOREGROUND;
    inherited.flags = TC_UI_STYLE_OVERRIDE_INHERIT;
    inherited.value.font_size = 19.0f;
    inherited.value.foreground = tc_ui_color {1.0f, 0.5f, 0.25f, 1.0f};
    assert(tc_widget_set_style_override(&parent_a.widget, &inherited));
    assert(tc_ui_document_resolve_style(document, &child.widget, 0, &style));
    assert(style.font_size == 19.0f);
    assert(style.foreground.g == 0.5f);

    local.fields = TC_UI_STYLE_FONT_SIZE;
    local.value.font_size = 23.0f;
    assert(tc_widget_set_style_override(&child.widget, &local));
    assert(tc_ui_document_resolve_style(document, &child.widget, 0, &style));
    assert(style.font_size == 23.0f);
    assert(style.foreground.g == 0.5f);

    tc_widget_clear_dirty(&child.widget, TC_WIDGET_DIRTY_MASK);
    assert(tc_widget_append_child(&parent_b.widget, &child.widget));
    assert(tc_widget_has_dirty_flags(
        &child.widget,
        TC_WIDGET_DIRTY_LAYOUT | TC_WIDGET_DIRTY_PAINT | TC_WIDGET_DIRTY_STATE
    ));
    assert(tc_ui_document_resolve_style(document, &child.widget, 0, &style));
    assert(style.font_size == 23.0f);
    assert(style.foreground.r == theme.roles[TC_UI_STYLE_BUTTON].base.foreground.r);

    assert(tc_ui_document_resolve_style(
        document,
        &child.widget,
        TC_UI_STYLE_STATE_HOVERED,
        &style
    ));
    assert(style.background.r == theme.roles[TC_UI_STYLE_BUTTON].hovered.value.background.r);
    assert(tc_ui_document_resolve_style(
        document,
        &child.widget,
        TC_UI_STYLE_STATE_PRESSED | TC_UI_STYLE_STATE_FOCUSED,
        &style
    ));
    assert(style.background.r == theme.roles[TC_UI_STYLE_BUTTON].pressed.value.background.r);
    assert(style.border.r == theme.roles[TC_UI_STYLE_BUTTON].focused.value.border.r);

    tc_widget_clear_dirty(&child.widget, TC_WIDGET_DIRTY_MASK);
    tc_widget_set_enabled(&parent_b.widget, false);
    assert(tc_widget_has_dirty_flags(
        &child.widget,
        TC_WIDGET_DIRTY_LAYOUT | TC_WIDGET_DIRTY_PAINT | TC_WIDGET_DIRTY_STATE
    ));
    assert((tc_ui_document_widget_style_state(document, &child.widget) &
        TC_UI_STYLE_STATE_DISABLED) != 0);
    assert(tc_ui_document_resolve_style(document, &child.widget, 0, &style));
    assert(style.background.r == theme.roles[TC_UI_STYLE_BUTTON].disabled.value.background.r);

    theme.roles[TC_UI_STYLE_BUTTON].base.font_size = 17.0f;
    tc_widget_clear_style_override(&child.widget);
    tc_widget_clear_dirty(&child.widget, TC_WIDGET_DIRTY_MASK);
    assert(tc_ui_document_set_theme(document, &theme));
    assert(tc_ui_document_theme_revision(document) == initial_revision + 1);
    assert(tc_widget_has_dirty_flags(
        &child.widget,
        TC_WIDGET_DIRTY_LAYOUT | TC_WIDGET_DIRTY_PAINT | TC_WIDGET_DIRTY_STATE
    ));
    assert(tc_ui_document_resolve_style(document, &child.widget, 0, &style));
    assert(style.font_size == 17.0f);

    tc_ui_style_override invalid {};
    invalid.fields = TC_UI_STYLE_FONT_SIZE;
    invalid.value.font_size = -1.0f;
    assert(!tc_widget_set_style_override(&child.widget, &invalid));
    assert(tc_ui_document_resolve_style(document, &child.widget, TC_UI_STYLE_STATE_CHECKED, &style));
    assert(tc_ui_document_is_alive(document, child_handle));
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
    test_pointer_routing_hover_pressed_and_bubbling();
    test_routing_snapshot_survives_destroyed_target();
    test_keyboard_bubbling_focus_events_and_tab_traversal();
    test_state_setters_survive_lifecycle_callback_destroy();
    test_overlay_paint_hit_order_and_tooltip_transparency();
    test_overlay_outside_escape_and_programmatic_dismissal();
    test_modal_overlay_blocks_lower_input_and_scopes_focus();
    test_tooltip_rect_is_host_driven_and_clamped();
    test_theme_style_resolution_inheritance_and_invalidation();
    return EXIT_SUCCESS;
}
