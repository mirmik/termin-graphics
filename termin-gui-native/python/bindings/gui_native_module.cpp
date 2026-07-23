#include "gui_native_bindings_module.hpp"

#include <exception>

#include <tcbase/tc_log.h>

NB_MODULE(_gui_native, m) {
    try {
        nb::module_::import_("tgfx._tgfx_native");
        nb::module_::import_("termin.display._platform_native");
    } catch (const std::exception& error) {
        tc_log_error(
            "[termin-gui-native/python] failed to import tgfx._tgfx_native: %s",
            error.what());
        throw;
    }

    bind_gui_native_core(m);
    bind_gui_native_widgets(m);
    bind_gui_native_commands_and_dialogs(m);
    bind_gui_native_views_and_collections(m);
    bind_gui_native_rendering_and_document(m);
    bind_gui_native_application_host(m);
}
