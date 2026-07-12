#include "gui_native_bindings_shared.hpp"

using namespace termin::gui_native::python_bindings;

void bind_gui_native_commands_and_dialogs(nb::module_& m) {
    nb::class_<termin::gui_native::CommandData>(m, "CommandData")
        .def(nb::init<>())
        .def(
            "__init__",
            [](termin::gui_native::CommandData* self, std::string stable_id, std::string label,
               std::string icon, std::string shortcut, std::string tooltip,
               termin::gui_native::CommandKind kind, bool enabled, bool checkable, bool checked,
               uint32_t texture_id, std::shared_ptr<termin::gui_native::CommandModel> submenu) {
                new (self) termin::gui_native::CommandData{std::move(stable_id),
                                                           std::move(label),
                                                           std::move(icon),
                                                           std::move(shortcut),
                                                           std::move(tooltip),
                                                           kind,
                                                           enabled,
                                                           checkable,
                                                           checked,
                                                           texture_id,
                                                           std::move(submenu)};
            },
            nb::arg("stable_id"), nb::arg("label") = "", nb::arg("icon") = "",
            nb::arg("shortcut") = "", nb::arg("tooltip") = "",
            nb::arg("kind") = termin::gui_native::CommandKind::Action, nb::arg("enabled") = true,
            nb::arg("checkable") = false, nb::arg("checked") = false, nb::arg("texture_id") = 0,
            nb::arg("submenu") = nullptr)
        .def_rw("stable_id", &termin::gui_native::CommandData::stable_id)
        .def_rw("label", &termin::gui_native::CommandData::label)
        .def_rw("icon", &termin::gui_native::CommandData::icon)
        .def_rw("shortcut", &termin::gui_native::CommandData::shortcut)
        .def_rw("tooltip", &termin::gui_native::CommandData::tooltip)
        .def_rw("kind", &termin::gui_native::CommandData::kind)
        .def_rw("enabled", &termin::gui_native::CommandData::enabled)
        .def_rw("checkable", &termin::gui_native::CommandData::checkable)
        .def_rw("checked", &termin::gui_native::CommandData::checked)
        .def_rw("texture_id", &termin::gui_native::CommandData::texture_id)
        .def_rw("submenu", &termin::gui_native::CommandData::submenu);

    nb::class_<termin::gui_native::Command>(m, "Command")
        .def_prop_ro("id", [](const termin::gui_native::Command& self) { return self.id; })
        .def_prop_ro("data", [](const termin::gui_native::Command& self) { return self.data; });

    nb::class_<termin::gui_native::CommandModel>(m, "CommandModel")
        .def(nb::init<>())
        .def_prop_ro("command_count", &termin::gui_native::CommandModel::size)
        .def_prop_ro("revision", &termin::gui_native::CommandModel::revision)
        .def_prop_ro("commands",
                     [](const termin::gui_native::CommandModel& self) { return self.commands(); })
        .def("contains", &termin::gui_native::CommandModel::contains, nb::arg("command"))
        .def("index_of", &termin::gui_native::CommandModel::index_of, nb::arg("command"))
        .def(
            "command_at",
            [](const termin::gui_native::CommandModel& self, size_t index) {
                return self.command_at(index);
            },
            nb::arg("index"))
        .def(
            "command",
            [](const termin::gui_native::CommandModel& self,
               termin::gui_native::CommandId command) { return self.command(command); },
            nb::arg("command"))
        .def("set_commands", &termin::gui_native::CommandModel::set_commands, nb::arg("commands"))
        .def("append", &termin::gui_native::CommandModel::append, nb::arg("command"))
        .def("insert", &termin::gui_native::CommandModel::insert, nb::arg("index"),
             nb::arg("command"))
        .def("update", &termin::gui_native::CommandModel::update, nb::arg("command"),
             nb::arg("data"))
        .def("set_enabled", &termin::gui_native::CommandModel::set_enabled, nb::arg("command"),
             nb::arg("enabled"))
        .def("set_checked", &termin::gui_native::CommandModel::set_checked, nb::arg("command"),
             nb::arg("checked"))
        .def("erase", &termin::gui_native::CommandModel::erase, nb::arg("command"))
        .def("clear", &termin::gui_native::CommandModel::clear);

    nb::class_<ToolBarRef>(m, "ToolBar")
        .def_prop_ro("widget", [](const ToolBarRef& self) { return self.widget; })
        .def_prop_ro("handle",
                     [](const ToolBarRef& self) { return WidgetHandle{self.widget.handle}; })
        .def_prop_rw(
            "model", [](const ToolBarRef& self) { return self.get().model(); },
            [](const ToolBarRef& self, std::shared_ptr<termin::gui_native::CommandModel> model) {
                self.get().set_model(std::move(model));
            })
        .def_prop_ro("item_rects", [](const ToolBarRef& self) { return self.get().item_rects(); })
        .def_prop_ro("hovered_index",
                     [](const ToolBarRef& self) -> int64_t {
                         const size_t index = self.get().hovered_index();
                         return index == SIZE_MAX ? -1 : static_cast<int64_t>(index);
                     })
        .def_prop_ro("hovered_tooltip",
                     [](const ToolBarRef& self) { return self.get().hovered_tooltip(); })
        .def_prop_rw(
            "item_height", [](const ToolBarRef& self) { return self.get().item_height(); },
            [](const ToolBarRef& self, float value) { self.get().set_item_height(value); })
        .def_prop_rw(
            "padding", [](const ToolBarRef& self) { return self.get().padding(); },
            [](const ToolBarRef& self, float value) { self.get().set_padding(value); })
        .def_prop_rw(
            "centered", [](const ToolBarRef& self) { return self.get().centered(); },
            [](const ToolBarRef& self, bool value) { self.get().set_centered(value); })
        .def(
            "connect_activated",
            [](const ToolBarRef& self, nb::object callback) {
                auto state = self.widget.state;
                return self.get().activated().connect(
                    [state, callback = std::move(callback)](
                        termin::gui_native::ToolBar&, size_t index,
                        termin::gui_native::CommandId id,
                        const termin::gui_native::CommandData& command) {
                        try {
                            nb::gil_scoped_acquire gil;
                            callback(index, id, command);
                        } catch (...) {
                            if (state && !state->pending_exception) {
                                state->pending_exception = std::current_exception();
                            }
                            tc_log_error(
                                "[termin-gui-native/python] ToolBar activation callback failed");
                        }
                    });
            },
            nb::arg("callback"));

    nb::class_<TabViewRef>(m, "TabView")
        .def_prop_ro("widget",
                     [](const TabViewRef &self) { return self.widget; })
        .def_prop_ro("handle",
                     [](const TabViewRef &self) {
                       return WidgetHandle{self.widget.handle};
                     })
        .def_prop_ro(
            "page_count",
            [](const TabViewRef &self) { return self.get().page_count(); })
        .def_prop_rw(
            "selected_index",
            [](const TabViewRef &self) { return self.get().selected_index(); },
            [](const TabViewRef &self, size_t index) {
              if (index >= self.get().page_count()) {
                throw std::out_of_range("TabView selected_index out of range");
              }
              self.get().set_selected_index(index);
              self.widget.throw_pending_exception();
            })
        .def(
            "add_page",
            [](const TabViewRef &self, const std::string &title,
               const WidgetRef &page) {
              if (self.widget.state != page.state) {
                throw std::invalid_argument(
                    "TabView page belongs to another document");
              }
              self.get().add_page(title, page.handle);
              self.widget.throw_pending_exception();
            },
            nb::arg("title"), nb::arg("page"))
        .def(
            "remove_page",
            [](const TabViewRef &self, size_t index) {
              const bool removed = self.get().remove_page(index);
              self.widget.throw_pending_exception();
              return removed;
            },
            nb::arg("index"))
        .def(
            "set_page_title",
            [](const TabViewRef &self, size_t index, const std::string &title) {
              if (!self.get().set_page_title(index, title)) {
                throw std::out_of_range("TabView page index out of range");
              }
            },
            nb::arg("index"), nb::arg("title"))
        .def(
            "page_title",
            [](const TabViewRef &self, size_t index) {
              return self.get().page_title(index);
            },
            nb::arg("index"))
        .def(
            "page_handle",
            [](const TabViewRef &self, size_t index) {
              return WidgetHandle{self.get().page_handle(index)};
            },
            nb::arg("index"))
        .def(
            "connect_selection_changed",
            [](const TabViewRef &self, nb::object callback) {
              auto state = self.widget.state;
              return self.get().selection_changed().connect(
                  [state, callback = std::move(callback)](
                      termin::gui_native::TabView &, size_t index) {
                    try {
                      nb::gil_scoped_acquire gil;
                      callback(index);
                    } catch (...) {
                      if (state && !state->pending_exception) {
                        state->pending_exception = std::current_exception();
                      }
                      tc_log_error("[termin-gui-native/python] TabView "
                                   "selection callback failed");
                    }
                  });
            },
            nb::arg("callback"));

    nb::class_<MenuRef>(m, "Menu")
        .def_prop_ro("widget", [](const MenuRef& self) { return self.widget; })
        .def_prop_ro("handle", [](const MenuRef& self) { return WidgetHandle{self.widget.handle}; })
        .def_prop_rw(
            "model", [](const MenuRef& self) { return self.get().model(); },
            [](const MenuRef& self, std::shared_ptr<termin::gui_native::CommandModel> model) {
                self.get().set_model(std::move(model));
            })
        .def_prop_ro("open", [](const MenuRef& self) { return self.get().open(); })
        .def_prop_ro("current_index", [](const MenuRef& self) -> int64_t {
            const size_t index = self.get().current_index();
            return index == SIZE_MAX ? -1 : static_cast<int64_t>(index);
        })
        .def_prop_ro("scroll_offset", [](const MenuRef& self) { return self.get().scroll_offset(); })
        .def_prop_ro("content_height", [](const MenuRef& self) { return self.get().content_height(); })
        .def_prop_rw(
            "max_visible_height", [](const MenuRef& self) { return self.get().max_visible_height(); },
            [](const MenuRef& self, float value) { self.get().set_max_visible_height(value); })
        .def("show", [](const MenuRef& self, tc_ui_point position, tc_ui_rect viewport,
                         bool dismiss_on_outside) {
            return self.get().show(self.widget.state->document, position, viewport,
                                   dismiss_on_outside);
        }, nb::arg("position"), nb::arg("viewport"), nb::arg("dismiss_on_outside") = true)
        .def("dismiss", [](const MenuRef& self, tc_ui_overlay_dismiss_reason reason) {
            return self.get().dismiss(self.widget.state->document, reason);
        }, nb::arg("reason") = TC_UI_OVERLAY_DISMISS_PROGRAMMATIC)
        .def("connect_activated", [](const MenuRef& self, nb::object callback) {
            auto state = self.widget.state;
            return self.get().activated().connect(
                [state, callback = std::move(callback)](termin::gui_native::Menu&, size_t index,
                    termin::gui_native::CommandId id,
                    const termin::gui_native::CommandData& command) {
                    try {
                        nb::gil_scoped_acquire gil;
                        callback(index, id, command);
                    } catch (...) {
                        if (state && !state->pending_exception)
                            state->pending_exception = std::current_exception();
                        tc_log_error("[termin-gui-native/python] Menu activation callback failed");
                    }
                });
        }, nb::arg("callback"));

    nb::class_<termin::gui_native::MenuBarEntry>(m, "MenuBarEntry")
        .def(nb::init<std::string, std::string,
                      std::shared_ptr<termin::gui_native::CommandModel>>(),
             nb::arg("stable_id"), nb::arg("label"), nb::arg("menu"))
        .def_rw("stable_id", &termin::gui_native::MenuBarEntry::stable_id)
        .def_rw("label", &termin::gui_native::MenuBarEntry::label)
        .def_rw("menu", &termin::gui_native::MenuBarEntry::menu);

    nb::class_<MenuBarRef>(m, "MenuBar")
        .def_prop_ro("widget", [](const MenuBarRef& self) { return self.widget; })
        .def_prop_ro("handle", [](const MenuBarRef& self) { return WidgetHandle{self.widget.handle}; })
        .def_prop_rw("entries", [](const MenuBarRef& self) { return self.get().entries(); },
                     [](const MenuBarRef& self,
                        std::vector<termin::gui_native::MenuBarEntry> entries) {
                         self.get().set_entries(std::move(entries));
                     })
        .def_prop_ro("item_rects", [](const MenuBarRef& self) { return self.get().item_rects(); })
        .def_prop_ro("menu_open", [](const MenuBarRef& self) { return self.get().menu_open(); })
        .def_prop_ro("open_index", [](const MenuBarRef& self) -> int64_t {
            const size_t index = self.get().open_index();
            return index == SIZE_MAX ? -1 : static_cast<int64_t>(index);
        })
        .def("add_menu", [](const MenuBarRef& self, termin::gui_native::MenuBarEntry entry) {
            self.get().add_menu(std::move(entry));
        }, nb::arg("entry"))
        .def("clear", [](const MenuBarRef& self) { self.get().clear(); })
        .def("dispatch_shortcut", [](const MenuBarRef& self, int32_t key, int32_t modifiers) {
            return self.get().dispatch_shortcut(key, modifiers);
        }, nb::arg("key"), nb::arg("modifiers") = 0)
        .def("connect_activated", [](const MenuBarRef& self, nb::object callback) {
            auto state = self.widget.state;
            return self.get().activated().connect(
                [state, callback = std::move(callback)](termin::gui_native::MenuBar&, size_t menu,
                    termin::gui_native::CommandId id,
                    const termin::gui_native::CommandData& command) {
                    try {
                        nb::gil_scoped_acquire gil;
                        callback(menu, id, command);
                    } catch (...) {
                        if (state && !state->pending_exception)
                            state->pending_exception = std::current_exception();
                        tc_log_error("[termin-gui-native/python] MenuBar activation callback failed");
                    }
                });
        }, nb::arg("callback"));

    nb::class_<StatusBarRef>(m, "StatusBar")
        .def_prop_ro("widget", [](const StatusBarRef& self) { return self.widget; })
        .def_prop_ro("handle",
                     [](const StatusBarRef& self) { return WidgetHandle{self.widget.handle}; })
        .def_prop_rw(
            "text", [](const StatusBarRef& self) { return self.get().text(); },
            [](const StatusBarRef& self, std::string value) {
                self.get().set_text(std::move(value));
            })
        .def_prop_ro("message", [](const StatusBarRef& self) { return self.get().message(); })
        .def_prop_ro("has_message",
                     [](const StatusBarRef& self) { return self.get().has_message(); })
        .def_prop_ro("displayed_text",
                     [](const StatusBarRef& self) { return self.get().displayed_text(); })
        .def(
            "show_message",
            [](const StatusBarRef& self, std::string message) {
                self.get().show_message(std::move(message));
            },
            nb::arg("message"))
        .def("clear_message", [](const StatusBarRef& self) { self.get().clear_message(); });

    nb::enum_<termin::gui_native::DialogDismissReason>(m, "DialogDismissReason")
        .value("Action", termin::gui_native::DialogDismissReason::Action)
        .value("Escape", termin::gui_native::DialogDismissReason::Escape)
        .value("Programmatic", termin::gui_native::DialogDismissReason::Programmatic);

    nb::enum_<termin::gui_native::FileDialogMode>(m, "FileDialogMode")
        .value("OpenFile", termin::gui_native::FileDialogMode::OpenFile)
        .value("SaveFile", termin::gui_native::FileDialogMode::SaveFile)
        .value("OpenDirectory", termin::gui_native::FileDialogMode::OpenDirectory);

    nb::class_<termin::gui_native::FileDialogFilter>(m, "FileDialogFilter")
        .def(nb::init<std::string, std::vector<std::string>>(), nb::arg("label"),
             nb::arg("patterns"))
        .def_rw("label", &termin::gui_native::FileDialogFilter::label)
        .def_rw("patterns", &termin::gui_native::FileDialogFilter::patterns);

    nb::class_<termin::gui_native::FileDialogEntry>(m, "FileDialogEntry")
        .def_prop_ro("name", [](const termin::gui_native::FileDialogEntry& self) {
            return self.name;
        })
        .def_prop_ro("path", [](const termin::gui_native::FileDialogEntry& self) {
            return self.path;
        })
        .def_prop_ro("is_directory", [](const termin::gui_native::FileDialogEntry& self) {
            return self.is_directory;
        })
        .def_prop_ro("size", [](const termin::gui_native::FileDialogEntry& self) {
            return self.size;
        })
        .def_prop_ro("modified_time", [](const termin::gui_native::FileDialogEntry& self) {
            return self.modified_time;
        });

    nb::class_<termin::gui_native::FileDialogConfirmResult>(m, "FileDialogConfirmResult")
        .def_prop_ro("path", [](const termin::gui_native::FileDialogConfirmResult& self) {
            return self.path;
        })
        .def_prop_ro("error", [](const termin::gui_native::FileDialogConfirmResult& self) {
            return self.error;
        });

    nb::class_<termin::gui_native::FileDialogModel>(m, "FileDialogModel")
        .def(nb::init<termin::gui_native::FileDialogMode>(), nb::arg("mode"))
        .def_prop_ro("mode", &termin::gui_native::FileDialogModel::mode)
        .def_prop_ro("current_directory",
                     &termin::gui_native::FileDialogModel::current_directory)
        .def_prop_ro("entries", &termin::gui_native::FileDialogModel::entries)
        .def_prop_ro("filters", &termin::gui_native::FileDialogModel::filters)
        .def_prop_ro("selected_filter",
                     &termin::gui_native::FileDialogModel::selected_filter)
        .def_prop_ro("selected_index", &termin::gui_native::FileDialogModel::selected_index)
        .def_prop_rw("file_name", &termin::gui_native::FileDialogModel::file_name,
                     &termin::gui_native::FileDialogModel::set_file_name)
        .def_prop_ro("error", &termin::gui_native::FileDialogModel::error)
        .def_prop_ro("can_go_back", &termin::gui_native::FileDialogModel::can_go_back)
        .def_prop_ro("can_go_forward", &termin::gui_native::FileDialogModel::can_go_forward)
        .def_static("parse_filter_string",
                    &termin::gui_native::FileDialogModel::parse_filter_string,
                    nb::arg("text"))
        .def("set_filters", &termin::gui_native::FileDialogModel::set_filters,
             nb::arg("filters"))
        .def("set_filter", &termin::gui_native::FileDialogModel::set_filter, nb::arg("index"))
        .def("navigate", &termin::gui_native::FileDialogModel::navigate, nb::arg("path"),
             nb::arg("push_history") = true)
        .def("go_back", &termin::gui_native::FileDialogModel::go_back)
        .def("go_forward", &termin::gui_native::FileDialogModel::go_forward)
        .def("go_up", &termin::gui_native::FileDialogModel::go_up)
        .def("refresh", &termin::gui_native::FileDialogModel::refresh)
        .def("select", &termin::gui_native::FileDialogModel::select, nb::arg("index"))
        .def("confirm", &termin::gui_native::FileDialogModel::confirm)
        .def("create_directory", &termin::gui_native::FileDialogModel::create_directory,
             nb::arg("name"));

    nb::class_<termin::gui_native::DialogAction>(m, "DialogAction")
        .def(nb::init<std::string, std::string, bool, bool>(), nb::arg("stable_id"),
             nb::arg("label"), nb::arg("is_default") = false, nb::arg("is_cancel") = false)
        .def_rw("stable_id", &termin::gui_native::DialogAction::stable_id)
        .def_rw("label", &termin::gui_native::DialogAction::label)
        .def_rw("is_default", &termin::gui_native::DialogAction::is_default)
        .def_rw("is_cancel", &termin::gui_native::DialogAction::is_cancel);

    nb::class_<termin::gui_native::DialogResult>(m, "DialogResult")
        .def_prop_ro("action_id", [](const termin::gui_native::DialogResult& self) {
            return self.action_id;
        })
        .def_prop_ro("reason", [](const termin::gui_native::DialogResult& self) {
            return self.reason;
        });

    nb::class_<DialogRef>(m, "Dialog")
        .def_prop_ro("widget", [](const DialogRef& self) { return self.widget; })
        .def_prop_ro("handle", [](const DialogRef& self) { return WidgetHandle{self.widget.handle}; })
        .def_prop_rw("title", [](const DialogRef& self) { return self.get().title(); },
                     [](const DialogRef& self, std::string title) {
                         self.get().set_title(std::move(title));
                     })
        .def_prop_rw("actions", [](const DialogRef& self) { return self.get().actions(); },
                     [](const DialogRef& self,
                        std::vector<termin::gui_native::DialogAction> actions) {
                         self.get().set_actions(std::move(actions));
                     })
        .def_prop_rw("dismiss_on_escape",
                     [](const DialogRef& self) { return self.get().dismiss_on_escape(); },
                     [](const DialogRef& self, bool enabled) {
                         self.get().set_dismiss_on_escape(enabled);
                     })
        .def_prop_ro("open", [](const DialogRef& self) { return self.get().open(); })
        .def("set_content", [](const DialogRef& self, const WidgetRef& content) {
            self.get().set_content(
                native_widget_checked<termin::gui_native::NativeWidget>(content, "NativeWidget"));
        }, nb::arg("content"))
        .def("show", [](const DialogRef& self, tc_ui_rect viewport) {
            return self.get().show(self.widget.state->document, viewport);
        }, nb::arg("viewport"))
        .def("close", [](const DialogRef& self) {
            return self.get().close(self.widget.state->document);
        })
        .def("activate", [](const DialogRef& self, const std::string& action_id) {
            return self.get().activate(action_id, self.widget.state->document);
        }, nb::arg("action_id"))
        .def("connect_finished", [](const DialogRef& self, nb::object callback) {
            auto state = self.widget.state;
            return self.get().finished().connect(
                [state, callback = std::move(callback)](termin::gui_native::Dialog&,
                    const termin::gui_native::DialogResult& result) {
                    try {
                        nb::gil_scoped_acquire gil;
                        callback(result);
                    } catch (...) {
                        if (state && !state->pending_exception)
                            state->pending_exception = std::current_exception();
                        tc_log_error("[termin-gui-native/python] Dialog finished callback failed");
                    }
                });
        }, nb::arg("callback"));

    nb::enum_<termin::gui_native::MessageBoxKind>(m, "MessageBoxKind")
        .value("Information", termin::gui_native::MessageBoxKind::Information)
        .value("Warning", termin::gui_native::MessageBoxKind::Warning)
        .value("Error", termin::gui_native::MessageBoxKind::Error)
        .value("Question", termin::gui_native::MessageBoxKind::Question);

    nb::class_<MessageBoxRef>(m, "MessageBox")
        .def_prop_ro("widget", [](const MessageBoxRef& self) { return self.widget; })
        .def_prop_ro("handle", [](const MessageBoxRef& self) {
            return WidgetHandle{self.widget.handle};
        })
        .def_prop_ro("message", [](const MessageBoxRef& self) { return self.get().message(); })
        .def_prop_ro("kind", [](const MessageBoxRef& self) { return self.get().kind(); })
        .def_prop_ro("open", [](const MessageBoxRef& self) { return self.get().open(); })
        .def("show", [](const MessageBoxRef& self, tc_ui_rect viewport) {
            return self.get().show(self.widget.state->document, viewport);
        }, nb::arg("viewport"))
        .def("connect_finished", [](const MessageBoxRef& self, nb::object callback) {
            auto state = self.widget.state;
            return self.get().finished().connect(
                [state, callback = std::move(callback)](termin::gui_native::Dialog&,
                    const termin::gui_native::DialogResult& result) {
                    try {
                        nb::gil_scoped_acquire gil;
                        callback(result);
                    } catch (...) {
                        if (state && !state->pending_exception)
                            state->pending_exception = std::current_exception();
                        tc_log_error("[termin-gui-native/python] MessageBox callback failed");
                    }
                });
        }, nb::arg("callback"));

    nb::class_<InputDialogRef>(m, "InputDialog")
        .def_prop_ro("widget", [](const InputDialogRef& self) { return self.widget; })
        .def_prop_ro("handle", [](const InputDialogRef& self) {
            return WidgetHandle{self.widget.handle};
        })
        .def_prop_ro("message", [](const InputDialogRef& self) { return self.get().message(); })
        .def_prop_rw("value", [](const InputDialogRef& self) { return self.get().value(); },
                     [](const InputDialogRef& self, std::string value) {
                         self.get().set_value(std::move(value));
                     })
        .def_prop_ro("open", [](const InputDialogRef& self) { return self.get().open(); })
        .def("show", [](const InputDialogRef& self, tc_ui_rect viewport) {
            return self.get().show(self.widget.state->document, viewport);
        }, nb::arg("viewport"))
        .def("connect_value_finished", [](const InputDialogRef& self, nb::object callback) {
            auto state = self.widget.state;
            return self.get().value_finished().connect(
                [state, callback = std::move(callback)](termin::gui_native::InputDialog&,
                    const std::optional<std::string>& value) {
                    try {
                        nb::gil_scoped_acquire gil;
                        if (value)
                            callback(*value);
                        else
                            callback(nb::none());
                    } catch (...) {
                        if (state && !state->pending_exception)
                            state->pending_exception = std::current_exception();
                        tc_log_error("[termin-gui-native/python] InputDialog callback failed");
                    }
                });
        }, nb::arg("callback"));

    nb::class_<FileDialogOverlayRef>(m, "FileDialogOverlay")
        .def_prop_ro("widget", [](const FileDialogOverlayRef& self) { return self.widget; })
        .def_prop_ro("handle", [](const FileDialogOverlayRef& self) {
            return WidgetHandle{self.widget.handle};
        })
        .def_prop_ro("model", [](const FileDialogOverlayRef& self) ->
                     termin::gui_native::FileDialogModel& { return self.get().model(); },
                     nb::rv_policy::reference_internal)
        .def_prop_ro("open", [](const FileDialogOverlayRef& self) { return self.get().open(); })
        .def("set_filters", [](const FileDialogOverlayRef& self,
                               std::vector<termin::gui_native::FileDialogFilter> filters) {
            self.get().set_filters(std::move(filters));
        }, nb::arg("filters"))
        .def("set_initial_directory", [](const FileDialogOverlayRef& self, std::string directory) {
            self.get().set_initial_directory(std::move(directory));
        }, nb::arg("directory"))
        .def("set_file_name", [](const FileDialogOverlayRef& self, std::string file_name) {
            self.get().set_file_name(std::move(file_name));
        }, nb::arg("file_name"))
        .def("show", [](const FileDialogOverlayRef& self, tc_ui_rect viewport) {
            return self.get().show(self.widget.state->document, viewport);
        }, nb::arg("viewport"))
        .def("activate", [](const FileDialogOverlayRef& self, const std::string& action_id) {
            return self.get().activate(action_id, self.widget.state->document);
        }, nb::arg("action_id"))
        .def("connect_path_finished", [](const FileDialogOverlayRef& self, nb::object callback) {
            auto state = self.widget.state;
            return self.get().path_finished().connect(
                [state, callback = std::move(callback)](termin::gui_native::FileDialogOverlay&,
                    const std::optional<std::string>& path) {
                    try {
                        nb::gil_scoped_acquire gil;
                        if (path)
                            callback(*path);
                        else
                            callback(nb::none());
                    } catch (...) {
                        if (state && !state->pending_exception)
                            state->pending_exception = std::current_exception();
                        tc_log_error(
                            "[termin-gui-native/python] FileDialogOverlay callback failed");
                    }
                });
        }, nb::arg("callback"));

    nb::enum_<termin::gui_native::ColorPickerSurfaceKind>(m, "ColorPickerSurfaceKind")
        .value("SaturationValue", termin::gui_native::ColorPickerSurfaceKind::SaturationValue)
        .value("Hue", termin::gui_native::ColorPickerSurfaceKind::Hue)
        .value("Alpha", termin::gui_native::ColorPickerSurfaceKind::Alpha);

    nb::class_<termin::gui_native::ColorPickerSurface>(m, "ColorPickerSurface")
        .def_prop_ro("width", [](const termin::gui_native::ColorPickerSurface& self) {
            return self.width;
        })
        .def_prop_ro("height", [](const termin::gui_native::ColorPickerSurface& self) {
            return self.height;
        })
        .def_prop_ro("revision", [](const termin::gui_native::ColorPickerSurface& self) {
            return self.revision;
        })
        .def_prop_ro("rgba", [](const termin::gui_native::ColorPickerSurface& self) {
            return nb::bytes(reinterpret_cast<const char*>(self.rgba.data()), self.rgba.size());
        });

    nb::class_<termin::gui_native::ColorPickerTextureIds>(m, "ColorPickerTextureIds")
        .def(nb::init<uint32_t, uint32_t, uint32_t>(), nb::arg("saturation_value") = 0,
             nb::arg("hue") = 0, nb::arg("alpha") = 0)
        .def_rw("saturation_value",
                &termin::gui_native::ColorPickerTextureIds::saturation_value)
        .def_rw("hue", &termin::gui_native::ColorPickerTextureIds::hue)
        .def_rw("alpha", &termin::gui_native::ColorPickerTextureIds::alpha);

    nb::class_<termin::gui_native::ColorPickerModel>(m, "ColorPickerModel")
        .def("__init__", [](termin::gui_native::ColorPickerModel* self,
                            std::optional<tc_ui_color> initial,
                            bool show_alpha) {
            const tc_ui_color resolved_initial = initial.value_or(
                tc_ui_color{1.0f, 1.0f, 1.0f, 1.0f});
            new (self) termin::gui_native::ColorPickerModel(
                termin::gui_native::Color{resolved_initial.r, resolved_initial.g,
                                          resolved_initial.b, resolved_initial.a}, show_alpha);
        }, nb::arg("initial").none() = nb::none(),
           nb::arg("show_alpha") = true)
        .def_prop_rw("color", [](const termin::gui_native::ColorPickerModel& self) {
            return self.color().c_color();
        }, [](termin::gui_native::ColorPickerModel& self, tc_ui_color color) {
            self.set_color(termin::gui_native::Color{color.r, color.g, color.b, color.a});
        })
        .def_prop_ro("initial_color", [](const termin::gui_native::ColorPickerModel& self) {
            return self.initial_color().c_color();
        })
        .def_prop_rw("hue", &termin::gui_native::ColorPickerModel::hue,
                     &termin::gui_native::ColorPickerModel::set_hue)
        .def_prop_rw("saturation", &termin::gui_native::ColorPickerModel::saturation,
                     &termin::gui_native::ColorPickerModel::set_saturation)
        .def_prop_rw("value", &termin::gui_native::ColorPickerModel::value,
                     &termin::gui_native::ColorPickerModel::set_value)
        .def_prop_rw("alpha", &termin::gui_native::ColorPickerModel::alpha,
                     &termin::gui_native::ColorPickerModel::set_alpha)
        .def_prop_rw("show_alpha", &termin::gui_native::ColorPickerModel::show_alpha,
                     &termin::gui_native::ColorPickerModel::set_show_alpha)
        .def_prop_ro("revision", &termin::gui_native::ColorPickerModel::revision)
        .def("set_hsv", &termin::gui_native::ColorPickerModel::set_hsv, nb::arg("hue"),
             nb::arg("saturation"), nb::arg("value"));

    nb::class_<ColorPickerRef>(m, "ColorPicker")
        .def_prop_ro("widget", [](const ColorPickerRef& self) { return self.widget; })
        .def_prop_ro("handle", [](const ColorPickerRef& self) {
            return WidgetHandle{self.widget.handle};
        })
        .def_prop_rw("model", [](const ColorPickerRef& self) { return self.get().model(); },
                     [](const ColorPickerRef& self,
                        std::shared_ptr<termin::gui_native::ColorPickerModel> model) {
                         self.get().set_model(std::move(model));
                     })
        .def_prop_rw("texture_ids", [](const ColorPickerRef& self) {
            return self.get().texture_ids();
        }, [](const ColorPickerRef& self, termin::gui_native::ColorPickerTextureIds ids) {
            self.get().set_texture_ids(ids);
        })
        .def("surface", [](const ColorPickerRef& self,
                           termin::gui_native::ColorPickerSurfaceKind kind) ->
             const termin::gui_native::ColorPickerSurface& { return self.get().surface(kind); },
             nb::arg("kind"), nb::rv_policy::reference_internal)
        .def("connect_surfaces_invalidated", [](const ColorPickerRef& self, nb::object callback) {
            auto state = self.widget.state;
            return self.get().surfaces_invalidated().connect(
                [state, callback = std::move(callback)](termin::gui_native::ColorPicker&,
                                                        uint32_t flags) {
                    try {
                        nb::gil_scoped_acquire gil;
                        callback(flags);
                    } catch (...) {
                        if (state && !state->pending_exception)
                            state->pending_exception = std::current_exception();
                        tc_log_error(
                            "[termin-gui-native/python] ColorPicker surface callback failed");
                    }
                });
        }, nb::arg("callback"));

    nb::class_<ColorDialogRef>(m, "ColorDialog")
        .def_prop_ro("widget", [](const ColorDialogRef& self) { return self.widget; })
        .def_prop_ro("handle",
                     [](const ColorDialogRef& self) { return WidgetHandle{self.widget.handle}; })
        .def_prop_ro("model", [](const ColorDialogRef& self) { return self.get().model(); })
        .def_prop_ro("picker", [](const ColorDialogRef& self) -> nb::object {
            const tc_widget_handle handle = self.get().picker_handle();
            if (tc_widget_handle_is_invalid(handle)) {
                return nb::none();
            }
            return nb::cast(ColorPickerRef{WidgetRef{self.widget.state, handle}});
        })
        .def_prop_rw(
            "color", [](const ColorDialogRef& self) { return self.get().color().c_color(); },
            [](const ColorDialogRef& self, tc_ui_color color) {
                self.get().set_color(termin::gui_native::Color{color.r, color.g, color.b, color.a});
            })
        .def_prop_ro("open", [](const ColorDialogRef& self) { return self.get().open(); })
        .def(
            "show",
            [](const ColorDialogRef& self, tc_ui_rect viewport) {
                return self.get().show(self.widget.state->document, viewport);
            },
            nb::arg("viewport"))
        .def(
            "activate",
            [](const ColorDialogRef& self, const std::string& action_id) {
                return self.get().activate(action_id, self.widget.state->document);
            },
            nb::arg("action_id"))
        .def(
            "connect_color_finished",
            [](const ColorDialogRef& self, nb::object callback) {
                auto state = self.widget.state;
                return self.get().color_finished().connect(
                    [state, callback = std::move(callback)](
                        termin::gui_native::ColorDialog&,
                        const std::optional<termin::gui_native::Color>& color) {
                        try {
                            nb::gil_scoped_acquire gil;
                            if (color)
                                callback(color->c_color());
                            else
                                callback(nb::none());
                        } catch (...) {
                            if (state && !state->pending_exception)
                                state->pending_exception = std::current_exception();
                            tc_log_error("[termin-gui-native/python] ColorDialog callback failed");
                        }
                    });
            },
            nb::arg("callback"));

}
