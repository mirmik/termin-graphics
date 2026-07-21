#include "gui_native_bindings_shared.hpp"

using namespace termin::gui_native::python_bindings;

void bind_gui_native_collection_views(nb::module_ &m) {
  nb::enum_<termin::gui_native::SelectionMode>(m, "SelectionMode")
      .value("Single", termin::gui_native::SelectionMode::Single)
      .value("Multiple", termin::gui_native::SelectionMode::Multiple);

  nb::class_<ListWidgetRef>(m, "ListWidget")
      .def_prop_ro("widget",
                   [](const ListWidgetRef &self) { return self.widget; })
      .def_prop_ro("handle",
                   [](const ListWidgetRef &self) {
                     return WidgetHandle{self.widget.handle};
                   })
      .def_prop_rw(
          "model", [](const ListWidgetRef &self) { return self.get().model(); },
          [](const ListWidgetRef &self,
             std::shared_ptr<termin::gui_native::CollectionModel> model) {
            self.get().set_model(std::move(model));
          })
      .def_prop_rw(
          "selection_mode",
          [](const ListWidgetRef &self) {
            return self.get().selection().mode();
          },
          [](const ListWidgetRef &self,
             termin::gui_native::SelectionMode mode) {
            self.get().set_selection_mode(mode);
          })
      .def_prop_ro("selected_indices",
                   [](const ListWidgetRef &self) {
                     return self.get().selection().selected_indices();
                   })
      .def_prop_ro("current_index",
                   [](const ListWidgetRef &self) -> int64_t {
                     const size_t index = self.get().selection().current();
                     return index == termin::gui_native::SelectionModel::npos
                                ? -1
                                : static_cast<int64_t>(index);
                   })
      .def_prop_rw(
          "scroll_y",
          [](const ListWidgetRef &self) { return self.get().scroll_y(); },
          [](const ListWidgetRef &self, float value) {
            self.get().set_scroll_y(value);
          })
      .def_prop_ro(
          "content_height",
          [](const ListWidgetRef &self) { return self.get().content_height(); })
      .def_prop_ro("visible_range",
                   [](const ListWidgetRef &self) {
                     const auto [first, last] = self.get().visible_range();
                     return std::vector<size_t>{first, last};
                   })
      .def(
          "set_row_height",
          [](const ListWidgetRef &self, float height) {
            self.get().set_row_height(height);
          },
          nb::arg("height"))
      .def(
          "set_row_spacing",
          [](const ListWidgetRef &self, float spacing) {
            self.get().set_row_spacing(spacing);
          },
          nb::arg("spacing"))
      .def(
          "select",
          [](const ListWidgetRef &self, size_t index, bool toggle, bool extend,
             bool additive) {
            const bool changed =
                self.get().select_index(index, toggle, extend, additive);
            self.widget.throw_pending_exception();
            return changed;
          },
          nb::arg("index"), nb::arg("toggle") = false,
          nb::arg("extend") = false, nb::arg("additive") = false)
      .def("clear_selection",
           [](const ListWidgetRef &self) {
             const bool changed = self.get().clear_selection();
             self.widget.throw_pending_exception();
             return changed;
           })
      .def(
          "ensure_visible",
          [](const ListWidgetRef &self, size_t index) {
            self.get().ensure_visible(index);
          },
          nb::arg("index"))
      .def(
          "connect_selection_changed",
          [](const ListWidgetRef &self, nb::object callback) {
            auto state = self.widget.state;
            return self.get().selection_changed().connect(
                [state, callback = std::move(callback)](
                    termin::gui_native::ListWidget &,
                    const std::vector<size_t> &selected) {
                  try {
                    nb::gil_scoped_acquire gil;
                    callback(selected);
                  } catch (...) {
                    if (state && !state->pending_exception)
                      state->pending_exception = std::current_exception();
                    tc_log_error("[termin-gui-native/python] ListWidget "
                                 "selection callback failed");
                  }
                });
          },
          nb::arg("callback"))
      .def(
          "connect_context_menu_requested",
          [](const ListWidgetRef &self, nb::object callback) {
            auto state = self.widget.state;
            return self.get().context_menu_requested().connect(
                [state, callback = std::move(callback)](
                    termin::gui_native::ListWidget &, int64_t index, float x,
                    float y) {
                  try {
                    nb::gil_scoped_acquire gil;
                    callback(index, x, y);
                  } catch (...) {
                    if (state && !state->pending_exception)
                      state->pending_exception = std::current_exception();
                    tc_log_error("[termin-gui-native/python] ListWidget "
                                 "context callback failed");
                  }
                });
          },
          nb::arg("callback"))
      .def(
          "connect_activated",
          [](const ListWidgetRef &self, nb::object callback) {
            auto state = self.widget.state;
            return self.get().activated().connect(
                [state, callback = std::move(callback)](
                    termin::gui_native::ListWidget &, size_t index,
                    const termin::gui_native::CollectionItem &item) {
                  try {
                    nb::gil_scoped_acquire gil;
                    callback(index, item);
                  } catch (...) {
                    if (state && !state->pending_exception)
                      state->pending_exception = std::current_exception();
                    tc_log_error("[termin-gui-native/python] ListWidget "
                                 "activation callback failed");
                  }
                });
          },
          nb::arg("callback"));

  nb::class_<FileGridWidgetRef>(m, "FileGridWidget")
      .def_prop_ro("widget",
                   [](const FileGridWidgetRef &self) { return self.widget; })
      .def_prop_ro("handle",
                   [](const FileGridWidgetRef &self) {
                     return WidgetHandle{self.widget.handle};
                   })
      .def_prop_rw(
          "model",
          [](const FileGridWidgetRef &self) { return self.get().model(); },
          [](const FileGridWidgetRef &self,
             std::shared_ptr<termin::gui_native::CollectionModel> model) {
            self.get().set_model(std::move(model));
          })
      .def_prop_rw(
          "selection_mode",
          [](const FileGridWidgetRef &self) {
            return self.get().selection().mode();
          },
          [](const FileGridWidgetRef &self,
             termin::gui_native::SelectionMode mode) {
            self.get().set_selection_mode(mode);
            self.widget.throw_pending_exception();
          })
      .def_prop_ro("selected_indices",
                   [](const FileGridWidgetRef &self) {
                     return self.get().selection().selected_indices();
                   })
      .def_prop_ro("current_index",
                   [](const FileGridWidgetRef &self) -> int64_t {
                     const size_t index = self.get().selection().current();
                     return index == termin::gui_native::SelectionModel::npos
                                ? -1
                                : static_cast<int64_t>(index);
                   })
      .def_prop_ro("column_count",
                   [](const FileGridWidgetRef &self) {
                     return self.get().column_count();
                   })
      .def_prop_ro(
          "row_count",
          [](const FileGridWidgetRef &self) { return self.get().row_count(); })
      .def_prop_rw(
          "scroll_y",
          [](const FileGridWidgetRef &self) { return self.get().scroll_y(); },
          [](const FileGridWidgetRef &self, float value) {
            self.get().set_scroll_y(value);
          })
      .def_prop_ro("content_height",
                   [](const FileGridWidgetRef &self) {
                     return self.get().content_height();
                   })
      .def_prop_ro("has_scrollbar",
                   [](const FileGridWidgetRef &self) {
                     return self.get().has_scrollbar();
                   })
      .def_prop_ro("scrollbar_thumb_rect",
                   [](const FileGridWidgetRef &self) {
                     return self.get().scrollbar_thumb_rect();
                   })
      .def_prop_ro("visible_range",
                   [](const FileGridWidgetRef &self) {
                     const auto [first, last] = self.get().visible_range();
                     return std::vector<size_t>{first, last};
                   })
      .def_prop_rw(
          "show_scrollbar",
          [](const FileGridWidgetRef &self) {
            return self.get().show_scrollbar();
          },
          [](const FileGridWidgetRef &self, bool value) {
            self.get().set_show_scrollbar(value);
          })
      .def_prop_rw(
          "empty_text",
          [](const FileGridWidgetRef &self) { return self.get().empty_text(); },
          [](const FileGridWidgetRef &self, std::string value) {
            self.get().set_empty_text(std::move(value));
          })
      .def(
          "set_tile_size",
          [](const FileGridWidgetRef &self, float width, float height) {
            self.get().set_tile_size(width, height);
          },
          nb::arg("width"), nb::arg("height"))
      .def(
          "set_tile_spacing",
          [](const FileGridWidgetRef &self, float spacing) {
            self.get().set_tile_spacing(spacing);
          },
          nb::arg("spacing"))
      .def(
          "set_padding",
          [](const FileGridWidgetRef &self, float padding) {
            self.get().set_padding(padding);
          },
          nb::arg("padding"))
      .def(
          "set_icon_size",
          [](const FileGridWidgetRef &self, float size) {
            self.get().set_icon_size(size);
          },
          nb::arg("size"))
      .def(
          "set_scrollbar_width",
          [](const FileGridWidgetRef &self, float width) {
            self.get().set_scrollbar_width(width);
          },
          nb::arg("width"))
      .def(
          "item_rect",
          [](const FileGridWidgetRef &self, size_t index) {
            return self.get().item_rect(index);
          },
          nb::arg("index"))
      .def(
          "select",
          [](const FileGridWidgetRef &self, size_t index, bool toggle,
             bool extend, bool additive) {
            const bool changed =
                self.get().select_index(index, toggle, extend, additive);
            self.widget.throw_pending_exception();
            return changed;
          },
          nb::arg("index"), nb::arg("toggle") = false,
          nb::arg("extend") = false, nb::arg("additive") = false)
      .def("clear_selection",
           [](const FileGridWidgetRef &self) {
             const bool changed = self.get().clear_selection();
             self.widget.throw_pending_exception();
             return changed;
           })
      .def(
          "ensure_visible",
          [](const FileGridWidgetRef &self, size_t index) {
            self.get().ensure_visible(index);
          },
          nb::arg("index"))
      .def(
          "connect_selection_changed",
          [](const FileGridWidgetRef &self, nb::object callback) {
            auto state = self.widget.state;
            return self.get().selection_changed().connect(
                [state, callback = std::move(callback)](
                    termin::gui_native::FileGridWidget &,
                    const std::vector<size_t> &selected) {
                  try {
                    nb::gil_scoped_acquire gil;
                    callback(selected);
                  } catch (...) {
                    if (state && !state->pending_exception) {
                      state->pending_exception = std::current_exception();
                    }
                    tc_log_error("[termin-gui-native/python] FileGridWidget "
                                 "selection callback failed");
                  }
                });
          },
          nb::arg("callback"))
      .def(
          "connect_activated",
          [](const FileGridWidgetRef &self, nb::object callback) {
            auto state = self.widget.state;
            return self.get().activated().connect(
                [state, callback = std::move(callback)](
                    termin::gui_native::FileGridWidget &, size_t index,
                    const termin::gui_native::CollectionItem &item) {
                  try {
                    nb::gil_scoped_acquire gil;
                    callback(index, item);
                  } catch (...) {
                    if (state && !state->pending_exception) {
                      state->pending_exception = std::current_exception();
                    }
                    tc_log_error("[termin-gui-native/python] FileGridWidget "
                                 "activation callback failed");
                  }
                });
          },
          nb::arg("callback"))
      .def(
          "connect_delete_requested",
          [](const FileGridWidgetRef &self, nb::object callback) {
            auto state = self.widget.state;
            return self.get().delete_requested().connect(
                [state, callback = std::move(callback)](
                    termin::gui_native::FileGridWidget &, size_t index,
                    const termin::gui_native::CollectionItem &item) {
                  try {
                    nb::gil_scoped_acquire gil;
                    callback(index, item);
                  } catch (...) {
                    if (state && !state->pending_exception) {
                      state->pending_exception = std::current_exception();
                    }
                    tc_log_error("[termin-gui-native/python] FileGridWidget "
                                 "delete callback failed");
                  }
                });
          },
          nb::arg("callback"))
      .def(
          "connect_context_menu_requested",
          [](const FileGridWidgetRef &self, nb::object callback) {
            auto state = self.widget.state;
            return self.get().context_menu_requested().connect(
                [state, callback = std::move(callback)](
                    termin::gui_native::FileGridWidget &, int64_t index,
                    float x, float y) {
                  try {
                    nb::gil_scoped_acquire gil;
                    callback(index, x, y);
                  } catch (...) {
                    if (state && !state->pending_exception) {
                      state->pending_exception = std::current_exception();
                    }
                    tc_log_error("[termin-gui-native/python] FileGridWidget "
                                 "context callback failed");
                  }
                });
          },
          nb::arg("callback"))
      .def(
          "connect_drag_requested",
          [](const FileGridWidgetRef &self, nb::object callback) {
            auto state = self.widget.state;
            return self.get().drag_requested().connect(
                [state, callback = std::move(callback)](
                    termin::gui_native::FileGridWidget &, size_t index, float x,
                    float y, int32_t modifiers) {
                  try {
                    nb::gil_scoped_acquire gil;
                    callback(index, x, y, modifiers);
                  } catch (...) {
                    if (state && !state->pending_exception)
                      state->pending_exception = std::current_exception();
                    tc_log_error("[termin-gui-native/python] FileGridWidget "
                                 "drag callback failed");
                  }
                });
          },
          nb::arg("callback"));

  nb::class_<termin::gui_native::TreeNode>(m, "TreeNode")
      .def_prop_ro(
          "id",
          [](const termin::gui_native::TreeNode &self) { return self.id; })
      .def_prop_ro(
          "item",
          [](const termin::gui_native::TreeNode &self) { return self.item; })
      .def_prop_ro(
          "parent",
          [](const termin::gui_native::TreeNode &self) { return self.parent; })
      .def_prop_ro("children", [](const termin::gui_native::TreeNode &self) {
        return self.children;
      });

  nb::class_<termin::gui_native::TreeVisibleRow>(m, "TreeVisibleRow")
      .def_prop_ro("node",
                   [](const termin::gui_native::TreeVisibleRow &self) {
                     return self.node;
                   })
      .def_prop_ro("depth", [](const termin::gui_native::TreeVisibleRow &self) {
        return self.depth;
      });

  nb::enum_<termin::gui_native::TreeDropPosition>(m, "TreeDropPosition")
      .value("Before", termin::gui_native::TreeDropPosition::Before)
      .value("Inside", termin::gui_native::TreeDropPosition::Inside)
      .value("After", termin::gui_native::TreeDropPosition::After)
      .value("Root", termin::gui_native::TreeDropPosition::Root);

  nb::class_<termin::gui_native::TreeModel>(m, "TreeModel")
      .def(nb::init<>())
      .def_prop_ro("node_count", &termin::gui_native::TreeModel::size)
      .def_prop_ro("revision", &termin::gui_native::TreeModel::revision)
      .def_prop_ro("roots",
                   [](const termin::gui_native::TreeModel &self) {
                     return self.roots();
                   })
      .def("contains", &termin::gui_native::TreeModel::contains,
           nb::arg("node"))
      .def(
          "node",
          [](const termin::gui_native::TreeModel &self,
             termin::gui_native::TreeNodeId node) { return self.node(node); },
          nb::arg("node"))
      .def(
          "children",
          [](const termin::gui_native::TreeModel &self,
             termin::gui_native::TreeNodeId parent) {
            return self.children(parent);
          },
          nb::arg("parent") = termin::gui_native::kInvalidTreeNodeId)
      .def("append_root", &termin::gui_native::TreeModel::append_root,
           nb::arg("item"))
      .def("append_child", &termin::gui_native::TreeModel::append_child,
           nb::arg("parent"), nb::arg("item"))
      .def("insert_child", &termin::gui_native::TreeModel::insert_child,
           nb::arg("parent"), nb::arg("index"), nb::arg("item"))
      .def("update", &termin::gui_native::TreeModel::update, nb::arg("node"),
           nb::arg("item"))
      .def("move", &termin::gui_native::TreeModel::move, nb::arg("node"),
           nb::arg("new_parent") = termin::gui_native::kInvalidTreeNodeId,
           nb::arg("index") = SIZE_MAX)
      .def("erase", &termin::gui_native::TreeModel::erase, nb::arg("node"))
      .def("clear", &termin::gui_native::TreeModel::clear);

  nb::class_<termin::gui_native::TreeExpansionModel>(m, "TreeExpansionModel")
      .def(nb::init<>())
      .def_prop_ro("revision",
                   &termin::gui_native::TreeExpansionModel::revision)
      .def("expanded", &termin::gui_native::TreeExpansionModel::expanded,
           nb::arg("node"))
      .def("set_expanded",
           &termin::gui_native::TreeExpansionModel::set_expanded,
           nb::arg("node"), nb::arg("expanded"))
      .def("toggle", &termin::gui_native::TreeExpansionModel::toggle,
           nb::arg("node"))
      .def("clear", &termin::gui_native::TreeExpansionModel::clear)
      .def(
          "reconcile",
          [](termin::gui_native::TreeExpansionModel &self,
             const termin::gui_native::TreeModel &model) {
            return self.reconcile(model);
          },
          nb::arg("model"));

  nb::class_<TreeWidgetRef>(m, "TreeWidget")
      .def_prop_ro("widget",
                   [](const TreeWidgetRef &self) { return self.widget; })
      .def_prop_ro("handle",
                   [](const TreeWidgetRef &self) {
                     return WidgetHandle{self.widget.handle};
                   })
      .def_prop_rw(
          "model", [](const TreeWidgetRef &self) { return self.get().model(); },
          [](const TreeWidgetRef &self,
             std::shared_ptr<termin::gui_native::TreeModel> model) {
            self.get().set_model(std::move(model));
          })
      .def_prop_rw(
          "expansion_model",
          [](const TreeWidgetRef &self) {
            return self.get().expansion_model();
          },
          [](const TreeWidgetRef &self,
             std::shared_ptr<termin::gui_native::TreeExpansionModel>
                 expansion) {
            self.get().set_expansion_model(std::move(expansion));
          })
      .def_prop_ro(
          "selected_node",
          [](const TreeWidgetRef &self) { return self.get().selected_node(); })
      .def_prop_rw(
          "draggable",
          [](const TreeWidgetRef &self) { return self.get().draggable(); },
          [](const TreeWidgetRef &self, bool value) {
            self.get().set_draggable(value);
          })
      .def_prop_ro(
          "dragging",
          [](const TreeWidgetRef &self) { return self.get().dragging(); })
      .def_prop_rw(
          "scroll_y",
          [](const TreeWidgetRef &self) { return self.get().scroll_y(); },
          [](const TreeWidgetRef &self, float value) {
            self.get().set_scroll_y(value);
          })
      .def_prop_ro(
          "content_height",
          [](const TreeWidgetRef &self) { return self.get().content_height(); })
      .def_prop_ro(
          "visible_count",
          [](const TreeWidgetRef &self) { return self.get().visible_count(); })
      .def_prop_ro("visible_range",
                   [](const TreeWidgetRef &self) {
                     const auto [first, last] = self.get().visible_range();
                     return std::vector<size_t>{first, last};
                   })
      .def(
          "visible_row",
          [](const TreeWidgetRef &self, size_t index) {
            return self.get().visible_row(index);
          },
          nb::arg("index"))
      .def(
          "set_row_height",
          [](const TreeWidgetRef &self, float height) {
            self.get().set_row_height(height);
          },
          nb::arg("height"))
      .def(
          "set_row_spacing",
          [](const TreeWidgetRef &self, float spacing) {
            self.get().set_row_spacing(spacing);
          },
          nb::arg("spacing"))
      .def(
          "set_indent_size",
          [](const TreeWidgetRef &self, float size) {
            self.get().set_indent_size(size);
          },
          nb::arg("size"))
      .def(
          "select",
          [](const TreeWidgetRef &self, termin::gui_native::TreeNodeId node,
             bool reveal) {
            const bool changed = self.get().select_node(node, reveal);
            self.widget.throw_pending_exception();
            return changed;
          },
          nb::arg("node"), nb::arg("reveal") = true)
      .def("clear_selection",
           [](const TreeWidgetRef &self) {
             const bool changed = self.get().clear_selection();
             self.widget.throw_pending_exception();
             return changed;
           })
      .def(
          "set_expanded",
          [](const TreeWidgetRef &self, termin::gui_native::TreeNodeId node,
             bool expanded) {
            const bool changed = self.get().set_expanded(node, expanded);
            self.widget.throw_pending_exception();
            return changed;
          },
          nb::arg("node"), nb::arg("expanded"))
      .def(
          "toggle",
          [](const TreeWidgetRef &self, termin::gui_native::TreeNodeId node) {
            const bool changed = self.get().toggle(node);
            self.widget.throw_pending_exception();
            return changed;
          },
          nb::arg("node"))
      .def(
          "expanded",
          [](const TreeWidgetRef &self, termin::gui_native::TreeNodeId node) {
            return self.get().expanded(node);
          },
          nb::arg("node"))
      .def(
          "ensure_visible",
          [](const TreeWidgetRef &self, termin::gui_native::TreeNodeId node) {
            self.get().ensure_visible(node);
            self.widget.throw_pending_exception();
          },
          nb::arg("node"))
      .def(
          "connect_selection_changed",
          [](const TreeWidgetRef &self, nb::object callback) {
            auto state = self.widget.state;
            return self.get().selection_changed().connect(
                [state, callback = std::move(callback)](
                    termin::gui_native::TreeWidget &,
                    termin::gui_native::TreeNodeId node) {
                  try {
                    nb::gil_scoped_acquire gil;
                    callback(node);
                  } catch (...) {
                    if (state && !state->pending_exception)
                      state->pending_exception = std::current_exception();
                    tc_log_error("[termin-gui-native/python] TreeWidget "
                                 "selection callback failed");
                  }
                });
          },
          nb::arg("callback"))
      .def(
          "connect_expansion_changed",
          [](const TreeWidgetRef &self, nb::object callback) {
            auto state = self.widget.state;
            return self.get().expansion_changed().connect(
                [state, callback = std::move(callback)](
                    termin::gui_native::TreeWidget &,
                    termin::gui_native::TreeNodeId node, bool expanded) {
                  try {
                    nb::gil_scoped_acquire gil;
                    callback(node, expanded);
                  } catch (...) {
                    if (state && !state->pending_exception)
                      state->pending_exception = std::current_exception();
                    tc_log_error("[termin-gui-native/python] TreeWidget "
                                 "expansion callback failed");
                  }
                });
          },
          nb::arg("callback"))
      .def(
          "connect_activated",
          [](const TreeWidgetRef &self, nb::object callback) {
            auto state = self.widget.state;
            return self.get().activated().connect(
                [state, callback = std::move(callback)](
                    termin::gui_native::TreeWidget &,
                    termin::gui_native::TreeNodeId node,
                    const termin::gui_native::CollectionItem &item) {
                  try {
                    nb::gil_scoped_acquire gil;
                    callback(node, item);
                  } catch (...) {
                    if (state && !state->pending_exception)
                      state->pending_exception = std::current_exception();
                    tc_log_error("[termin-gui-native/python] TreeWidget "
                                 "activation callback failed");
                  }
                });
          },
          nb::arg("callback"))
      .def(
          "connect_delete_requested",
          [](const TreeWidgetRef &self, nb::object callback) {
            auto state = self.widget.state;
            return self.get().delete_requested().connect(
                [state, callback = std::move(callback)](
                    termin::gui_native::TreeWidget &,
                    termin::gui_native::TreeNodeId node,
                    const termin::gui_native::CollectionItem &item) {
                  try {
                    nb::gil_scoped_acquire gil;
                    callback(node, item);
                  } catch (...) {
                    if (state && !state->pending_exception)
                      state->pending_exception = std::current_exception();
                    tc_log_error("[termin-gui-native/python] TreeWidget "
                                 "delete callback failed");
                  }
                });
          },
          nb::arg("callback"))
      .def(
          "connect_context_menu_requested",
          [](const TreeWidgetRef &self, nb::object callback) {
            auto state = self.widget.state;
            return self.get().context_menu_requested().connect(
                [state, callback = std::move(callback)](
                    termin::gui_native::TreeWidget &,
                    termin::gui_native::TreeNodeId node, float x, float y) {
                  try {
                    nb::gil_scoped_acquire gil;
                    callback(node, x, y);
                  } catch (...) {
                    if (state && !state->pending_exception)
                      state->pending_exception = std::current_exception();
                    tc_log_error("[termin-gui-native/python] TreeWidget "
                                 "context callback failed");
                  }
                });
          },
          nb::arg("callback"))
      .def(
          "connect_drop_requested",
          [](const TreeWidgetRef &self, nb::object callback) {
            auto state = self.widget.state;
            return self.get().drop_requested().connect(
                [state, callback = std::move(callback)](
                    termin::gui_native::TreeWidget &,
                    termin::gui_native::TreeNodeId dragged,
                    termin::gui_native::TreeNodeId target,
                    termin::gui_native::TreeDropPosition position) {
                  try {
                    nb::gil_scoped_acquire gil;
                    callback(dragged, target, position);
                  } catch (...) {
                    if (state && !state->pending_exception)
                      state->pending_exception = std::current_exception();
                    tc_log_error("[termin-gui-native/python] TreeWidget drop "
                                 "callback failed");
                  }
                });
          },
          nb::arg("callback"));

  nb::class_<termin::gui_native::TreeTableRowData>(m, "TreeTableRowData")
      .def(nb::init<>())
      .def(
          "__init__",
          [](termin::gui_native::TreeTableRowData *self, std::string stable_id,
             std::string parent_stable_id, std::vector<std::string> cells,
             bool enabled) {
            new (self) termin::gui_native::TreeTableRowData{
                std::move(stable_id), std::move(parent_stable_id),
                std::move(cells), enabled};
          },
          nb::arg("stable_id"), nb::arg("parent_stable_id"), nb::arg("cells"),
          nb::arg("enabled") = true)
      .def_rw("stable_id", &termin::gui_native::TreeTableRowData::stable_id)
      .def_rw("parent_stable_id",
              &termin::gui_native::TreeTableRowData::parent_stable_id)
      .def_rw("cells", &termin::gui_native::TreeTableRowData::cells)
      .def_rw("enabled", &termin::gui_native::TreeTableRowData::enabled);

  nb::class_<termin::gui_native::TreeTableNode>(m, "TreeTableNode")
      .def_prop_ro(
          "id",
          [](const termin::gui_native::TreeTableNode &self) { return self.id; })
      .def_prop_ro("data",
                   [](const termin::gui_native::TreeTableNode &self) {
                     return self.data;
                   })
      .def_prop_ro("parent",
                   [](const termin::gui_native::TreeTableNode &self) {
                     return self.parent;
                   })
      .def_prop_ro("children",
                   [](const termin::gui_native::TreeTableNode &self) {
                     return self.children;
                   });

  nb::class_<termin::gui_native::TreeTableModel>(m, "TreeTableModel")
      .def(nb::init<>())
      .def_prop_ro("node_count", &termin::gui_native::TreeTableModel::size)
      .def_prop_ro("revision", &termin::gui_native::TreeTableModel::revision)
      .def_prop_ro("roots",
                   [](const termin::gui_native::TreeTableModel &self) {
                     return self.roots();
                   })
      .def("contains", &termin::gui_native::TreeTableModel::contains,
           nb::arg("node"))
      .def("find", &termin::gui_native::TreeTableModel::find,
           nb::arg("stable_id"))
      .def(
          "node",
          [](const termin::gui_native::TreeTableModel &self,
             termin::gui_native::TreeTableNodeId node) {
            return self.node(node);
          },
          nb::arg("node"))
      .def(
          "children",
          [](const termin::gui_native::TreeTableModel &self,
             termin::gui_native::TreeTableNodeId parent) {
            return self.children(parent);
          },
          nb::arg("parent") = termin::gui_native::kInvalidTreeTableNodeId)
      .def("set_rows", &termin::gui_native::TreeTableModel::set_rows,
           nb::arg("rows"))
      .def("clear", &termin::gui_native::TreeTableModel::clear);

  nb::class_<termin::gui_native::TableRowData>(m, "TableRowData")
      .def(nb::init<>())
      .def(
          "__init__",
          [](termin::gui_native::TableRowData *self, std::string stable_id,
             std::vector<std::string> cells, bool enabled) {
            new (self) termin::gui_native::TableRowData{
                std::move(stable_id), std::move(cells), enabled};
          },
          nb::arg("stable_id"), nb::arg("cells"), nb::arg("enabled") = true)
      .def_rw("stable_id", &termin::gui_native::TableRowData::stable_id)
      .def_rw("cells", &termin::gui_native::TableRowData::cells)
      .def_rw("enabled", &termin::gui_native::TableRowData::enabled);

  nb::class_<termin::gui_native::TableRow>(m, "TableRow")
      .def_prop_ro(
          "id",
          [](const termin::gui_native::TableRow &self) { return self.id; })
      .def_prop_ro("data", [](const termin::gui_native::TableRow &self) {
        return self.data;
      });

  nb::class_<termin::gui_native::TableModel>(m, "TableModel")
      .def(nb::init<>())
      .def_prop_ro("row_count", &termin::gui_native::TableModel::size)
      .def_prop_ro("revision", &termin::gui_native::TableModel::revision)
      .def_prop_ro("rows",
                   [](const termin::gui_native::TableModel &self) {
                     return self.rows();
                   })
      .def("contains", &termin::gui_native::TableModel::contains,
           nb::arg("row"))
      .def("index_of", &termin::gui_native::TableModel::index_of,
           nb::arg("row"))
      .def(
          "row_at",
          [](const termin::gui_native::TableModel &self, size_t index) {
            return self.row_at(index);
          },
          nb::arg("index"))
      .def(
          "row",
          [](const termin::gui_native::TableModel &self,
             termin::gui_native::TableRowId row) { return self.row(row); },
          nb::arg("row"))
      .def("set_rows", &termin::gui_native::TableModel::set_rows,
           nb::arg("rows"))
      .def("append", &termin::gui_native::TableModel::append, nb::arg("row"))
      .def("insert", &termin::gui_native::TableModel::insert, nb::arg("index"),
           nb::arg("row"))
      .def("update", &termin::gui_native::TableModel::update, nb::arg("row"),
           nb::arg("data"))
      .def("erase", &termin::gui_native::TableModel::erase, nb::arg("row"))
      .def("clear", &termin::gui_native::TableModel::clear);

  nb::enum_<termin::gui_native::TableColumnPolicy>(m, "TableColumnPolicy")
      .value("Fixed", termin::gui_native::TableColumnPolicy::Fixed)
      .value("Stretch", termin::gui_native::TableColumnPolicy::Stretch);

  nb::class_<termin::gui_native::TableColumn>(m, "TableColumn")
      .def(nb::init<>())
      .def(
          "__init__",
          [](termin::gui_native::TableColumn *self, std::string stable_id,
             std::string header, termin::gui_native::TableColumnPolicy policy,
             float width, float min_width, float max_width, float stretch,
             bool resizable) {
            new (self) termin::gui_native::TableColumn{std::move(stable_id),
                                                       std::move(header),
                                                       policy,
                                                       width,
                                                       min_width,
                                                       max_width,
                                                       stretch,
                                                       resizable};
          },
          nb::arg("stable_id"), nb::arg("header"),
          nb::arg("policy") = termin::gui_native::TableColumnPolicy::Stretch,
          nb::arg("width") = 0.0f, nb::arg("min_width") = 40.0f,
          nb::arg("max_width") = 0.0f, nb::arg("stretch") = 1.0f,
          nb::arg("resizable") = true)
      .def_rw("stable_id", &termin::gui_native::TableColumn::stable_id)
      .def_rw("header", &termin::gui_native::TableColumn::header)
      .def_rw("policy", &termin::gui_native::TableColumn::policy)
      .def_rw("width", &termin::gui_native::TableColumn::width)
      .def_rw("min_width", &termin::gui_native::TableColumn::min_width)
      .def_rw("max_width", &termin::gui_native::TableColumn::max_width)
      .def_rw("stretch", &termin::gui_native::TableColumn::stretch)
      .def_rw("resizable", &termin::gui_native::TableColumn::resizable);

  nb::class_<termin::gui_native::TableColumnModel>(m, "TableColumnModel")
      .def(nb::init<>())
      .def_prop_ro("column_count", &termin::gui_native::TableColumnModel::size)
      .def_prop_ro("revision", &termin::gui_native::TableColumnModel::revision)
      .def_prop_ro("columns",
                   [](const termin::gui_native::TableColumnModel &self) {
                     return self.columns();
                   })
      .def(
          "column",
          [](const termin::gui_native::TableColumnModel &self, size_t index) {
            return self.column(index);
          },
          nb::arg("index"))
      .def("set_columns", &termin::gui_native::TableColumnModel::set_columns,
           nb::arg("columns"))
      .def("append", &termin::gui_native::TableColumnModel::append,
           nb::arg("column"))
      .def("insert", &termin::gui_native::TableColumnModel::insert,
           nb::arg("index"), nb::arg("column"))
      .def("update", &termin::gui_native::TableColumnModel::update,
           nb::arg("index"), nb::arg("column"))
      .def("resize", &termin::gui_native::TableColumnModel::resize,
           nb::arg("index"), nb::arg("width"))
      .def("erase", &termin::gui_native::TableColumnModel::erase,
           nb::arg("index"))
      .def("clear", &termin::gui_native::TableColumnModel::clear);

  nb::class_<termin::gui_native::TableColumnLayout>(m, "TableColumnLayout")
      .def_prop_ro("x",
                   [](const termin::gui_native::TableColumnLayout &self) {
                     return self.x;
                   })
      .def_prop_ro("width",
                   [](const termin::gui_native::TableColumnLayout &self) {
                     return self.width;
                   });

  nb::class_<TreeTableWidgetRef>(m, "TreeTableWidget")
      .def_prop_ro("widget",
                   [](const TreeTableWidgetRef &self) { return self.widget; })
      .def_prop_ro("handle",
                   [](const TreeTableWidgetRef &self) {
                     return WidgetHandle{self.widget.handle};
                   })
      .def_prop_rw(
          "model",
          [](const TreeTableWidgetRef &self) { return self.get().model(); },
          [](const TreeTableWidgetRef &self,
             std::shared_ptr<termin::gui_native::TreeTableModel> model) {
            self.get().set_model(std::move(model));
          })
      .def_prop_rw(
          "column_model",
          [](const TreeTableWidgetRef &self) {
            return self.get().column_model();
          },
          [](const TreeTableWidgetRef &self,
             std::shared_ptr<termin::gui_native::TableColumnModel> columns) {
            self.get().set_column_model(std::move(columns));
          })
      .def_prop_rw(
          "expansion_model",
          [](const TreeTableWidgetRef &self) {
            return self.get().expansion_model();
          },
          [](const TreeTableWidgetRef &self,
             std::shared_ptr<termin::gui_native::TreeExpansionModel>
                 expansion) {
            self.get().set_expansion_model(std::move(expansion));
          })
      .def_prop_ro("selected_node",
                   [](const TreeTableWidgetRef &self) {
                     return self.get().selected_node();
                   })
      .def_prop_rw(
          "scroll_y",
          [](const TreeTableWidgetRef &self) { return self.get().scroll_y(); },
          [](const TreeTableWidgetRef &self, float value) {
            self.get().set_scroll_y(value);
          })
      .def_prop_ro("content_height",
                   [](const TreeTableWidgetRef &self) {
                     return self.get().content_height();
                   })
      .def_prop_ro("visible_count",
                   [](const TreeTableWidgetRef &self) {
                     return self.get().visible_count();
                   })
      .def_prop_ro("visible_range",
                   [](const TreeTableWidgetRef &self) {
                     const auto [first, last] = self.get().visible_range();
                     return std::vector<size_t>{first, last};
                   })
      .def_prop_ro("column_layout",
                   [](const TreeTableWidgetRef &self) {
                     return self.get().column_layout();
                   })
      .def(
          "visible_row",
          [](const TreeTableWidgetRef &self, size_t index) {
            return self.get().visible_row(index);
          },
          nb::arg("index"))
      .def(
          "set_row_height",
          [](const TreeTableWidgetRef &self, float height) {
            self.get().set_row_height(height);
          },
          nb::arg("height"))
      .def(
          "set_header_height",
          [](const TreeTableWidgetRef &self, float height) {
            self.get().set_header_height(height);
          },
          nb::arg("height"))
      .def(
          "set_indent_size",
          [](const TreeTableWidgetRef &self, float size) {
            self.get().set_indent_size(size);
          },
          nb::arg("size"))
      .def(
          "select",
          [](const TreeTableWidgetRef &self,
             termin::gui_native::TreeTableNodeId node, bool reveal) {
            const bool changed = self.get().select_node(node, reveal);
            self.widget.throw_pending_exception();
            return changed;
          },
          nb::arg("node"), nb::arg("reveal") = true)
      .def("clear_selection",
           [](const TreeTableWidgetRef &self) {
             const bool changed = self.get().clear_selection();
             self.widget.throw_pending_exception();
             return changed;
           })
      .def(
          "set_expanded",
          [](const TreeTableWidgetRef &self,
             termin::gui_native::TreeTableNodeId node, bool expanded) {
            const bool changed = self.get().set_expanded(node, expanded);
            self.widget.throw_pending_exception();
            return changed;
          },
          nb::arg("node"), nb::arg("expanded"))
      .def(
          "toggle",
          [](const TreeTableWidgetRef &self,
             termin::gui_native::TreeTableNodeId node) {
            const bool changed = self.get().toggle(node);
            self.widget.throw_pending_exception();
            return changed;
          },
          nb::arg("node"))
      .def(
          "expanded",
          [](const TreeTableWidgetRef &self,
             termin::gui_native::TreeTableNodeId node) {
            return self.get().expanded(node);
          },
          nb::arg("node"))
      .def(
          "ensure_visible",
          [](const TreeTableWidgetRef &self,
             termin::gui_native::TreeTableNodeId node) {
            self.get().ensure_visible(node);
            self.widget.throw_pending_exception();
          },
          nb::arg("node"))
      .def(
          "connect_selection_changed",
          [](const TreeTableWidgetRef &self, nb::object callback) {
            auto state = self.widget.state;
            return self.get().selection_changed().connect(
                [state, callback = std::move(callback)](
                    termin::gui_native::TreeTableWidget &,
                    termin::gui_native::TreeTableNodeId node) {
                  try {
                    nb::gil_scoped_acquire gil;
                    callback(node);
                  } catch (...) {
                    if (state && !state->pending_exception)
                      state->pending_exception = std::current_exception();
                    tc_log_error("[termin-gui-native/python] TreeTableWidget "
                                 "selection callback failed");
                  }
                });
          },
          nb::arg("callback"))
      .def(
          "connect_expansion_changed",
          [](const TreeTableWidgetRef &self, nb::object callback) {
            auto state = self.widget.state;
            return self.get().expansion_changed().connect(
                [state, callback = std::move(callback)](
                    termin::gui_native::TreeTableWidget &,
                    termin::gui_native::TreeTableNodeId node, bool expanded) {
                  try {
                    nb::gil_scoped_acquire gil;
                    callback(node, expanded);
                  } catch (...) {
                    if (state && !state->pending_exception)
                      state->pending_exception = std::current_exception();
                    tc_log_error("[termin-gui-native/python] TreeTableWidget "
                                 "expansion callback failed");
                  }
                });
          },
          nb::arg("callback"));

  nb::class_<TableWidgetRef>(m, "TableWidget")
      .def_prop_ro("widget",
                   [](const TableWidgetRef &self) { return self.widget; })
      .def_prop_ro("handle",
                   [](const TableWidgetRef &self) {
                     return WidgetHandle{self.widget.handle};
                   })
      .def_prop_rw(
          "model",
          [](const TableWidgetRef &self) { return self.get().model(); },
          [](const TableWidgetRef &self,
             std::shared_ptr<termin::gui_native::TableModel> model) {
            self.get().set_model(std::move(model));
          })
      .def_prop_rw(
          "column_model",
          [](const TableWidgetRef &self) { return self.get().column_model(); },
          [](const TableWidgetRef &self,
             std::shared_ptr<termin::gui_native::TableColumnModel> columns) {
            self.get().set_column_model(std::move(columns));
          })
      .def_prop_rw(
          "selection_mode",
          [](const TableWidgetRef &self) {
            return self.get().selection().mode();
          },
          [](const TableWidgetRef &self,
             termin::gui_native::SelectionMode mode) {
            self.get().set_selection_mode(mode);
            self.widget.throw_pending_exception();
          })
      .def_prop_ro("selected_indices",
                   [](const TableWidgetRef &self) {
                     return self.get().selection().selected_indices();
                   })
      .def_prop_ro("current_index",
                   [](const TableWidgetRef &self) -> int64_t {
                     const size_t index = self.get().selection().current();
                     return index == termin::gui_native::SelectionModel::npos
                                ? -1
                                : static_cast<int64_t>(index);
                   })
      .def_prop_rw(
          "scroll_y",
          [](const TableWidgetRef &self) { return self.get().scroll_y(); },
          [](const TableWidgetRef &self, float value) {
            self.get().set_scroll_y(value);
          })
      .def_prop_ro("content_height",
                   [](const TableWidgetRef &self) {
                     return self.get().content_height();
                   })
      .def_prop_ro("visible_range",
                   [](const TableWidgetRef &self) {
                     const auto [first, last] = self.get().visible_range();
                     return std::vector<size_t>{first, last};
                   })
      .def_prop_ro(
          "column_layout",
          [](const TableWidgetRef &self) { return self.get().column_layout(); })
      .def(
          "set_row_height",
          [](const TableWidgetRef &self, float height) {
            self.get().set_row_height(height);
          },
          nb::arg("height"))
      .def(
          "set_header_height",
          [](const TableWidgetRef &self, float height) {
            self.get().set_header_height(height);
          },
          nb::arg("height"))
      .def(
          "select",
          [](const TableWidgetRef &self, size_t index, bool toggle, bool extend,
             bool additive) {
            const bool changed =
                self.get().select_row(index, toggle, extend, additive);
            self.widget.throw_pending_exception();
            return changed;
          },
          nb::arg("index"), nb::arg("toggle") = false,
          nb::arg("extend") = false, nb::arg("additive") = false)
      .def("clear_selection",
           [](const TableWidgetRef &self) {
             const bool changed = self.get().clear_selection();
             self.widget.throw_pending_exception();
             return changed;
           })
      .def(
          "ensure_visible",
          [](const TableWidgetRef &self, size_t index) {
            self.get().ensure_visible(index);
          },
          nb::arg("index"))
      .def(
          "connect_selection_changed",
          [](const TableWidgetRef &self, nb::object callback) {
            auto state = self.widget.state;
            return self.get().selection_changed().connect(
                [state, callback = std::move(callback)](
                    termin::gui_native::TableWidget &,
                    const std::vector<size_t> &selected) {
                  try {
                    nb::gil_scoped_acquire gil;
                    callback(selected);
                  } catch (...) {
                    if (state && !state->pending_exception) {
                      state->pending_exception = std::current_exception();
                    }
                    tc_log_error("[termin-gui-native/python] TableWidget "
                                 "selection callback failed");
                  }
                });
          },
          nb::arg("callback"))
      .def(
          "connect_activated",
          [](const TableWidgetRef &self, nb::object callback) {
            auto state = self.widget.state;
            return self.get().activated().connect(
                [state, callback = std::move(callback)](
                    termin::gui_native::TableWidget &, size_t index,
                    termin::gui_native::TableRowId row,
                    const termin::gui_native::TableRowData &data) {
                  try {
                    nb::gil_scoped_acquire gil;
                    callback(index, row, data);
                  } catch (...) {
                    if (state && !state->pending_exception) {
                      state->pending_exception = std::current_exception();
                    }
                    tc_log_error("[termin-gui-native/python] TableWidget "
                                 "activation callback failed");
                  }
                });
          },
          nb::arg("callback"))
      .def(
          "connect_header_clicked",
          [](const TableWidgetRef &self, nb::object callback) {
            auto state = self.widget.state;
            return self.get().header_clicked().connect(
                [state, callback = std::move(callback)](
                    termin::gui_native::TableWidget &, size_t index,
                    const termin::gui_native::TableColumn &column) {
                  try {
                    nb::gil_scoped_acquire gil;
                    callback(index, column);
                  } catch (...) {
                    if (state && !state->pending_exception) {
                      state->pending_exception = std::current_exception();
                    }
                    tc_log_error("[termin-gui-native/python] TableWidget "
                                 "header callback failed");
                  }
                });
          },
          nb::arg("callback"))
      .def(
          "connect_column_resized",
          [](const TableWidgetRef &self, nb::object callback) {
            auto state = self.widget.state;
            return self.get().column_resized().connect(
                [state, callback = std::move(callback)](
                    termin::gui_native::TableWidget &, size_t index,
                    float width) {
                  try {
                    nb::gil_scoped_acquire gil;
                    callback(index, width);
                  } catch (...) {
                    if (state && !state->pending_exception) {
                      state->pending_exception = std::current_exception();
                    }
                    tc_log_error("[termin-gui-native/python] TableWidget "
                                 "resize callback failed");
                  }
                });
          },
          nb::arg("callback"))
      .def(
          "connect_context_menu_requested",
          [](const TableWidgetRef &self, nb::object callback) {
            auto state = self.widget.state;
            return self.get().context_menu_requested().connect(
                [state, callback = std::move(callback)](
                    termin::gui_native::TableWidget &, int64_t index, float x,
                    float y) {
                  try {
                    nb::gil_scoped_acquire gil;
                    callback(index, x, y);
                  } catch (...) {
                    if (state && !state->pending_exception)
                      state->pending_exception = std::current_exception();
                    tc_log_error("[termin-gui-native/python] TableWidget "
                                 "context callback failed");
                  }
                });
          },
          nb::arg("callback"));
}
