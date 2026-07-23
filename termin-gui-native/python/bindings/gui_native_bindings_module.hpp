#pragma once

#include <nanobind/nanobind.h>

namespace nb = nanobind;

void bind_gui_native_core(nb::module_& m);
void bind_gui_native_widgets(nb::module_& m);
void bind_gui_native_commands_and_dialogs(nb::module_& m);
void bind_gui_native_views_and_collections(nb::module_& m);
void bind_gui_native_rendering_and_document(nb::module_& m);
void bind_gui_native_application_host(nb::module_& m);
