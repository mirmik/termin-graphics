#include <termin/gui_native/tc_document.hpp>
#include <termin/gui_native/tc_ui_serialization.h>
#include <termin/gui_native/tc_widget_registry.h>

#include <cassert>
#include <cstdlib>
#include <cstring>

namespace {

struct SerializableWidget {
    tc_widget widget{};
    int64_t value = 0;
};

struct FactoryState {
    int64_t next_value = 0;
};

const tc_widget_vtable SERIALIZABLE_VTABLE{
    .type_name = "SerializableFallback",
};

void delete_serializable(tc_widget* widget) {
    auto* body = static_cast<SerializableWidget*>(widget->body);
    delete body;
}

bool create_serializable(tc_ui_document_handle, void* userdata, tc_widget_factory_result* result) {
    auto* state = static_cast<FactoryState*>(userdata);
    auto* body = new SerializableWidget();
    body->value = state->next_value++;
    tc_widget_init_unowned(&body->widget, &SERIALIZABLE_VTABLE, TC_LANGUAGE_CXX, body);
    *result = tc_widget_factory_result{&body->widget, &delete_serializable, TC_WIDGET_OWNED};
    return true;
}

bool serialize_state(const tc_widget* widget, void*, tc_value* state) {
    const auto* body = static_cast<const SerializableWidget*>(widget->body);
    tc_value_dict_set(state, "value", tc_value_int(body->value));
    return true;
}

bool deserialize_state(tc_widget* widget, const tc_value* state, void*) {
    auto* body = static_cast<SerializableWidget*>(widget->body);
    tc_value* value = tc_value_dict_get(const_cast<tc_value*>(state), "value");
    if (!value || value->type != TC_VALUE_INT) {
        return false;
    }
    body->value = value->data.i;
    return true;
}

tc_widget_factory_descriptor descriptor(FactoryState& state) {
    return tc_widget_factory_descriptor{
        TC_WIDGET_FACTORY_ABI_VERSION,
        TC_LANGUAGE_CXX,
        &create_serializable,
        nullptr,
        nullptr,
        &state,
        &serialize_state,
        &deserialize_state,
    };
}

SerializableWidget* body(tc_ui_document_handle document, tc_widget_handle handle) {
    tc_widget* widget = tc_ui_document_resolve_widget(document, handle);
    return widget ? static_cast<SerializableWidget*>(widget->body) : nullptr;
}

void test_document_round_trip_preserves_structure_common_and_type_state() {
    tc_runtime_type_registry_clear();
    FactoryState state;
    const tc_widget_factory_descriptor factory = descriptor(state);
    assert(tc_widget_registry_register("test.ui.SerializableWidget", "test.module", nullptr,
                                       &factory));

    tc_ui_document_handle source = tc_ui_document_create();
    const tc_widget_handle root =
        tc_ui_document_create_registered_widget(source, "test.ui.SerializableWidget");
    const tc_widget_handle child =
        tc_ui_document_create_registered_widget(source, "test.ui.SerializableWidget");
    const tc_widget_handle overlay =
        tc_ui_document_create_registered_widget(source, "test.ui.SerializableWidget");
    assert(body(source, root) && body(source, child) && body(source, overlay));
    body(source, root)->value = 101;
    body(source, child)->value = 202;
    body(source, overlay)->value = 303;
    tc_widget* root_widget = tc_ui_document_resolve_widget(source, root);
    tc_widget* child_widget = tc_ui_document_resolve_widget(source, child);
    assert(tc_widget_set_stable_id(root_widget, "root-id"));
    assert(tc_widget_set_name(root_widget, "Root Name"));
    assert(tc_widget_set_debug_name(root_widget, "Root Debug"));
    tc_widget_set_bounds(root_widget, tc_ui_rect{1.0f, 2.0f, 300.0f, 200.0f});
    tc_widget_set_preferred_size(child_widget, tc_ui_size{80.0f, 24.0f});
    tc_widget_set_focusable(child_widget, true);
    tc_widget_set_enabled(child_widget, false);
    assert(tc_widget_set_cursor_intent(child_widget, TC_UI_CURSOR_CROSSHAIR));
    tc_widget_set_style_role(child_widget, TC_UI_STYLE_BUTTON);
    tc_ui_style_override style{};
    style.fields = TC_UI_STYLE_BACKGROUND | TC_UI_STYLE_FONT_SIZE |
                   TC_UI_STYLE_CORNER_RADIUS;
    style.flags = TC_UI_STYLE_OVERRIDE_INHERIT;
    style.value.background = tc_ui_color{0.1f, 0.2f, 0.3f, 1.0f};
    style.value.font_size = 19.0f;
    style.value.corner_radius = 7.0f;
    assert(tc_widget_set_style_override(child_widget, &style));
    assert(tc_widget_append_child(root_widget, child_widget));
    assert(tc_ui_document_add_root(source, root));
    assert(tc_ui_document_show_overlay(source, overlay, TC_UI_OVERLAY_TOOLTIP));

    tc_value serialized = tc_ui_document_serialize(source);
    assert(serialized.type == TC_VALUE_DICT);
    tc_value* schema = tc_value_dict_get(&serialized, "$schema");
    assert(schema && std::strcmp(schema->data.s, TC_UI_DOCUMENT_SCHEMA) == 0);

    tc_ui_document_handle restored_handle = tc_ui_document_create();
    termin::gui_native::TcDocument restored_owner(restored_handle);
    const tc::trent serialized_cpp = tc::trent::copy_of(serialized);
    restored_owner.restore(serialized_cpp);
    tc_ui_document_handle restored = restored_owner.get();
    assert(tc_ui_document_live_widget_count(restored) == 3);
    assert(tc_ui_document_root_count(restored) == 1);
    assert(tc_ui_document_overlay_count(restored) == 1);
    const tc_widget_handle restored_root = tc_ui_document_root_at(restored, 0);
    tc_widget* restored_root_widget = tc_ui_document_resolve_widget(restored, restored_root);
    assert(restored_root_widget && restored_root_widget->child_count == 1);
    tc_widget* restored_child_widget = restored_root_widget->children[0];
    const tc_widget_handle restored_overlay = tc_ui_document_overlay_at(restored, 0);
    assert(std::strcmp(tc_widget_type_name(restored_root_widget), "test.ui.SerializableWidget") ==
           0);
    assert(std::strcmp(tc_widget_stable_id(restored_root_widget), "root-id") == 0);
    assert(std::strcmp(tc_widget_name(restored_root_widget), "Root Name") == 0);
    assert(std::strcmp(tc_widget_debug_name(restored_root_widget), "Root Debug") == 0);
    assert(restored_root_widget->bounds.width == 300.0f);
    assert(body(restored, restored_root)->value == 101);
    assert(body(restored, restored_child_widget->handle)->value == 202);
    assert(body(restored, restored_overlay)->value == 303);
    assert(tc_widget_is_focusable(restored_child_widget));
    assert(!tc_widget_is_enabled(restored_child_widget));
    assert(tc_widget_cursor_intent(restored_child_widget) == TC_UI_CURSOR_CROSSHAIR);
    assert(tc_widget_style_role(restored_child_widget) == TC_UI_STYLE_BUTTON);
    const tc_ui_style_override restored_style = tc_widget_style_override(restored_child_widget);
    assert(restored_style.fields == style.fields);
    assert(restored_style.flags == style.flags);
    assert(restored_style.value.font_size == 19.0f);
    assert(restored_style.value.corner_radius == 7.0f);
    assert(tc_ui_document_overlay_flags_at(restored, 0) ==
           (TC_UI_OVERLAY_TOOLTIP | TC_UI_OVERLAY_POINTER_TRANSPARENT));
    const tc::trent reserialized = restored_owner.serialize();
    assert(reserialized.raw()->type == TC_VALUE_DICT);

    tc_value* records = tc_value_dict_get(&serialized, "widgets");
    tc_value* first_record = tc_value_list_get(records, 0);
    tc_value* children = tc_value_dict_get(first_record, "children");
    tc_value_list_get(children, 0)->data.i = 0;
    tc_ui_document_handle rejected = tc_ui_document_create();
    assert(!tc_ui_document_restore(rejected, &serialized));
    assert(tc_ui_document_live_widget_count(rejected) == 0);

    tc_ui_document_destroy(rejected);
    tc_ui_document_destroy(restored_handle);
    tc_ui_document_destroy(source);
    tc_value_free(&serialized);
    assert(tc_widget_registry_unregister("test.ui.SerializableWidget"));
    tc_runtime_type_registry_clear();
}

} // namespace

int main() {
    test_document_round_trip_preserves_structure_common_and_type_state();
    return EXIT_SUCCESS;
}
