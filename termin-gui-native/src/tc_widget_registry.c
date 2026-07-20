#include <termin/gui_native/tc_widget_registry.h>

#include <stdlib.h>
#include <string.h>

#include <tcbase/tc_log.h>

typedef struct tc_widget_factory_record {
    tc_widget_factory_descriptor descriptor;
    bool owns_userdata;
} tc_widget_factory_record;

typedef struct tc_widget_instance_ref {
    tc_ui_document* document;
    tc_widget_handle handle;
} tc_widget_instance_ref;

typedef struct tc_widget_instance_list {
    tc_widget_instance_ref* items;
    size_t count;
    size_t capacity;
    bool ok;
} tc_widget_instance_list;

static void destroy_factory_record(void* payload) {
    tc_widget_factory_record* record = (tc_widget_factory_record*)payload;
    if (!record) {
        return;
    }
    if (record->owns_userdata && record->descriptor.destroy_userdata &&
        record->descriptor.userdata) {
        record->descriptor.destroy_userdata(record->descriptor.userdata);
    }
    free(record);
}

static bool collect_widget_instance(void* instance, void* user_data) {
    tc_widget_instance_list* list = (tc_widget_instance_list*)user_data;
    tc_widget* widget = (tc_widget*)instance;
    if (!list || !widget || !widget->document || tc_widget_handle_is_invalid(widget->handle)) {
        tc_log_error("[termin-gui-native] registered widget instance has invalid ownership state");
        if (list) {
            list->ok = false;
        }
        return false;
    }
    if (list->count >= list->capacity) {
        const size_t next_capacity = list->capacity == 0 ? 8 : list->capacity * 2;
        tc_widget_instance_ref* next = (tc_widget_instance_ref*)realloc(
            list->items, next_capacity * sizeof(tc_widget_instance_ref));
        if (!next) {
            tc_log_error("[termin-gui-native] failed to collect registered widgets for unload");
            list->ok = false;
            return false;
        }
        list->items = next;
        list->capacity = next_capacity;
    }
    list->items[list->count++] = (tc_widget_instance_ref){widget->document, widget->handle};
    return true;
}

static bool prepare_factory_unload(const char* type_name, void* payload, void* context) {
    tc_widget_instance_list list = {NULL, 0, 0, true};
    size_t index;
    bool ok = true;
    (void)payload;
    (void)context;

    tc_runtime_type_registry_foreach_instance(type_name, collect_widget_instance, &list);
    if (!list.ok) {
        free(list.items);
        return false;
    }
    for (index = 0; index < list.count; ++index) {
        tc_widget_instance_ref ref = list.items[index];
        if (!tc_ui_document_is_alive(ref.document, ref.handle)) {
            continue;
        }
        if (!tc_ui_document_destroy_widget_recursive(ref.document, ref.handle)) {
            tc_log_error("[termin-gui-native] failed to invalidate widget type '%s' during unload",
                         type_name ? type_name : "<unknown>");
            ok = false;
        }
    }
    free(list.items);
    return ok;
}

static tc_widget_factory_record* factory_record(const char* type_name) {
    return (tc_widget_factory_record*)tc_runtime_type_registry_get_facet(
        type_name, TC_RUNTIME_TYPE_FACET_WIDGET_FACTORY);
}

bool tc_widget_registry_initialize(void) {
    static const char* builtin_type = "termin.gui.Widget";
    static const char* builtin_owner = "termin-gui-native";
    tc_runtime_type_descriptor* descriptor;

    descriptor = tc_runtime_type_descriptor_create(builtin_type, builtin_owner, NULL);
    if (!descriptor || !tc_runtime_type_registry_commit_descriptor(descriptor)) {
        tc_log_error("[termin-gui-native] failed to publish builtin widget parent '%s'",
                     builtin_type);
        return false;
    }
    return true;
}

