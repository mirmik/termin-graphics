#include "gui_native_bindings_module.hpp"

void bind_gui_native_scene_views(nb::module_ &m);
void bind_gui_native_collection_views(nb::module_ &m);
void bind_gui_native_control_views(nb::module_ &m);

void bind_gui_native_views_and_collections(nb::module_ &m) {
  bind_gui_native_scene_views(m);
  bind_gui_native_collection_views(m);
  bind_gui_native_control_views(m);
}
