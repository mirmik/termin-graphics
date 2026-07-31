#ifndef TERMIN_GUI_NATIVE_TC_UISCRIPT_H
#define TERMIN_GUI_NATIVE_TC_UISCRIPT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <tcbase/tc_value.h>
#include <termin/gui_native/export.h>
#include <termin/gui_native/tc_ui_document.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TC_UISCRIPT_TYPE_ABI_VERSION 2u
#define TC_RUNTIME_TYPE_FACET_UISCRIPT "termin.gui.uiscript"

typedef bool (*tc_uiscript_apply_properties_fn)(
    tc_widget* widget,
    const tc_value* properties
);

typedef bool (*tc_uiscript_attach_child_fn)(
    tc_widget* parent,
    tc_widget* child,
    const tc_value* child_properties
);

typedef struct tc_uiscript_type_descriptor {
    uint32_t abi_version;
    const char* const* properties;
    size_t property_count;
    tc_uiscript_apply_properties_fn apply_properties;
    tc_uiscript_attach_child_fn attach_child;
    const char* const* child_properties;
    size_t child_property_count;
} tc_uiscript_type_descriptor;

TERMIN_GUI_NATIVE_API const tc_uiscript_type_descriptor*
tc_uiscript_type_descriptor_get(const char* type_name);

#ifdef __cplusplus
}
#endif

#endif
