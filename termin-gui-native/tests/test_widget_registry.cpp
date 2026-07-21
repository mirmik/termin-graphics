#include <termin/gui_native/tc_widget_registry.h>

#include <cassert>
#include <cstdlib>
#include <cstring>

namespace {

struct FactoryState;

struct FactoryWidget {
    tc_widget widget{};
    FactoryState* state = nullptr;
};

struct FactoryState {
    size_t creates = 0;
    size_t adopts = 0;
    size_t deletes = 0;
    size_t userdata_destroys = 0;
    bool fail_after_adopt = false;
    bool borrowed = false;
    int64_t serialized_value = 17;
    FactoryWidget borrowed_widget{};
};

const tc_widget_vtable FACTORY_WIDGET_VTABLE{
    .type_name = "FactoryWidgetFallback",
};

void delete_factory_widget(tc_widget* widget) {
    auto* body = static_cast<FactoryWidget*>(widget ? widget->body : nullptr);
    assert(body && body->state);
    body->state->deletes += 1;
    delete body;
}

bool create_factory_widget(tc_ui_document_handle, void* userdata, tc_widget_factory_result* result) {
    auto* state = static_cast<FactoryState*>(userdata);
    assert(state && result);
    state->creates += 1;
    FactoryWidget* body = nullptr;
    if (state->borrowed) {
        body = &state->borrowed_widget;
        body->state = state;
        tc_widget_init_unowned(&body->widget, &FACTORY_WIDGET_VTABLE, TC_LANGUAGE_CXX, body);
        *result = tc_widget_factory_result{&body->widget, nullptr, TC_WIDGET_BORROWED};
    } else {
        body = new FactoryWidget();
        body->state = state;
        tc_widget_init_unowned(&body->widget, &FACTORY_WIDGET_VTABLE, TC_LANGUAGE_CXX, body);
        *result = tc_widget_factory_result{
            &body->widget,
            &delete_factory_widget,
            TC_WIDGET_OWNED,
        };
    }
    return true;
}

bool after_adopt_factory_widget(tc_ui_document_handle, tc_widget*, tc_widget_handle, void* userdata) {
    auto* state = static_cast<FactoryState*>(userdata);
    state->adopts += 1;
    return !state->fail_after_adopt;
}

void destroy_factory_userdata(void* userdata) {
    auto* state = static_cast<FactoryState*>(userdata);
    state->userdata_destroys += 1;
}

bool serialize_factory_widget(const tc_widget*, void* userdata, tc_value* state) {
    auto* factory_state = static_cast<FactoryState*>(userdata);
    assert(factory_state && state && state->type == TC_VALUE_DICT);
    tc_value_dict_set(state, "value", tc_value_int(factory_state->serialized_value));
    return true;
}

bool deserialize_factory_widget(tc_widget*, const tc_value* state, void* userdata) {
    auto* factory_state = static_cast<FactoryState*>(userdata);
    tc_value* value = tc_value_dict_get(const_cast<tc_value*>(state), "value");
    if (!factory_state || !value || value->type != TC_VALUE_INT) {
        return false;
    }
    factory_state->serialized_value = value->data.i;
    return true;
}

tc_widget_factory_descriptor descriptor(FactoryState& state) {
    return tc_widget_factory_descriptor{
        TC_WIDGET_FACTORY_ABI_VERSION, TC_LANGUAGE_CXX,
        &create_factory_widget,        &after_adopt_factory_widget,
        &destroy_factory_userdata,     &state,
        &serialize_factory_widget,     &deserialize_factory_widget,
    };
}

void test_owned_factory_identity_and_unload_invalidation() {
    tc_runtime_type_registry_clear();
    assert(tc_widget_registry_initialize());
    FactoryState state;
    const tc_widget_factory_descriptor factory = descriptor(state);
    assert(tc_widget_registry_register("test.ui.OwnedWidget", "test.module", "termin.gui.Widget",
                                       &factory));
    assert(tc_widget_registry_has("test.ui.OwnedWidget"));
    assert(tc_widget_registry_type_count() == 1);
    assert(std::strcmp(tc_widget_registry_type_at(0), "test.ui.OwnedWidget") == 0);
    assert(std::strcmp(tc_runtime_type_registry_get_owner("test.ui.OwnedWidget"), "test.module") ==
           0);
    assert(std::strcmp(tc_runtime_type_registry_get_parent("test.ui.OwnedWidget"),
                       "termin.gui.Widget") == 0);

    tc_ui_document_handle document = tc_ui_document_create();
    assert(tc_ui_document_is_valid(document));
    const tc_widget_handle parent =
        tc_ui_document_create_registered_widget(document, "test.ui.OwnedWidget");
    const tc_widget_handle child =
        tc_ui_document_create_registered_widget(document, "test.ui.OwnedWidget");
    assert(!tc_widget_handle_is_invalid(parent));
    assert(!tc_widget_handle_is_invalid(child));
    tc_widget* parent_widget = tc_ui_document_resolve_widget(document, parent);
    tc_widget* child_widget = tc_ui_document_resolve_widget(document, child);
    assert(parent_widget && child_widget);
    assert(tc_widget_append_child(parent_widget, child_widget));
    assert(std::strcmp(tc_widget_type_name(parent_widget), "test.ui.OwnedWidget") == 0);
    assert(tc_widget_ownership(parent_widget) == TC_WIDGET_OWNED);
    assert(tc_runtime_type_registry_instance_count("test.ui.OwnedWidget") == 2);
    assert(state.creates == 2 && state.adopts == 2);

    assert(!tc_widget_registry_register("test.ui.OwnedWidget", "test.module", "termin.gui.Widget",
                                        &factory));
    assert(tc_widget_registry_unregister("test.ui.OwnedWidget"));
    assert(!tc_ui_document_is_alive(document, parent));
    assert(!tc_ui_document_is_alive(document, child));
    assert(state.deletes == 2);
    assert(state.userdata_destroys == 1);
    assert(!tc_widget_registry_has("test.ui.OwnedWidget"));
    tc_ui_document_destroy(document);
    tc_runtime_type_registry_clear();
}

void test_descriptor_validation_leaves_no_partial_widget_type() {
    tc_runtime_type_registry_clear();
    FactoryState state;
    const tc_widget_factory_descriptor factory = descriptor(state);

    assert(!tc_widget_registry_register("test.ui.NoOwner", nullptr, nullptr, &factory));
    assert(!tc_runtime_type_registry_has_type("test.ui.NoOwner"));
    assert(!tc_widget_registry_register("test.ui.MissingParent", "test.module",
                                        "test.ui.DoesNotExist", &factory));
    assert(!tc_runtime_type_registry_has_type("test.ui.MissingParent"));
    assert(!tc_widget_registry_register("test.ui.SelfParent", "test.module",
                                        "test.ui.SelfParent", &factory));
    assert(!tc_runtime_type_registry_has_type("test.ui.SelfParent"));
    assert(state.userdata_destroys == 0);
    tc_runtime_type_registry_clear();
}

void test_idle_factory_replacement_uses_a_fresh_descriptor() {
    tc_runtime_type_registry_clear();
    FactoryState first_state;
    FactoryState second_state;
    const tc_widget_factory_descriptor first = descriptor(first_state);
    const tc_widget_factory_descriptor second = descriptor(second_state);

    assert(tc_widget_registry_register("test.ui.Replaceable", "test.module", nullptr, &first));
    assert(tc_widget_registry_register("test.ui.Replaceable", "test.module", nullptr, &second));
    assert(first_state.userdata_destroys == 1);
    assert(second_state.userdata_destroys == 0);

    tc_ui_document_handle document = tc_ui_document_create();
    const tc_widget_handle handle =
        tc_ui_document_create_registered_widget(document, "test.ui.Replaceable");
    assert(!tc_widget_handle_is_invalid(handle));
    assert(first_state.creates == 0);
    assert(second_state.creates == 1);
    assert(tc_widget_registry_unregister("test.ui.Replaceable"));
    assert(second_state.userdata_destroys == 1);
    tc_ui_document_destroy(document);
    tc_runtime_type_registry_clear();
}

void test_borrowed_factory_is_explicit_and_not_deleted() {
    tc_runtime_type_registry_clear();
    FactoryState state;
    state.borrowed = true;
    const tc_widget_factory_descriptor factory = descriptor(state);
    assert(tc_widget_registry_register("test.ui.BorrowedWidget", "test.module", nullptr, &factory));
    tc_ui_document_handle document = tc_ui_document_create();
    const tc_widget_handle handle =
        tc_ui_document_create_registered_widget(document, "test.ui.BorrowedWidget");
    assert(!tc_widget_handle_is_invalid(handle));
    tc_widget* widget = tc_ui_document_resolve_widget(document, handle);
    assert(widget == &state.borrowed_widget.widget);
    assert(tc_widget_ownership(widget) == TC_WIDGET_BORROWED);
    assert(tc_widget_registry_unregister("test.ui.BorrowedWidget"));
    assert(!tc_ui_document_is_alive(document, handle));
    assert(state.deletes == 0);
    assert(tc_ui_document_handle_is_invalid(state.borrowed_widget.widget.document));
    tc_ui_document_destroy(document);
    tc_runtime_type_registry_clear();
}

void test_after_adopt_failure_rolls_back_handle_and_owned_body() {
    tc_runtime_type_registry_clear();
    FactoryState state;
    state.fail_after_adopt = true;
    const tc_widget_factory_descriptor factory = descriptor(state);
    assert(tc_widget_registry_register("test.ui.FailingWidget", "test.module", nullptr, &factory));
    tc_ui_document_handle document = tc_ui_document_create();
    const tc_widget_handle handle =
        tc_ui_document_create_registered_widget(document, "test.ui.FailingWidget");
    assert(tc_widget_handle_is_invalid(handle));
    assert(state.creates == 1 && state.adopts == 1 && state.deletes == 1);
    assert(tc_runtime_type_registry_instance_count("test.ui.FailingWidget") == 0);
    assert(tc_widget_registry_unregister("test.ui.FailingWidget"));
    tc_ui_document_destroy(document);
    tc_runtime_type_registry_clear();
}

void test_registered_state_hooks_round_trip_strict_dict_state() {
    tc_runtime_type_registry_clear();
    FactoryState state;
    const tc_widget_factory_descriptor factory = descriptor(state);
    assert(tc_widget_registry_register("test.ui.StatefulWidget", "test.module", nullptr, &factory));
    tc_ui_document_handle document = tc_ui_document_create();
    const tc_widget_handle handle =
        tc_ui_document_create_registered_widget(document, "test.ui.StatefulWidget");
    tc_widget* widget = tc_ui_document_resolve_widget(document, handle);
    assert(widget);

    tc_value serialized = tc_value_nil();
    assert(tc_widget_registry_serialize_state(widget, &serialized));
    assert(serialized.type == TC_VALUE_DICT);
    tc_value* value = tc_value_dict_get(&serialized, "value");
    assert(value && value->type == TC_VALUE_INT && value->data.i == 17);

    tc_value restored = tc_value_dict_new();
    tc_value_dict_set(&restored, "value", tc_value_int(91));
    assert(tc_widget_registry_deserialize_state(widget, &restored));
    assert(state.serialized_value == 91);
    tc_value_free(&restored);
    tc_value_free(&serialized);

    assert(tc_widget_registry_unregister("test.ui.StatefulWidget"));
    tc_ui_document_destroy(document);
    tc_runtime_type_registry_clear();
}

void test_owner_reload_invalidates_documents_nested_trees_and_factory_userdata() {
    tc_runtime_type_registry_clear();
    FactoryState parent_state;
    FactoryState sibling_state;
    FactoryState foreign_state;
    const tc_widget_factory_descriptor parent_factory = descriptor(parent_state);
    const tc_widget_factory_descriptor sibling_factory = descriptor(sibling_state);
    const tc_widget_factory_descriptor foreign_factory = descriptor(foreign_state);
    assert(tc_widget_registry_register("test.ui.ReloadParent", "test.module.reload", nullptr,
                                       &parent_factory));
    assert(tc_widget_registry_register("test.ui.ReloadSibling", "test.module.reload", nullptr,
                                       &sibling_factory));
    assert(tc_widget_registry_register("test.ui.ForeignChild", "test.module.foreign", nullptr,
                                       &foreign_factory));

    tc_ui_document_handle first_document = tc_ui_document_create();
    tc_ui_document_handle second_document = tc_ui_document_create();
    assert(tc_ui_document_is_valid(first_document));
    assert(tc_ui_document_is_valid(second_document));
    const tc_widget_handle parent =
        tc_ui_document_create_registered_widget(first_document, "test.ui.ReloadParent");
    const tc_widget_handle foreign_child =
        tc_ui_document_create_registered_widget(first_document, "test.ui.ForeignChild");
    const tc_widget_handle sibling =
        tc_ui_document_create_registered_widget(second_document, "test.ui.ReloadSibling");
    assert(tc_widget_append_child(tc_ui_document_resolve_widget(first_document, parent),
                                  tc_ui_document_resolve_widget(first_document, foreign_child)));

    assert(tc_widget_registry_unregister_owner("test.module.reload",
                                               TC_WIDGET_OWNER_RELOAD_INVALIDATE) == 2);
    assert(!tc_ui_document_is_alive(first_document, parent));
    assert(!tc_ui_document_is_alive(first_document, foreign_child));
    assert(!tc_ui_document_is_alive(second_document, sibling));
    assert(parent_state.deletes == 1 && sibling_state.deletes == 1 && foreign_state.deletes == 1);
    assert(parent_state.userdata_destroys == 1 && sibling_state.userdata_destroys == 1);
    assert(foreign_state.userdata_destroys == 0);
    assert(!tc_widget_registry_has("test.ui.ReloadParent"));
    assert(!tc_widget_registry_has("test.ui.ReloadSibling"));
    assert(tc_widget_registry_has("test.ui.ForeignChild"));

    const tc_widget_handle fresh_foreign =
        tc_ui_document_create_registered_widget(first_document, "test.ui.ForeignChild");
    assert(!tc_widget_handle_is_invalid(fresh_foreign));
    assert(fresh_foreign.generation != foreign_child.generation);
    assert(tc_widget_registry_register("test.ui.ReloadParent", "test.module.reload", nullptr,
                                       &parent_factory));
    const tc_widget_handle fresh_parent =
        tc_ui_document_create_registered_widget(first_document, "test.ui.ReloadParent");
    assert(!tc_widget_handle_is_invalid(fresh_parent));
    assert(fresh_parent.generation != parent.generation);

    assert(tc_widget_registry_unregister_owner("test.module.reload",
                                               TC_WIDGET_OWNER_RELOAD_INVALIDATE) == 1);
    assert(tc_widget_registry_unregister_owner("test.module.foreign",
                                               TC_WIDGET_OWNER_RELOAD_INVALIDATE) == 1);
    tc_ui_document_destroy(second_document);
    tc_ui_document_destroy(first_document);
    tc_runtime_type_registry_clear();
}

} // namespace

int main() {
    test_owned_factory_identity_and_unload_invalidation();
    test_descriptor_validation_leaves_no_partial_widget_type();
    test_idle_factory_replacement_uses_a_fresh_descriptor();
    test_borrowed_factory_is_explicit_and_not_deleted();
    test_after_adopt_failure_rolls_back_handle_and_owned_body();
    test_registered_state_hooks_round_trip_strict_dict_state();
    test_owner_reload_invalidates_documents_nested_trees_and_factory_userdata();
    return EXIT_SUCCESS;
}
