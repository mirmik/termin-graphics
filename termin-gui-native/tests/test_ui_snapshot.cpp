#include <termin/gui_native/tc_document.hpp>
#include <termin/gui_native/document_snapshot.hpp>

#include <cassert>
#include <cstdlib>
#include <cstring>
#include <utility>

namespace {

tc_ui_event_result handle_pointer(tc_widget*, tc_ui_document_handle, const tc_ui_pointer_event*) {
    return TC_UI_EVENT_HANDLED;
}

bool contains(const tc_ui_rect& bounds, float x, float y) {
    return x >= bounds.x && y >= bounds.y && x <= bounds.x + bounds.width &&
           y <= bounds.y + bounds.height;
}

tc_widget_handle hit_snapshot_widget(tc_widget* widget, tc_ui_document_handle, float x, float y) {
    for (size_t index = widget->child_count; index > 0; --index) {
        tc_widget* child = widget->children[index - 1];
        if (child && contains(child->bounds, x, y)) {
            return child->handle;
        }
    }
    return contains(widget->bounds, x, y) ? widget->handle : tc_widget_handle_invalid_value();
}

const tc_widget_vtable SNAPSHOT_WIDGET_VTABLE{
    .type_name = "SnapshotWidget",
    .pointer_event = &handle_pointer,
    .hit_test = &hit_snapshot_widget,
};

struct SnapshotWidget {
    tc_widget widget{};

