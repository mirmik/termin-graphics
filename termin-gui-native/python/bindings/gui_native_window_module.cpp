#include "gui_native_bindings_module.hpp"

#include <exception>

#include <tcbase/tc_log.h>

NB_MODULE(_gui_native_window, m) {
    try {
        nb::module_::import_("termin.gui_native._gui_native");
        nb::module_::import_("tgfx._tgfx_native");
        nb::module_::import_("termin.display._platform_native");
    } catch (const std::exception& error) {
        tc_log_error("[termin-gui-native/python] failed to initialize window adapter bindings: %s", error.what());
        throw;
    }
    bind_gui_native_window_adapter(m);
}