bool tc_widget_registry_register(const char* type_name, const char* owner, const char* parent_type,
                                 const tc_widget_factory_descriptor* descriptor) {
    tc_widget_factory_record* record;
    tc_runtime_type_descriptor* type_descriptor;
    const bool replacing = tc_widget_registry_has(type_name);
    if (!type_name || !type_name[0] || !owner || !owner[0] || !descriptor ||
        !descriptor->create) {
        tc_log_error(
            "[termin-gui-native] widget factory registration requires type, explicit owner and create");
        return false;
    }
    if (descriptor->abi_version != TC_WIDGET_FACTORY_ABI_VERSION) {
        tc_log_error("[termin-gui-native] widget factory '%s' has unsupported ABI version %u",
                     type_name, descriptor->abi_version);
        return false;
    }
    if (descriptor->language < 0 || descriptor->language >= TC_LANGUAGE_MAX) {
        tc_log_error("[termin-gui-native] widget factory '%s' has invalid language", type_name);
        return false;
    }
    if (parent_type && parent_type[0] && strcmp(type_name, parent_type) == 0) {
        tc_log_error("[termin-gui-native] widget type '%s' cannot be its own parent", type_name);
        return false;
    }
    if (replacing) {
        if (tc_runtime_type_registry_instance_count(type_name) != 0) {
            tc_log_error(
                "[termin-gui-native] cannot replace widget factory '%s' with live instances",
                type_name);
            return false;
        }
    }
    if (parent_type && parent_type[0] &&
        !tc_runtime_type_registry_has_type(parent_type)) {
        tc_log_error("[termin-gui-native] widget type '%s' has missing parent '%s'",
                     type_name, parent_type);
        return false;
    }

    record = (tc_widget_factory_record*)calloc(1, sizeof(tc_widget_factory_record));
    if (!record) {
        tc_log_error("[termin-gui-native] failed to allocate widget factory '%s'", type_name);
        return false;
    }
    record->descriptor = *descriptor;
    type_descriptor = tc_runtime_type_descriptor_create(type_name, owner, parent_type);
    if (!type_descriptor) {
        free(record);
        return false;
    }
    if (!tc_runtime_type_descriptor_add_facet(
            type_descriptor, TC_RUNTIME_TYPE_FACET_WIDGET_FACTORY, record,
            destroy_factory_record, prepare_factory_unload, TC_WIDGET_FACTORY_ABI_VERSION)) {
        tc_runtime_type_descriptor_destroy(type_descriptor);
        return false;
    }

    if (replacing) {
        const char* existing_owner = tc_runtime_type_registry_get_owner(type_name);
        if (!existing_owner || strcmp(existing_owner, owner) != 0) {
            tc_log_error(
                "[termin-gui-native] cannot replace widget type '%s' owned by '%s' with owner '%s'",
                type_name,
                existing_owner ? existing_owner : "<none>",
                owner
            );
            tc_runtime_type_descriptor_destroy(type_descriptor);
            return false;
        }
        if (!tc_runtime_type_descriptor_allow_same_owner_replacement(type_descriptor)) {
            tc_runtime_type_descriptor_destroy(type_descriptor);
            return false;
        }
    }
    if (!tc_runtime_type_registry_commit_descriptor(type_descriptor)) {
        tc_log_error("[termin-gui-native] failed to commit widget type '%s'", type_name);
        return false;
    }
    record->owns_userdata = true;
    return true;
}

bool tc_widget_registry_unregister(const char* type_name) {
    if (!type_name || !type_name[0]) {
        tc_log_error("[termin-gui-native] cannot unregister unnamed widget type");
        return false;
    }
    if (!tc_widget_registry_has(type_name)) {
        return true;
    }
    return tc_runtime_type_registry_unregister_type_with_context(type_name, NULL);
}

size_t tc_widget_registry_unregister_owner(const char* owner,
                                           tc_widget_owner_reload_policy policy) {
    if (!owner || !owner[0]) {
        tc_log_error("[termin-gui-native] cannot unregister an unnamed widget owner");
        return 0;
    }
    if (policy != TC_WIDGET_OWNER_RELOAD_INVALIDATE) {
        tc_log_error("[termin-gui-native] widget owner '%s' requested unsupported reload policy",
                     owner);
        return 0;
    }
    return tc_runtime_type_registry_unregister_owner(owner);
}

bool tc_widget_registry_has(const char* type_name) {
    return type_name &&
           tc_runtime_type_registry_has_facet(type_name, TC_RUNTIME_TYPE_FACET_WIDGET_FACTORY);
}

size_t tc_widget_registry_type_count(void) {
    return tc_runtime_type_registry_types_with_facet_count(TC_RUNTIME_TYPE_FACET_WIDGET_FACTORY);
}

const char* tc_widget_registry_type_at(size_t index) {
    return tc_runtime_type_registry_type_with_facet_at(TC_RUNTIME_TYPE_FACET_WIDGET_FACTORY, index);
}

bool tc_widget_registry_serialize_state(const tc_widget* widget, tc_value* out_state) {
    const char* type_name;
    tc_widget_factory_record* record;
    tc_value state;
    if (!widget || !out_state || !(type_name = widget->runtime_type_link.type_name)) {
        tc_log_error("[termin-gui-native] widget state serialization requires registered widget");
        return false;
    }
    record = factory_record(type_name);
    if (!record) {
        tc_log_error("[termin-gui-native] widget type '%s' lost its factory during serialization",
                     type_name);
        return false;
    }
    state = tc_value_dict_new();
    if (record->descriptor.serialize_state &&
        !record->descriptor.serialize_state(widget, record->descriptor.userdata, &state)) {
        tc_log_error("[termin-gui-native] widget type '%s' failed to serialize state", type_name);
        tc_value_free(&state);
        return false;
    }
    if (state.type != TC_VALUE_DICT) {
        tc_log_error("[termin-gui-native] widget type '%s' returned non-dict state", type_name);
        tc_value_free(&state);
        return false;
    }
    *out_state = state;
    return true;
}

