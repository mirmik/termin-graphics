#ifndef TERMIN_GUI_NATIVE_TC_UI_SERIALIZATION_H
#define TERMIN_GUI_NATIVE_TC_UI_SERIALIZATION_H

#include <tcbase/tc_value.h>
#include <termin/gui_native/tc_ui_document.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TC_UI_DOCUMENT_SCHEMA "termin.gui.document"
#define TC_UI_DOCUMENT_SCHEMA_VERSION 2

/* Returns a dict on success and nil on failure. The caller owns the result. */
TERMIN_GUI_NATIVE_API tc_value tc_ui_document_serialize(tc_ui_document_handle document);

/* Restores into an empty document. Any widgets created before failure are rolled back. */
TERMIN_GUI_NATIVE_API bool tc_ui_document_restore(tc_ui_document_handle document,
                                                  const tc_value* serialized);

#ifdef __cplusplus
}
#endif

#endif // TERMIN_GUI_NATIVE_TC_UI_SERIALIZATION_H
