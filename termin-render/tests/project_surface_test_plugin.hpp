#pragma once

#include <cstddef>

extern "C" {

bool termin_project_surface_test_plugin_register();
size_t termin_project_surface_test_plugin_unregister_owner();
const char* termin_project_surface_test_plugin_owner();
}
