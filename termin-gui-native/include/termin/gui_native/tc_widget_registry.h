#ifndef TC_WIDGET_REGISTRY_H
#define TC_WIDGET_REGISTRY_H

#include <tcbase/tc_value.h>
#include <termin/gui_native/tc_ui_document.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TC_WIDGET_FACTORY_ABI_VERSION 2u
#define TC_RUNTIME_TYPE_FACET_WIDGET_FACTORY "termin.gui.widget_factory"

typedef struct tc_widget_factory_result {
    tc_widget* widget;
    tc_widget_deleter deleter;
    tc_widget_ownership_policy ownership;
} tc_widget_factory_result;

typedef bool (*tc_widget_factory_create_fn)(tc_ui_document* document, void* userdata,
                                            tc_widget_factory_result* out_result);

typedef bool (*tc_widget_factory_after_adopt_fn)(tc_ui_document* document, tc_widget* widget,
                                                 tc_widget_handle handle, void* userdata);

typedef void (*tc_widget_factory_userdata_destroy_fn)(void* userdata);

/* out_state is an initialized dict which the hook may populate or replace after freeing. */
typedef bool (*tc_widget_state_serialize_fn)(const tc_widget* widget, void* userdata,
                                             tc_value* out_state);

/* state is always a dict owned by the caller and is valid only for the callback duration. */
typedef bool (*tc_widget_state_deserialize_fn)(tc_widget* widget, const tc_value* state,
                                               void* userdata);

typedef struct tc_widget_factory_descriptor {
    uint32_t abi_version;
    tc_language language;
    tc_widget_factory_create_fn create;
    tc_widget_factory_after_adopt_fn after_adopt;
    tc_widget_factory_userdata_destroy_fn destroy_userdata;
    void* userdata;
    tc_widget_state_serialize_fn serialize_state;
    tc_widget_state_deserialize_fn deserialize_state;
} tc_widget_factory_descriptor;

/*
 * Owner reload deliberately invalidates live widgets instead of attempting an
 * in-place recreation while the owner's replacement factories are unavailable.
 * Recursive invalidation follows document topology, so descendants owned by a
 * different module are invalidated together with an unloaded parent.
 */
typedef enum tc_widget_owner_reload_policy {
    TC_WIDGET_OWNER_RELOAD_INVALIDATE = 0,
} tc_widget_owner_reload_policy;

/*
 * Publishes one complete widget runtime type. `owner` must be non-empty and an
 * existing `parent_type` must already be committed (the built-in
 * termin.gui.Widget root is initialized lazily). On success the registry owns
 * descriptor->userdata; on failure ownership remains with the caller.
 */
TERMIN_GUI_NATIVE_API bool
tc_widget_registry_register(const char* type_name, const char* owner, const char* parent_type,
                            const tc_widget_factory_descriptor* descriptor);
TERMIN_GUI_NATIVE_API bool tc_widget_registry_unregister(const char* type_name);
TERMIN_GUI_NATIVE_API size_t
tc_widget_registry_unregister_owner(const char* owner, tc_widget_owner_reload_policy policy);
TERMIN_GUI_NATIVE_API bool tc_widget_registry_has(const char* type_name);
TERMIN_GUI_NATIVE_API size_t tc_widget_registry_type_count(void);
TERMIN_GUI_NATIVE_API const char* tc_widget_registry_type_at(size_t index);

TERMIN_GUI_NATIVE_API bool tc_widget_registry_serialize_state(const tc_widget* widget,
                                                              tc_value* out_state);
TERMIN_GUI_NATIVE_API bool tc_widget_registry_deserialize_state(tc_widget* widget,
                                                                const tc_value* state);

TERMIN_GUI_NATIVE_API tc_widget_handle
tc_ui_document_create_registered_widget(tc_ui_document* document, const char* type_name);

#ifdef __cplusplus
}
#endif

#endif // TC_WIDGET_REGISTRY_H