bool tc_widget_registry_deserialize_state(tc_widget* widget, const tc_value* state) {
    const char* type_name;
    tc_widget_factory_record* record;
    if (!widget || !state || state->type != TC_VALUE_DICT ||
        !(type_name = widget->runtime_type_link.type_name)) {
        tc_log_error(
            "[termin-gui-native] widget state deserialization requires registered widget and dict");
        return false;
    }
    record = factory_record(type_name);
    if (!record) {
        tc_log_error("[termin-gui-native] widget type '%s' lost its factory during deserialization",
                     type_name);
        return false;
    }
    if (!record->descriptor.deserialize_state) {
        if (tc_value_dict_size(state) != 0) {
            tc_log_error("[termin-gui-native] widget type '%s' has state but no deserializer",
                         type_name);
            return false;
        }
        return true;
    }
    if (!record->descriptor.deserialize_state(widget, state, record->descriptor.userdata)) {
        tc_log_error("[termin-gui-native] widget type '%s' failed to deserialize state", type_name);
        return false;
    }
    return true;
}

static void release_unadopted_result(const tc_widget_factory_result* result) {
    if (result && result->widget && result->ownership == TC_WIDGET_OWNED && result->deleter) {
        result->deleter(result->widget);
    }
}

tc_widget_handle tc_ui_document_create_registered_widget(tc_ui_document* document,
                                                         const char* type_name) {
    tc_widget_factory_record* record;
    tc_widget_factory_result result = {NULL, NULL, TC_WIDGET_BORROWED};
    tc_widget_handle handle = tc_widget_handle_invalid();
    if (!document || !type_name || !type_name[0]) {
        tc_log_error("[termin-gui-native] registered widget creation requires document and type");
        return handle;
    }
    record = factory_record(type_name);
    if (!record) {
        tc_log_error("[termin-gui-native] unknown registered widget type '%s'", type_name);
        return handle;
    }
    if (!record->descriptor.create(document, record->descriptor.userdata, &result) ||
        !result.widget) {
        tc_log_error("[termin-gui-native] widget factory '%s' failed to create", type_name);
        release_unadopted_result(&result);
        return handle;
    }
    if ((result.ownership == TC_WIDGET_OWNED && !result.deleter) ||
        (result.ownership == TC_WIDGET_BORROWED && result.deleter) ||
        (result.ownership != TC_WIDGET_OWNED && result.ownership != TC_WIDGET_BORROWED)) {
        tc_log_error(
            "[termin-gui-native] widget factory '%s' returned inconsistent ownership policy",
            type_name);
        release_unadopted_result(&result);
        return handle;
    }
    if (result.widget->document || !tc_widget_handle_is_invalid(result.widget->handle) ||
        result.widget->runtime_type_link.type_name) {
        tc_log_error("[termin-gui-native] widget factory '%s' returned a live widget", type_name);
        release_unadopted_result(&result);
        return handle;
    }

    result.widget->native_language = record->descriptor.language;
    handle = result.ownership == TC_WIDGET_OWNED
        ? tc_ui_document_adopt_widget(document, result.widget, result.deleter)
        : tc_ui_document_attach_borrowed_widget(document, result.widget);
    if (tc_widget_handle_is_invalid(handle)) {
        release_unadopted_result(&result);
        return handle;
    }
    if (!tc_runtime_type_registry_link_instance(type_name, &result.widget->runtime_type_link,
                                                result.widget)) {
        tc_ui_document_destroy_widget_recursive(document, handle);
        return tc_widget_handle_invalid();
    }
    if (record->descriptor.after_adopt &&
        !record->descriptor.after_adopt(document, result.widget, handle,
                                        record->descriptor.userdata)) {
        tc_log_error("[termin-gui-native] widget factory '%s' failed after adoption", type_name);
        tc_ui_document_destroy_widget_recursive(document, handle);
        return tc_widget_handle_invalid();
    }
    if (!tc_ui_document_is_alive(document, handle)) {
        tc_log_error("[termin-gui-native] widget factory '%s' destroyed its adopted widget",
                     type_name);
        return tc_widget_handle_invalid();
    }
    return handle;
}