    explicit SnapshotWidget(const char* debug_name) {
        tc_widget_init_unowned(&widget, &SNAPSHOT_WIDGET_VTABLE, TC_LANGUAGE_CXX, this);
        widget.debug_name = debug_name;
    }
};

void test_empty_snapshot_has_explicit_empty_and_invalid_state() {
    tc_ui_document_handle document_handle = tc_ui_document_create();
    termin::gui_native::TcDocument document(document_handle);
    termin::gui_native::DocumentSnapshot snapshot(document);
    assert(snapshot.widgets().empty());
    assert(snapshot.children().empty());
    assert(snapshot.roots().empty());
    assert(snapshot.overlays().empty());
    assert(tc_widget_handle_is_invalid(snapshot.data().hovered));
    assert(tc_widget_handle_is_invalid(snapshot.data().pressed));
    assert(tc_widget_handle_is_invalid(snapshot.data().pointer_capture));
    assert(tc_widget_handle_is_invalid(snapshot.data().focused));
    tc_ui_document_destroy(document_handle);
}

void test_snapshot_copies_topology_metadata_and_interaction_state() {
    tc_ui_document_handle document_handle = tc_ui_document_create();
    termin::gui_native::TcDocument document(document_handle);
    SnapshotWidget root("root-debug");
    SnapshotWidget child("child-debug");
    SnapshotWidget overlay("overlay-debug");

    root.widget.stable_id = "root-stable";
    root.widget.name = "Root Name";
    tc_widget_set_bounds(&root.widget, tc_ui_rect{0.0f, 0.0f, 120.0f, 80.0f});
    tc_widget_set_bounds(&child.widget, tc_ui_rect{5.0f, 5.0f, 40.0f, 30.0f});
    tc_widget_set_preferred_size(&child.widget, tc_ui_size{40.0f, 30.0f});
    tc_widget_set_focusable(&child.widget, true);
    assert(tc_widget_set_cursor_intent(&child.widget, TC_UI_CURSOR_CROSSHAIR));

    const tc_widget_handle root_handle =
        tc_ui_document_attach_borrowed_widget(document.get(), &root.widget);
    const tc_widget_handle child_handle =
        tc_ui_document_attach_borrowed_widget(document.get(), &child.widget);
    const tc_widget_handle overlay_handle =
        tc_ui_document_attach_borrowed_widget(document.get(), &overlay.widget);
    assert(!tc_widget_handle_is_invalid(root_handle));
    assert(!tc_widget_handle_is_invalid(child_handle));
    assert(!tc_widget_handle_is_invalid(overlay_handle));
    assert(tc_widget_append_child(&root.widget, &child.widget));
    assert(tc_ui_document_add_root(document.get(), root_handle));
    assert(tc_ui_document_show_overlay(document.get(), overlay_handle,
                                       TC_UI_OVERLAY_POINTER_TRANSPARENT | TC_UI_OVERLAY_TOOLTIP));
    assert(tc_ui_document_set_focus(document.get(), child_handle));
    assert(tc_ui_document_set_pointer_capture(document.get(), child_handle));

    tc_ui_pointer_event move{};
    move.type = TC_UI_POINTER_MOVE;
    move.x = 10.0f;
    move.y = 10.0f;
    assert(tc_ui_document_dispatch_pointer_event(document.get(), &move) == TC_UI_EVENT_HANDLED);
    tc_ui_pointer_event down = move;
    down.type = TC_UI_POINTER_DOWN;
    down.button = 1;
    down.click_count = 1;
    assert(tc_ui_document_dispatch_pointer_event(document.get(), &down) == TC_UI_EVENT_HANDLED);

    termin::gui_native::DocumentSnapshot snapshot(document);
    assert(snapshot.widgets().size() == 3);
    assert(snapshot.roots().size() == 1);
    assert(tc_widget_handle_eq(snapshot.roots()[0], root_handle));
    assert(snapshot.overlays().size() == 1);
    assert(tc_widget_handle_eq(snapshot.overlays()[0].handle, overlay_handle));
    assert(snapshot.overlays()[0].flags ==
           (TC_UI_OVERLAY_POINTER_TRANSPARENT | TC_UI_OVERLAY_TOOLTIP));

    const tc_ui_widget_snapshot* root_data = snapshot.find(root_handle);
    const tc_ui_widget_snapshot* child_data = snapshot.find(child_handle);
    assert(root_data && child_data);
    assert(std::strcmp(root_data->type_name, "SnapshotWidget") == 0);
    assert(std::strcmp(root_data->stable_id, "root-stable") == 0);
    assert(std::strcmp(root_data->name, "Root Name") == 0);
    assert(std::strcmp(root_data->debug_name, "root-debug") == 0);
    assert(root_data->native_language == TC_LANGUAGE_CXX);
    assert(root_data->ownership == TC_WIDGET_BORROWED);
    assert(root_data->child_count == 1);
    assert(tc_widget_handle_eq(snapshot.children()[root_data->child_offset], child_handle));
    assert(tc_widget_handle_eq(child_data->parent, root_handle));
    assert(child_data->preferred_size.width == 40.0f);
    assert((child_data->flags & TC_WIDGET_FOCUSABLE) != 0);
    assert(child_data->cursor_intent == TC_UI_CURSOR_CROSSHAIR);
    assert(tc_widget_handle_eq(snapshot.data().hovered, child_handle));
    assert(tc_widget_handle_eq(snapshot.data().pressed, child_handle));
    assert(tc_widget_handle_eq(snapshot.data().pointer_capture, child_handle));
    assert(tc_widget_handle_eq(snapshot.data().focused, child_handle));
    assert(snapshot.data().cursor_intent == TC_UI_CURSOR_CROSSHAIR);
    assert(snapshot.data().theme_revision == document.theme_revision());

    assert(tc_ui_document_destroy_widget_recursive(document.get(), root_handle));
    assert(!tc_ui_document_is_alive(document.get(), root_handle));
    assert(std::strcmp(root_data->stable_id, "root-stable") == 0);
    assert(tc_widget_handle_eq(snapshot.children()[root_data->child_offset], child_handle));
    tc_ui_document_destroy(document_handle);
}

void test_document_and_snapshot_move_lifetimes() {
    tc_ui_document_handle document_handle = tc_ui_document_create();
    termin::gui_native::TcDocument source(document_handle);
    termin::gui_native::TcDocument moved(std::move(source));
    assert(!tc_ui_document_handle_is_invalid(source.get()));
    assert(!tc_ui_document_handle_is_invalid(moved.get()));

    termin::gui_native::DocumentSnapshot snapshot(moved);
    termin::gui_native::DocumentSnapshot moved_snapshot(std::move(snapshot));
    assert(snapshot.widgets().empty());
    assert(moved_snapshot.widgets().empty());

    termin::gui_native::DocumentSnapshot replacement_snapshot(moved);
    replacement_snapshot = std::move(moved_snapshot);
    assert(moved_snapshot.widgets().empty());
    assert(replacement_snapshot.widgets().empty());
    tc_ui_document_destroy(document_handle);
}

} // namespace

int main() {
    test_empty_snapshot_has_explicit_empty_and_invalid_state();
    test_snapshot_copies_topology_metadata_and_interaction_state();
    test_document_and_snapshot_move_lifetimes();
    return EXIT_SUCCESS;
}
