#include "gui_native_bindings_shared.hpp"

using namespace termin::gui_native::python_bindings;

void bind_gui_native_views_and_collections(nb::module_& m) {
    nb::class_<termin::gui_native::SceneTransform>(m, "SceneTransform")
        .def(nb::init<>())
        .def(
            "__init__",
            [](termin::gui_native::SceneTransform* self, float origin_x, float origin_y,
               float zoom) {
                new (self) termin::gui_native::SceneTransform{origin_x, origin_y, zoom};
            },
            nb::arg("origin_x"), nb::arg("origin_y"), nb::arg("zoom"))
        .def_rw("origin_x", &termin::gui_native::SceneTransform::origin_x)
        .def_rw("origin_y", &termin::gui_native::SceneTransform::origin_y)
        .def_rw("zoom", &termin::gui_native::SceneTransform::zoom)
        .def("world_to_screen", &termin::gui_native::SceneTransform::world_to_screen,
             nb::arg("point"))
        .def("screen_to_world", &termin::gui_native::SceneTransform::screen_to_world,
             nb::arg("point"));

    nb::class_<termin::gui_native::GraphicsItem>(m, "GraphicsItem")
        .def(nb::init<std::string>(), nb::arg("stable_id") = "")
        .def_prop_ro("id", &termin::gui_native::GraphicsItem::id)
        .def_prop_rw("stable_id", &termin::gui_native::GraphicsItem::stable_id,
                     &termin::gui_native::GraphicsItem::set_stable_id)
        .def_prop_ro("parent", &termin::gui_native::GraphicsItem::parent)
        .def_prop_ro("children",
                     [](const termin::gui_native::GraphicsItem& self) { return self.children(); })
        .def_prop_rw("position", &termin::gui_native::GraphicsItem::position,
                     &termin::gui_native::GraphicsItem::set_position)
        .def_prop_rw("size", &termin::gui_native::GraphicsItem::size,
                     &termin::gui_native::GraphicsItem::set_size)
        .def_prop_rw("z_index", &termin::gui_native::GraphicsItem::z_index,
                     &termin::gui_native::GraphicsItem::set_z_index)
        .def_prop_rw("visible", &termin::gui_native::GraphicsItem::visible,
                     &termin::gui_native::GraphicsItem::set_visible)
        .def_prop_rw("enabled", &termin::gui_native::GraphicsItem::enabled,
                     &termin::gui_native::GraphicsItem::set_enabled)
        .def_prop_rw("selectable", &termin::gui_native::GraphicsItem::selectable,
                     &termin::gui_native::GraphicsItem::set_selectable)
        .def_prop_rw("draggable", &termin::gui_native::GraphicsItem::draggable,
                     &termin::gui_native::GraphicsItem::set_draggable)
        .def_prop_ro("selected", &termin::gui_native::GraphicsItem::selected)
        .def_prop_ro("hovered", &termin::gui_native::GraphicsItem::hovered)
        .def_prop_ro("world_position", &termin::gui_native::GraphicsItem::world_position)
        .def_prop_ro("world_bounds", &termin::gui_native::GraphicsItem::world_bounds)
        .def_prop_rw(
            "embedded_widget",
            [](const termin::gui_native::GraphicsItem& self) {
                return WidgetHandle{self.embedded_widget()};
            },
            [](termin::gui_native::GraphicsItem& self, WidgetHandle handle) {
                self.set_embedded_widget(handle.handle);
            })
        .def("add_child", &termin::gui_native::GraphicsItem::add_child, nb::arg("child"))
        .def("remove_child", &termin::gui_native::GraphicsItem::remove_child, nb::arg("child"))
        .def("clear_children", &termin::gui_native::GraphicsItem::clear_children)
        .def("contains_local", &termin::gui_native::GraphicsItem::contains_local, nb::arg("x"),
             nb::arg("y"))
        .def("hit_test", &termin::gui_native::GraphicsItem::hit_test, nb::arg("world_x"),
             nb::arg("world_y"))
        .def("clear_embedded_widget", &termin::gui_native::GraphicsItem::clear_embedded_widget)
        .def(
            "set_paint_callback",
            [](termin::gui_native::GraphicsItem& self, nb::object callback) {
                if (callback.is_none()) {
                    self.set_paint_callback({});
                    return;
                }
                self.set_paint_callback([callback = std::move(callback)](
                                            termin::gui_native::GraphicsItem& item,
                                            tc_ui_paint_context* context,
                                            const termin::gui_native::SceneTransform& transform) {
                    nb::gil_scoped_acquire gil;
                    try {
                        PaintContext borrowed(context, false);
                        callback(item.shared_from_this(), std::move(borrowed),
                                 termin::gui_native::SceneTransform{transform});
                    } catch (...) {
                        tc_log_error(
                            "[termin-gui-native/python] GraphicsItem paint callback failed");
                        throw;
                    }
                });
            },
            nb::arg("callback"))
        .def(
            "set_hit_test_callback",
            [](termin::gui_native::GraphicsItem& self, nb::object callback) {
                if (callback.is_none()) {
                    self.set_hit_test_callback({});
                    return;
                }
                self.set_hit_test_callback([callback = std::move(callback)](
                                               const termin::gui_native::GraphicsItem& item,
                                               float x, float y) {
                    nb::gil_scoped_acquire gil;
                    try {
                        return nb::cast<bool>(callback(
                            const_cast<termin::gui_native::GraphicsItem&>(item).shared_from_this(),
                            x, y));
                    } catch (...) {
                        tc_log_error(
                            "[termin-gui-native/python] GraphicsItem hit-test callback failed");
                        throw;
                    }
                });
            },
            nb::arg("callback"));

    nb::class_<termin::gui_native::GraphicsScene>(m, "GraphicsScene")
        .def(nb::init<>())
        .def_prop_ro("items",
                     [](const termin::gui_native::GraphicsScene& self) { return self.items(); })
        .def_prop_ro(
            "selected_items",
            [](const termin::gui_native::GraphicsScene& self) { return self.selected_items(); })
        .def_prop_ro("revision", &termin::gui_native::GraphicsScene::revision)
        .def("add_item", &termin::gui_native::GraphicsScene::add_item, nb::arg("item"))
        .def("remove_item", &termin::gui_native::GraphicsScene::remove_item, nb::arg("item"))
        .def("clear", &termin::gui_native::GraphicsScene::clear)
        .def("hit_test", &termin::gui_native::GraphicsScene::hit_test, nb::arg("world_x"),
             nb::arg("world_y"))
        .def("set_selected", &termin::gui_native::GraphicsScene::set_selected, nb::arg("item"))
        .def("toggle_selected", &termin::gui_native::GraphicsScene::toggle_selected,
             nb::arg("item"))
        .def("clear_selection", &termin::gui_native::GraphicsScene::clear_selection)
        .def(
            "contains",
            [](const termin::gui_native::GraphicsScene& self,
               const std::shared_ptr<termin::gui_native::GraphicsItem>& item) {
                return self.contains(item.get());
            },
            nb::arg("item"))
        .def(
            "connect_selection_changed",
            [](termin::gui_native::GraphicsScene& self, nb::object callback) {
                return self.selection_changed().connect(
                    [callback = std::move(callback)](termin::gui_native::GraphicsScene&,
                                                     const auto& selected) {
                        nb::gil_scoped_acquire gil;
                        try {
                            callback(selected);
                        } catch (...) {
                            tc_log_error("[termin-gui-native/python] GraphicsScene selection "
                                         "callback failed");
                        }
                    });
            },
            nb::arg("callback"));

    nb::class_<termin::gui_native::CollectionItem>(m, "CollectionItem")
        .def(nb::init<>())
        .def(
            "__init__",
            [](termin::gui_native::CollectionItem* self, std::string stable_id, std::string text,
               std::string subtitle, bool enabled, uint32_t texture_id, std::string icon) {
                new (self)
                    termin::gui_native::CollectionItem{std::move(stable_id), std::move(text),
                                                       std::move(subtitle), enabled, texture_id,
                                                       std::move(icon)};
            },
            nb::arg("stable_id"), nb::arg("text"), nb::arg("subtitle") = "",
            nb::arg("enabled") = true, nb::arg("texture_id") = 0, nb::arg("icon") = "")
        .def_rw("stable_id", &termin::gui_native::CollectionItem::stable_id)
        .def_rw("text", &termin::gui_native::CollectionItem::text)
        .def_rw("subtitle", &termin::gui_native::CollectionItem::subtitle)
        .def_rw("enabled", &termin::gui_native::CollectionItem::enabled)
        .def_rw("texture_id", &termin::gui_native::CollectionItem::texture_id)
        .def_rw("icon", &termin::gui_native::CollectionItem::icon);

    nb::class_<termin::gui_native::CollectionModel>(m, "CollectionModel")
        .def(nb::init<>())
        .def_prop_ro("item_count", &termin::gui_native::CollectionModel::size)
        .def_prop_ro("revision", &termin::gui_native::CollectionModel::revision)
        .def_prop_ro("items",
                     [](const termin::gui_native::CollectionModel& self) { return self.items(); })
        .def(
            "item",
            [](const termin::gui_native::CollectionModel& self, size_t index) {
                return self.item(index);
            },
            nb::arg("index"))
        .def("set_items", &termin::gui_native::CollectionModel::set_items, nb::arg("items"))
        .def("append", &termin::gui_native::CollectionModel::append, nb::arg("item"))
        .def("update", &termin::gui_native::CollectionModel::update, nb::arg("index"),
             nb::arg("item"))
        .def("erase", &termin::gui_native::CollectionModel::erase, nb::arg("index"))
        .def("clear", &termin::gui_native::CollectionModel::clear);

    nb::class_<termin::gui_native::RichTextStyle>(m, "RichTextStyle")
        .def(nb::init<>())
        .def(
            "__init__",
            [](termin::gui_native::RichTextStyle* self, std::optional<tc_ui_color> color,
               bool bold, bool italic) {
                new (self) termin::gui_native::RichTextStyle{std::move(color), bold, italic};
            },
            nb::arg("color") = nb::none(), nb::arg("bold") = false,
            nb::arg("italic") = false)
        .def_rw("color", &termin::gui_native::RichTextStyle::color)
        .def_rw("bold", &termin::gui_native::RichTextStyle::bold)
        .def_rw("italic", &termin::gui_native::RichTextStyle::italic);

    nb::class_<termin::gui_native::RichTextSegment>(m, "RichTextSegment")
        .def(nb::init<>())
        .def(
            "__init__",
            [](termin::gui_native::RichTextSegment* self, std::string text,
               std::optional<termin::gui_native::RichTextStyle> style) {
                new (self) termin::gui_native::RichTextSegment{
                    std::move(text), style.value_or(termin::gui_native::RichTextStyle{})};
            },
            nb::arg("text"), nb::arg("style").none() = nb::none())
        .def_rw("text", &termin::gui_native::RichTextSegment::text)
        .def_rw("style", &termin::gui_native::RichTextSegment::style);

    nb::class_<termin::gui_native::RichTextModel>(m, "RichTextModel")
        .def(nb::init<>())
        .def_prop_ro("revision", &termin::gui_native::RichTextModel::revision)
        .def_prop_ro("text", &termin::gui_native::RichTextModel::text)
        .def_prop_ro("lines", [](const termin::gui_native::RichTextModel& self) {
            return self.lines();
        })
        .def("set_text", &termin::gui_native::RichTextModel::set_text, nb::arg("text"))
        .def("set_lines", &termin::gui_native::RichTextModel::set_lines, nb::arg("lines"))
        .def("set_html", &termin::gui_native::RichTextModel::set_html, nb::arg("html"))
        .def("clear", &termin::gui_native::RichTextModel::clear);

    nb::class_<RichTextViewRef>(m, "RichTextView")
        .def_prop_ro("widget", [](const RichTextViewRef& self) { return self.widget; })
        .def_prop_ro("handle", [](const RichTextViewRef& self) {
            return WidgetHandle{self.widget.handle};
        })
        .def_prop_rw(
            "model", [](const RichTextViewRef& self) { return self.get().model(); },
            [](const RichTextViewRef& self,
               std::shared_ptr<termin::gui_native::RichTextModel> model) {
                self.get().set_model(std::move(model));
            })
        .def_prop_rw(
            "placeholder", [](const RichTextViewRef& self) { return self.get().placeholder(); },
            [](const RichTextViewRef& self, const std::string& value) {
                self.get().set_placeholder(value);
            })
        .def_prop_rw(
            "word_wrap", [](const RichTextViewRef& self) { return self.get().word_wrap(); },
            [](const RichTextViewRef& self, bool value) { self.get().set_word_wrap(value); })
        .def_prop_rw(
            "show_scrollbar",
            [](const RichTextViewRef& self) { return self.get().show_scrollbar(); },
            [](const RichTextViewRef& self, bool value) {
                self.get().set_show_scrollbar(value);
            })
        .def_prop_rw(
            "line_height", [](const RichTextViewRef& self) { return self.get().line_height(); },
            [](const RichTextViewRef& self, float value) { self.get().set_line_height(value); })
        .def_prop_rw(
            "scroll_y", [](const RichTextViewRef& self) { return self.get().scroll_y(); },
            [](const RichTextViewRef& self, float value) { self.get().set_scroll_y(value); })
        .def_prop_ro("content_height",
                     [](const RichTextViewRef& self) { return self.get().content_height(); })
        .def_prop_ro("visual_line_count",
                     [](const RichTextViewRef& self) { return self.get().visual_line_count(); })
        .def_prop_ro("has_selection",
                     [](const RichTextViewRef& self) { return self.get().has_selection(); })
        .def_prop_ro("selected_text",
                     [](const RichTextViewRef& self) { return self.get().selected_text(); })
        .def(
            "select",
            [](const RichTextViewRef& self, size_t anchor, size_t cursor) {
                self.get().select(anchor, cursor);
            },
            nb::arg("anchor"), nb::arg("cursor"))
        .def("select_all", [](const RichTextViewRef& self) { self.get().select_all(); })
        .def("clear_selection",
             [](const RichTextViewRef& self) { self.get().clear_selection(); });

    nb::class_<termin::gui_native::FrameTimeModel>(m, "FrameTimeModel")
        .def(nb::init<>())
        .def_prop_rw(
            "max_samples",
            [](const termin::gui_native::FrameTimeModel& self) { return self.max_samples(); },
            &termin::gui_native::FrameTimeModel::set_max_samples)
        .def_prop_ro("samples",
                     [](const termin::gui_native::FrameTimeModel& self) { return self.samples(); })
        .def_prop_ro("revision", &termin::gui_native::FrameTimeModel::revision)
        .def("add_sample", &termin::gui_native::FrameTimeModel::add_sample, nb::arg("milliseconds"))
        .def("set_samples", &termin::gui_native::FrameTimeModel::set_samples, nb::arg("samples"))
        .def("clear", &termin::gui_native::FrameTimeModel::clear);

    nb::class_<FrameTimeGraphRef>(m, "FrameTimeGraph")
        .def_prop_ro("widget", [](const FrameTimeGraphRef& self) { return self.widget; })
        .def_prop_ro("handle",
                     [](const FrameTimeGraphRef& self) { return WidgetHandle{self.widget.handle}; })
        .def_prop_rw(
            "model", [](const FrameTimeGraphRef& self) { return self.get().model(); },
            [](const FrameTimeGraphRef& self,
               std::shared_ptr<termin::gui_native::FrameTimeModel> model) {
                self.get().set_model(std::move(model));
            })
        .def_prop_ro("target_frame_ms",
                     [](const FrameTimeGraphRef& self) { return self.get().target_frame_ms(); })
        .def_prop_ro("warning_frame_ms",
                     [](const FrameTimeGraphRef& self) { return self.get().warning_frame_ms(); })
        .def(
            "set_thresholds",
            [](const FrameTimeGraphRef& self, float target, float warning) {
                self.get().set_thresholds(target, warning);
            },
            nb::arg("target_frame_ms"), nb::arg("warning_frame_ms"));

    nb::class_<termin::gui_native::ViewportSurfaceSize>(m, "ViewportSurfaceSize")
        .def(nb::init<>())
        .def(
            "__init__",
            [](termin::gui_native::ViewportSurfaceSize* self, int width, int height) {
                new (self) termin::gui_native::ViewportSurfaceSize{width, height};
            },
            nb::arg("width"), nb::arg("height"))
        .def_rw("width", &termin::gui_native::ViewportSurfaceSize::width)
        .def_rw("height", &termin::gui_native::ViewportSurfaceSize::height);

    nb::enum_<termin::gui_native::ViewportExternalDragPhase>(m, "ViewportExternalDragPhase")
        .value("Enter", termin::gui_native::ViewportExternalDragPhase::Enter)
        .value("Move", termin::gui_native::ViewportExternalDragPhase::Move)
        .value("Leave", termin::gui_native::ViewportExternalDragPhase::Leave)
        .value("Drop", termin::gui_native::ViewportExternalDragPhase::Drop);

    nb::class_<termin::gui_native::ViewportExternalDragEvent>(m, "ViewportExternalDragEvent")
        .def(nb::init<>())
        .def(
            "__init__",
            [](termin::gui_native::ViewportExternalDragEvent* self,
               termin::gui_native::ViewportExternalDragPhase phase, std::string mime_type,
               std::string payload, float x, float y) {
                new (self) termin::gui_native::ViewportExternalDragEvent{
                    phase, std::move(mime_type), std::move(payload), x, y};
            },
            nb::arg("phase"), nb::arg("mime_type"), nb::arg("payload"), nb::arg("x"), nb::arg("y"))
        .def_rw("phase", &termin::gui_native::ViewportExternalDragEvent::phase)
        .def_rw("mime_type", &termin::gui_native::ViewportExternalDragEvent::mime_type)
        .def_rw("payload", &termin::gui_native::ViewportExternalDragEvent::payload)
        .def_rw("x", &termin::gui_native::ViewportExternalDragEvent::x)
        .def_rw("y", &termin::gui_native::ViewportExternalDragEvent::y);

    nb::class_<Viewport3DRef>(m, "Viewport3D")
        .def_prop_ro("widget", [](const Viewport3DRef& self) { return self.widget; })
        .def_prop_ro("handle",
                     [](const Viewport3DRef& self) { return WidgetHandle{self.widget.handle}; })
        .def_prop_ro("has_surface",
                     [](const Viewport3DRef& self) { return self.get().has_surface(); })
        .def_prop_ro("surface_valid",
                     [](const Viewport3DRef& self) {
                         const bool valid = self.get().surface_valid();
                         self.widget.throw_pending_exception();
                         return valid;
                     })
        .def_prop_ro("texture_id",
                     [](const Viewport3DRef& self) {
                         const uint32_t texture = self.get().texture_id();
                         self.widget.throw_pending_exception();
                         return texture;
                     })
        .def_prop_ro("surface_size",
                     [](const Viewport3DRef& self) {
                         const auto size = self.get().surface_size();
                         self.widget.throw_pending_exception();
                         return size;
                     })
        .def(
            "set_surface_host",
            [](const Viewport3DRef& self, nb::object host) {
                if (host.is_none()) {
                    self.get().detach_surface();
                    return;
                }
                self.get().set_surface_host(std::make_shared<PythonViewportSurfaceHost>(
                    std::move(host), self.widget.state));
                self.widget.throw_pending_exception();
            },
            nb::arg("host"))
        .def("detach_surface", [](const Viewport3DRef& self) { self.get().detach_surface(); })
        .def(
            "connect_before_resize",
            [](const Viewport3DRef& self, nb::object callback) {
                const std::shared_ptr<DocumentState> state = self.widget.state;
                return self.get().before_resize().connect(
                    [state, callback = std::move(callback)](
                        termin::gui_native::Viewport3D&,
                        termin::gui_native::ViewportSurfaceSize previous,
                        termin::gui_native::ViewportSurfaceSize next) {
                        if (!state || !state->document)
                            return;
                        nb::gil_scoped_acquire gil;
                        try {
                            callback(previous, next);
                        } catch (...) {
                            tc_log_error("[termin-gui-native/python] Viewport3D before-resize "
                                         "callback failed");
                            if (!state->pending_exception)
                                state->pending_exception = std::current_exception();
                            throw;
                        }
                    });
            },
            nb::arg("callback"))
        .def(
            "set_external_drag_handler",
            [](const Viewport3DRef& self, nb::object callback) {
                if (callback.is_none()) {
                    self.get().set_external_drag_handler({});
                    return;
                }
                const std::shared_ptr<DocumentState> state = self.widget.state;
                self.get().set_external_drag_handler([state, callback = std::move(callback)](
                                                         const termin::gui_native::
                                                             ViewportExternalDragEvent& event) {
                    if (!state || !state->document)
                        return false;
                    nb::gil_scoped_acquire gil;
                    try {
                        return nb::cast<bool>(callback(event));
                    } catch (...) {
                        tc_log_error(
                            "[termin-gui-native/python] Viewport3D external drag callback failed");
                        if (!state->pending_exception)
                            state->pending_exception = std::current_exception();
                        return false;
                    }
                });
            },
            nb::arg("callback"))
        .def(
            "dispatch_external_drag",
            [](const Viewport3DRef& self,
               const termin::gui_native::ViewportExternalDragEvent& event) {
                const bool handled = self.get().dispatch_external_drag(event);
                self.widget.throw_pending_exception();
                return handled;
            },
            nb::arg("event"));

    nb::class_<SceneViewRef>(m, "SceneView")
        .def_prop_ro("widget",
                     [](const SceneViewRef &self) { return self.widget; })
        .def_prop_ro("handle",
                     [](const SceneViewRef &self) {
                       return WidgetHandle{self.widget.handle};
                     })
        .def_prop_rw(
            "scene",
            [](const SceneViewRef &self) { return self.get().scene(); },
            [](const SceneViewRef &self,
               std::shared_ptr<termin::gui_native::GraphicsScene> scene) {
              self.get().set_scene(std::move(scene));
            })
        .def_prop_ro(
            "transform",
            [](const SceneViewRef &self) { return self.get().transform(); })
        .def_prop_rw(
            "offset",
            [](const SceneViewRef &self) { return self.get().offset(); },
            [](const SceneViewRef &self, tc_ui_point offset) {
              self.get().set_offset(offset);
              self.widget.throw_pending_exception();
            })
        .def_prop_ro("zoom",
                     [](const SceneViewRef &self) { return self.get().zoom(); })
        .def_prop_ro(
            "min_zoom",
            [](const SceneViewRef &self) { return self.get().min_zoom(); })
        .def_prop_ro(
            "max_zoom",
            [](const SceneViewRef &self) { return self.get().max_zoom(); })
        .def_prop_rw(
            "zoom_factor",
            [](const SceneViewRef &self) { return self.get().zoom_factor(); },
            [](const SceneViewRef &self, float factor) {
              self.get().set_zoom_factor(factor);
            })
        .def_prop_rw(
            "show_grid",
            [](const SceneViewRef &self) { return self.get().show_grid(); },
            [](const SceneViewRef &self, bool show) {
              self.get().set_show_grid(show);
            })
        .def_prop_rw(
            "grid_step",
            [](const SceneViewRef &self) { return self.get().grid_step(); },
            [](const SceneViewRef &self, float step) {
              self.get().set_grid_step(step);
            })
        .def(
            "set_zoom",
            [](const SceneViewRef &self, float zoom, tc_ui_point anchor) {
              self.get().set_zoom(zoom, anchor);
              self.widget.throw_pending_exception();
            },
            nb::arg("zoom"), nb::arg("anchor"))
        .def(
            "set_zoom_range",
            [](const SceneViewRef &self, float minimum, float maximum) {
              self.get().set_zoom_range(minimum, maximum);
              self.widget.throw_pending_exception();
            },
            nb::arg("minimum"), nb::arg("maximum"))
        .def(
            "set_scene_colors",
            [](const SceneViewRef &self, tc_ui_color background,
               tc_ui_color grid, tc_ui_color axes) {
              self.get().set_scene_colors(
                  termin::gui_native::Color{background.r, background.g,
                                            background.b, background.a},
                  termin::gui_native::Color{grid.r, grid.g, grid.b, grid.a},
                  termin::gui_native::Color{axes.r, axes.g, axes.b, axes.a});
            },
            nb::arg("background"), nb::arg("grid"), nb::arg("axes"))
        .def(
            "world_to_screen",
            [](const SceneViewRef &self, tc_ui_point point) {
              return self.get().world_to_screen(point);
            },
            nb::arg("point"))
        .def(
            "screen_to_world",
            [](const SceneViewRef &self, tc_ui_point point) {
              return self.get().screen_to_world(point);
            },
            nb::arg("point"))
        .def(
            "set_pointer_handler",
            [](const SceneViewRef &self, nb::object callback) {
              if (callback.is_none()) {
                self.get().set_pointer_handler({});
                return;
              }
              const auto state = self.widget.state;
              self.get().set_pointer_handler(
                  [state, callback = std::move(callback)](
                      termin::gui_native::SceneView &, tc_ui_point world,
                      const tc_ui_pointer_event &event) {
                    nb::gil_scoped_acquire gil;
                    try {
                      return nb::cast<bool>(callback(world, event));
                    } catch (...) {
                      if (state && !state->pending_exception)
                        state->pending_exception = std::current_exception();
                      tc_log_error("[termin-gui-native/python] SceneView "
                                   "pointer handler failed");
                      throw;
                    }
                  });
            },
            nb::arg("callback").none())
        .def(
            "set_key_handler",
            [](const SceneViewRef &self, nb::object callback) {
              if (callback.is_none()) {
                self.get().set_key_handler({});
                return;
              }
              const auto state = self.widget.state;
              self.get().set_key_handler(
                  [state, callback = std::move(callback)](
                      termin::gui_native::SceneView &,
                      const tc_ui_key_event &event) {
                    nb::gil_scoped_acquire gil;
                    try {
                      return nb::cast<bool>(callback(event));
                    } catch (...) {
                      if (state && !state->pending_exception)
                        state->pending_exception = std::current_exception();
                      tc_log_error("[termin-gui-native/python] SceneView key "
                                   "handler failed");
                      throw;
                    }
                  });
            },
            nb::arg("callback").none())
        .def(
            "set_text_handler",
            [](const SceneViewRef &self, nb::object callback) {
              if (callback.is_none()) {
                self.get().set_text_handler({});
                return;
              }
              const auto state = self.widget.state;
              self.get().set_text_handler([state,
                                           callback = std::move(callback)](
                                              termin::gui_native::SceneView &,
                                              const tc_ui_text_event &event) {
                nb::gil_scoped_acquire gil;
                try {
                  return nb::cast<bool>(callback(event.text ? event.text : ""));
                } catch (...) {
                  if (state && !state->pending_exception)
                    state->pending_exception = std::current_exception();
                  tc_log_error("[termin-gui-native/python] SceneView text "
                               "handler failed");
                  throw;
                }
              });
            },
            nb::arg("callback").none())
        .def(
            "connect_item_moved",
            [](const SceneViewRef &self, nb::object callback) {
              const auto state = self.widget.state;
              return self.get().item_moved().connect(
                  [state, callback = std::move(callback)](
                      termin::gui_native::SceneView &,
                      std::shared_ptr<termin::gui_native::GraphicsItem> item) {
                    nb::gil_scoped_acquire gil;
                    try {
                      callback(std::move(item));
                    } catch (...) {
                      if (state && !state->pending_exception)
                        state->pending_exception = std::current_exception();
                      tc_log_error("[termin-gui-native/python] SceneView "
                                   "item-moved callback failed");
                    }
                  });
            },
            nb::arg("callback"))
        .def(
            "connect_transform_changed",
            [](const SceneViewRef &self, nb::object callback) {
              const auto state = self.widget.state;
              return self.get().transform_changed().connect(
                  [state, callback = std::move(callback)](
                      termin::gui_native::SceneView &,
                      const termin::gui_native::SceneTransform &transform) {
                    nb::gil_scoped_acquire gil;
                    try {
                      callback(termin::gui_native::SceneTransform{transform});
                    } catch (...) {
                      if (state && !state->pending_exception)
                        state->pending_exception = std::current_exception();
                      tc_log_error("[termin-gui-native/python] SceneView "
                                   "transform callback "
                                   "failed");
                    }
                  });
            },
            nb::arg("callback"));

    nb::enum_<termin::gui_native::SelectionMode>(m, "SelectionMode")
        .value("Single", termin::gui_native::SelectionMode::Single)
        .value("Multiple", termin::gui_native::SelectionMode::Multiple);

    nb::enum_<termin::gui_native::PointerButton>(m, "PointerButton")
        .value("Left", termin::gui_native::PointerButton::Left)
        .value("Right", termin::gui_native::PointerButton::Right)
        .value("Middle", termin::gui_native::PointerButton::Middle);

    nb::class_<ListWidgetRef>(m, "ListWidget")
        .def_prop_ro("widget", [](const ListWidgetRef& self) { return self.widget; })
        .def_prop_ro("handle",
                     [](const ListWidgetRef& self) { return WidgetHandle{self.widget.handle}; })
        .def_prop_rw(
            "model", [](const ListWidgetRef& self) { return self.get().model(); },
            [](const ListWidgetRef& self,
               std::shared_ptr<termin::gui_native::CollectionModel> model) {
                self.get().set_model(std::move(model));
            })
        .def_prop_rw(
            "selection_mode",
            [](const ListWidgetRef& self) { return self.get().selection().mode(); },
            [](const ListWidgetRef& self, termin::gui_native::SelectionMode mode) {
                self.get().set_selection_mode(mode);
            })
        .def_prop_ro(
            "selected_indices",
            [](const ListWidgetRef& self) { return self.get().selection().selected_indices(); })
        .def_prop_ro("current_index",
                     [](const ListWidgetRef& self) -> int64_t {
                         const size_t index = self.get().selection().current();
                         return index == termin::gui_native::SelectionModel::npos
                                    ? -1
                                    : static_cast<int64_t>(index);
                     })
        .def_prop_rw(
            "scroll_y", [](const ListWidgetRef& self) { return self.get().scroll_y(); },
            [](const ListWidgetRef& self, float value) { self.get().set_scroll_y(value); })
        .def_prop_ro("content_height",
                     [](const ListWidgetRef& self) { return self.get().content_height(); })
        .def_prop_ro("visible_range",
                     [](const ListWidgetRef& self) {
                         const auto [first, last] = self.get().visible_range();
                         return std::vector<size_t>{first, last};
                     })
        .def(
            "set_row_height",
            [](const ListWidgetRef& self, float height) { self.get().set_row_height(height); },
            nb::arg("height"))
        .def(
            "set_row_spacing",
            [](const ListWidgetRef& self, float spacing) { self.get().set_row_spacing(spacing); },
            nb::arg("spacing"))
        .def(
            "select",
            [](const ListWidgetRef& self, size_t index, bool toggle, bool extend, bool additive) {
                const bool changed = self.get().select_index(index, toggle, extend, additive);
                self.widget.throw_pending_exception();
                return changed;
            },
            nb::arg("index"), nb::arg("toggle") = false, nb::arg("extend") = false,
            nb::arg("additive") = false)
        .def("clear_selection",
             [](const ListWidgetRef& self) {
                 const bool changed = self.get().clear_selection();
                 self.widget.throw_pending_exception();
                 return changed;
             })
        .def(
            "ensure_visible",
            [](const ListWidgetRef& self, size_t index) { self.get().ensure_visible(index); },
            nb::arg("index"))
        .def(
            "connect_selection_changed",
            [](const ListWidgetRef& self, nb::object callback) {
                auto state = self.widget.state;
                return self.get().selection_changed().connect(
                    [state, callback = std::move(callback)](termin::gui_native::ListWidget&,
                                                            const std::vector<size_t>& selected) {
                        try {
                            nb::gil_scoped_acquire gil;
                            callback(selected);
                        } catch (...) {
                            if (state && !state->pending_exception)
                                state->pending_exception = std::current_exception();
                            tc_log_error(
                                "[termin-gui-native/python] ListWidget selection callback failed");
                        }
                    });
            },
            nb::arg("callback"))
        .def(
            "connect_context_menu_requested",
            [](const ListWidgetRef& self, nb::object callback) {
                auto state = self.widget.state;
                return self.get().context_menu_requested().connect(
                    [state, callback = std::move(callback)](
                        termin::gui_native::ListWidget&, int64_t index, float x, float y) {
                        try {
                            nb::gil_scoped_acquire gil;
                            callback(index, x, y);
                        } catch (...) {
                            if (state && !state->pending_exception)
                                state->pending_exception = std::current_exception();
                            tc_log_error(
                                "[termin-gui-native/python] ListWidget context callback failed");
                        }
                    });
            },
            nb::arg("callback"))
        .def(
            "connect_activated",
            [](const ListWidgetRef& self, nb::object callback) {
                auto state = self.widget.state;
                return self.get().activated().connect(
                    [state, callback = std::move(callback)](
                        termin::gui_native::ListWidget&, size_t index,
                        const termin::gui_native::CollectionItem& item) {
                        try {
                            nb::gil_scoped_acquire gil;
                            callback(index, item);
                        } catch (...) {
                            if (state && !state->pending_exception)
                                state->pending_exception = std::current_exception();
                            tc_log_error(
                                "[termin-gui-native/python] ListWidget activation callback failed");
                        }
                    });
            },
            nb::arg("callback"));

    nb::class_<FileGridWidgetRef>(m, "FileGridWidget")
        .def_prop_ro("widget", [](const FileGridWidgetRef& self) { return self.widget; })
        .def_prop_ro("handle",
                     [](const FileGridWidgetRef& self) { return WidgetHandle{self.widget.handle}; })
        .def_prop_rw(
            "model", [](const FileGridWidgetRef& self) { return self.get().model(); },
            [](const FileGridWidgetRef& self,
               std::shared_ptr<termin::gui_native::CollectionModel> model) {
                self.get().set_model(std::move(model));
            })
        .def_prop_rw(
            "selection_mode",
            [](const FileGridWidgetRef& self) { return self.get().selection().mode(); },
            [](const FileGridWidgetRef& self, termin::gui_native::SelectionMode mode) {
                self.get().set_selection_mode(mode);
                self.widget.throw_pending_exception();
            })
        .def_prop_ro(
            "selected_indices",
            [](const FileGridWidgetRef& self) { return self.get().selection().selected_indices(); })
        .def_prop_ro("current_index",
                     [](const FileGridWidgetRef& self) -> int64_t {
                         const size_t index = self.get().selection().current();
                         return index == termin::gui_native::SelectionModel::npos
                                    ? -1
                                    : static_cast<int64_t>(index);
                     })
        .def_prop_ro("column_count",
                     [](const FileGridWidgetRef& self) { return self.get().column_count(); })
        .def_prop_ro("row_count",
                     [](const FileGridWidgetRef& self) { return self.get().row_count(); })
        .def_prop_rw(
            "scroll_y", [](const FileGridWidgetRef& self) { return self.get().scroll_y(); },
            [](const FileGridWidgetRef& self, float value) { self.get().set_scroll_y(value); })
        .def_prop_ro("content_height",
                     [](const FileGridWidgetRef& self) { return self.get().content_height(); })
        .def_prop_ro("has_scrollbar",
                     [](const FileGridWidgetRef& self) { return self.get().has_scrollbar(); })
        .def_prop_ro(
            "scrollbar_thumb_rect",
            [](const FileGridWidgetRef& self) { return self.get().scrollbar_thumb_rect(); })
        .def_prop_ro("visible_range",
                     [](const FileGridWidgetRef& self) {
                         const auto [first, last] = self.get().visible_range();
                         return std::vector<size_t>{first, last};
                     })
        .def_prop_rw(
            "show_scrollbar",
            [](const FileGridWidgetRef& self) { return self.get().show_scrollbar(); },
            [](const FileGridWidgetRef& self, bool value) { self.get().set_show_scrollbar(value); })
        .def_prop_rw(
            "empty_text", [](const FileGridWidgetRef& self) { return self.get().empty_text(); },
            [](const FileGridWidgetRef& self, std::string value) {
                self.get().set_empty_text(std::move(value));
            })
        .def(
            "set_tile_size",
            [](const FileGridWidgetRef& self, float width, float height) {
                self.get().set_tile_size(width, height);
            },
            nb::arg("width"), nb::arg("height"))
        .def(
            "set_tile_spacing",
            [](const FileGridWidgetRef& self, float spacing) {
                self.get().set_tile_spacing(spacing);
            },
            nb::arg("spacing"))
        .def(
            "set_padding",
            [](const FileGridWidgetRef& self, float padding) { self.get().set_padding(padding); },
            nb::arg("padding"))
        .def(
            "set_icon_size",
            [](const FileGridWidgetRef& self, float size) { self.get().set_icon_size(size); },
            nb::arg("size"))
        .def(
            "set_scrollbar_width",
            [](const FileGridWidgetRef& self, float width) {
                self.get().set_scrollbar_width(width);
            },
            nb::arg("width"))
        .def(
            "item_rect",
            [](const FileGridWidgetRef& self, size_t index) { return self.get().item_rect(index); },
            nb::arg("index"))
        .def(
            "select",
            [](const FileGridWidgetRef& self, size_t index, bool toggle, bool extend,
               bool additive) {
                const bool changed = self.get().select_index(index, toggle, extend, additive);
                self.widget.throw_pending_exception();
                return changed;
            },
            nb::arg("index"), nb::arg("toggle") = false, nb::arg("extend") = false,
            nb::arg("additive") = false)
        .def("clear_selection",
             [](const FileGridWidgetRef& self) {
                 const bool changed = self.get().clear_selection();
                 self.widget.throw_pending_exception();
                 return changed;
             })
        .def(
            "ensure_visible",
            [](const FileGridWidgetRef& self, size_t index) { self.get().ensure_visible(index); },
            nb::arg("index"))
        .def(
            "connect_selection_changed",
            [](const FileGridWidgetRef& self, nb::object callback) {
                auto state = self.widget.state;
                return self.get().selection_changed().connect([state,
                                                               callback = std::move(callback)](
                                                                  termin::gui_native::
                                                                      FileGridWidget&,
                                                                  const std::vector<size_t>&
                                                                      selected) {
                    try {
                        nb::gil_scoped_acquire gil;
                        callback(selected);
                    } catch (...) {
                        if (state && !state->pending_exception) {
                            state->pending_exception = std::current_exception();
                        }
                        tc_log_error(
                            "[termin-gui-native/python] FileGridWidget selection callback failed");
                    }
                });
            },
            nb::arg("callback"))
        .def(
            "connect_activated",
            [](const FileGridWidgetRef& self, nb::object callback) {
                auto state = self.widget.state;
                return self.get().activated().connect([state, callback = std::move(callback)](
                                                          termin::gui_native::FileGridWidget&,
                                                          size_t index,
                                                          const termin::gui_native::CollectionItem&
                                                              item) {
                    try {
                        nb::gil_scoped_acquire gil;
                        callback(index, item);
                    } catch (...) {
                        if (state && !state->pending_exception) {
                            state->pending_exception = std::current_exception();
                        }
                        tc_log_error(
                            "[termin-gui-native/python] FileGridWidget activation callback failed");
                    }
                });
            },
            nb::arg("callback"))
        .def(
            "connect_delete_requested",
            [](const FileGridWidgetRef& self, nb::object callback) {
                auto state = self.widget.state;
                return self.get().delete_requested().connect(
                    [state, callback = std::move(callback)](
                        termin::gui_native::FileGridWidget&, size_t index,
                        const termin::gui_native::CollectionItem& item) {
                        try {
                            nb::gil_scoped_acquire gil;
                            callback(index, item);
                        } catch (...) {
                            if (state && !state->pending_exception) {
                                state->pending_exception = std::current_exception();
                            }
                            tc_log_error(
                                "[termin-gui-native/python] FileGridWidget delete callback failed");
                        }
                    });
            },
            nb::arg("callback"))
        .def(
            "connect_context_menu_requested",
            [](const FileGridWidgetRef& self, nb::object callback) {
                auto state = self.widget.state;
                return self.get().context_menu_requested().connect([state,
                                                                    callback = std::move(callback)](
                                                                       termin::gui_native::
                                                                           FileGridWidget&,
                                                                       int64_t index, float x,
                                                                       float y) {
                    try {
                        nb::gil_scoped_acquire gil;
                        callback(index, x, y);
                    } catch (...) {
                        if (state && !state->pending_exception) {
                            state->pending_exception = std::current_exception();
                        }
                        tc_log_error(
                            "[termin-gui-native/python] FileGridWidget context callback failed");
                    }
                });
            },
            nb::arg("callback"))
        .def(
            "connect_drag_requested",
            [](const FileGridWidgetRef& self, nb::object callback) {
                auto state = self.widget.state;
                return self.get().drag_requested().connect(
                    [state, callback = std::move(callback)](
                        termin::gui_native::FileGridWidget&, size_t index, float x, float y,
                        int32_t modifiers) {
                        try {
                            nb::gil_scoped_acquire gil;
                            callback(index, x, y, modifiers);
                        } catch (...) {
                            if (state && !state->pending_exception)
                                state->pending_exception = std::current_exception();
                            tc_log_error(
                                "[termin-gui-native/python] FileGridWidget drag callback failed");
                        }
                    });
            },
            nb::arg("callback"));

    nb::class_<termin::gui_native::TreeNode>(m, "TreeNode")
        .def_prop_ro("id", [](const termin::gui_native::TreeNode& self) { return self.id; })
        .def_prop_ro("item", [](const termin::gui_native::TreeNode& self) { return self.item; })
        .def_prop_ro("parent", [](const termin::gui_native::TreeNode& self) { return self.parent; })
        .def_prop_ro("children",
                     [](const termin::gui_native::TreeNode& self) { return self.children; });

    nb::class_<termin::gui_native::TreeVisibleRow>(m, "TreeVisibleRow")
        .def_prop_ro("node",
                     [](const termin::gui_native::TreeVisibleRow& self) { return self.node; })
        .def_prop_ro("depth",
                     [](const termin::gui_native::TreeVisibleRow& self) { return self.depth; });

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
                     [](const termin::gui_native::TreeModel& self) { return self.roots(); })
        .def("contains", &termin::gui_native::TreeModel::contains, nb::arg("node"))
        .def(
            "node",
            [](const termin::gui_native::TreeModel& self, termin::gui_native::TreeNodeId node) {
                return self.node(node);
            },
            nb::arg("node"))
        .def(
            "children",
            [](const termin::gui_native::TreeModel& self, termin::gui_native::TreeNodeId parent) {
                return self.children(parent);
            },
            nb::arg("parent") = termin::gui_native::kInvalidTreeNodeId)
        .def("append_root", &termin::gui_native::TreeModel::append_root, nb::arg("item"))
        .def("append_child", &termin::gui_native::TreeModel::append_child, nb::arg("parent"),
             nb::arg("item"))
        .def("insert_child", &termin::gui_native::TreeModel::insert_child, nb::arg("parent"),
             nb::arg("index"), nb::arg("item"))
        .def("update", &termin::gui_native::TreeModel::update, nb::arg("node"), nb::arg("item"))
        .def("move", &termin::gui_native::TreeModel::move, nb::arg("node"),
             nb::arg("new_parent") = termin::gui_native::kInvalidTreeNodeId,
             nb::arg("index") = SIZE_MAX)
        .def("erase", &termin::gui_native::TreeModel::erase, nb::arg("node"))
        .def("clear", &termin::gui_native::TreeModel::clear);

    nb::class_<termin::gui_native::TreeExpansionModel>(m, "TreeExpansionModel")
        .def(nb::init<>())
        .def_prop_ro("revision", &termin::gui_native::TreeExpansionModel::revision)
        .def("expanded", &termin::gui_native::TreeExpansionModel::expanded, nb::arg("node"))
        .def("set_expanded", &termin::gui_native::TreeExpansionModel::set_expanded, nb::arg("node"),
             nb::arg("expanded"))
        .def("toggle", &termin::gui_native::TreeExpansionModel::toggle, nb::arg("node"))
        .def("clear", &termin::gui_native::TreeExpansionModel::clear)
        .def("reconcile", &termin::gui_native::TreeExpansionModel::reconcile, nb::arg("model"));

    nb::class_<TreeWidgetRef>(m, "TreeWidget")
        .def_prop_ro("widget", [](const TreeWidgetRef& self) { return self.widget; })
        .def_prop_ro("handle",
                     [](const TreeWidgetRef& self) { return WidgetHandle{self.widget.handle}; })
        .def_prop_rw(
            "model", [](const TreeWidgetRef& self) { return self.get().model(); },
            [](const TreeWidgetRef& self, std::shared_ptr<termin::gui_native::TreeModel> model) {
                self.get().set_model(std::move(model));
            })
        .def_prop_rw(
            "expansion_model",
            [](const TreeWidgetRef& self) { return self.get().expansion_model(); },
            [](const TreeWidgetRef& self,
               std::shared_ptr<termin::gui_native::TreeExpansionModel> expansion) {
                self.get().set_expansion_model(std::move(expansion));
            })
        .def_prop_ro("selected_node",
                     [](const TreeWidgetRef& self) { return self.get().selected_node(); })
        .def_prop_rw(
            "draggable", [](const TreeWidgetRef& self) { return self.get().draggable(); },
            [](const TreeWidgetRef& self, bool value) { self.get().set_draggable(value); })
        .def_prop_ro("dragging", [](const TreeWidgetRef& self) { return self.get().dragging(); })
        .def_prop_rw(
            "scroll_y", [](const TreeWidgetRef& self) { return self.get().scroll_y(); },
            [](const TreeWidgetRef& self, float value) { self.get().set_scroll_y(value); })
        .def_prop_ro("content_height",
                     [](const TreeWidgetRef& self) { return self.get().content_height(); })
        .def_prop_ro("visible_count",
                     [](const TreeWidgetRef& self) { return self.get().visible_count(); })
        .def_prop_ro("visible_range",
                     [](const TreeWidgetRef& self) {
                         const auto [first, last] = self.get().visible_range();
                         return std::vector<size_t>{first, last};
                     })
        .def(
            "visible_row",
            [](const TreeWidgetRef& self, size_t index) { return self.get().visible_row(index); },
            nb::arg("index"))
        .def(
            "set_row_height",
            [](const TreeWidgetRef& self, float height) { self.get().set_row_height(height); },
            nb::arg("height"))
        .def(
            "set_row_spacing",
            [](const TreeWidgetRef& self, float spacing) { self.get().set_row_spacing(spacing); },
            nb::arg("spacing"))
        .def(
            "set_indent_size",
            [](const TreeWidgetRef& self, float size) { self.get().set_indent_size(size); },
            nb::arg("size"))
        .def(
            "select",
            [](const TreeWidgetRef& self, termin::gui_native::TreeNodeId node, bool reveal) {
                const bool changed = self.get().select_node(node, reveal);
                self.widget.throw_pending_exception();
                return changed;
            },
            nb::arg("node"), nb::arg("reveal") = true)
        .def("clear_selection",
             [](const TreeWidgetRef& self) {
                 const bool changed = self.get().clear_selection();
                 self.widget.throw_pending_exception();
                 return changed;
             })
        .def(
            "set_expanded",
            [](const TreeWidgetRef& self, termin::gui_native::TreeNodeId node, bool expanded) {
                const bool changed = self.get().set_expanded(node, expanded);
                self.widget.throw_pending_exception();
                return changed;
            },
            nb::arg("node"), nb::arg("expanded"))
        .def(
            "toggle",
            [](const TreeWidgetRef& self, termin::gui_native::TreeNodeId node) {
                const bool changed = self.get().toggle(node);
                self.widget.throw_pending_exception();
                return changed;
            },
            nb::arg("node"))
        .def(
            "expanded",
            [](const TreeWidgetRef& self, termin::gui_native::TreeNodeId node) {
                return self.get().expanded(node);
            },
            nb::arg("node"))
        .def(
            "ensure_visible",
            [](const TreeWidgetRef& self, termin::gui_native::TreeNodeId node) {
                self.get().ensure_visible(node);
                self.widget.throw_pending_exception();
            },
            nb::arg("node"))
        .def(
            "connect_selection_changed",
            [](const TreeWidgetRef& self, nb::object callback) {
                auto state = self.widget.state;
                return self.get().selection_changed().connect(
                    [state, callback = std::move(callback)](termin::gui_native::TreeWidget&,
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
            [](const TreeWidgetRef& self, nb::object callback) {
                auto state = self.widget.state;
                return self.get().expansion_changed().connect(
                    [state, callback = std::move(callback)](termin::gui_native::TreeWidget&,
                                                            termin::gui_native::TreeNodeId node,
                                                            bool expanded) {
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
            [](const TreeWidgetRef& self, nb::object callback) {
                auto state = self.widget.state;
                return self.get().activated().connect(
                    [state, callback = std::move(callback)](
                        termin::gui_native::TreeWidget&, termin::gui_native::TreeNodeId node,
                        const termin::gui_native::CollectionItem& item) {
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
            [](const TreeWidgetRef& self, nb::object callback) {
                auto state = self.widget.state;
                return self.get().delete_requested().connect(
                    [state, callback = std::move(callback)](
                        termin::gui_native::TreeWidget&, termin::gui_native::TreeNodeId node,
                        const termin::gui_native::CollectionItem& item) {
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
            [](const TreeWidgetRef& self, nb::object callback) {
                auto state = self.widget.state;
                return self.get().context_menu_requested().connect(
                    [state, callback = std::move(callback)](termin::gui_native::TreeWidget&,
                                                            termin::gui_native::TreeNodeId node,
                                                            float x, float y) {
                        try {
                            nb::gil_scoped_acquire gil;
                            callback(node, x, y);
                        } catch (...) {
                            if (state && !state->pending_exception)
                                state->pending_exception = std::current_exception();
                            tc_log_error(
                                "[termin-gui-native/python] TreeWidget context callback failed");
                        }
                    });
            },
            nb::arg("callback"))
        .def(
            "connect_drop_requested",
            [](const TreeWidgetRef& self, nb::object callback) {
                auto state = self.widget.state;
                return self.get().drop_requested().connect(
                    [state, callback = std::move(callback)](
                        termin::gui_native::TreeWidget&, termin::gui_native::TreeNodeId dragged,
                        termin::gui_native::TreeNodeId target,
                        termin::gui_native::TreeDropPosition position) {
                        try {
                            nb::gil_scoped_acquire gil;
                            callback(dragged, target, position);
                        } catch (...) {
                            if (state && !state->pending_exception)
                                state->pending_exception = std::current_exception();
                            tc_log_error(
                                "[termin-gui-native/python] TreeWidget drop callback failed");
                        }
                    });
            },
            nb::arg("callback"));

    nb::class_<termin::gui_native::TableRowData>(m, "TableRowData")
        .def(nb::init<>())
        .def(
            "__init__",
            [](termin::gui_native::TableRowData* self, std::string stable_id,
               std::vector<std::string> cells, bool enabled) {
                new (self) termin::gui_native::TableRowData{std::move(stable_id), std::move(cells),
                                                            enabled};
            },
            nb::arg("stable_id"), nb::arg("cells"), nb::arg("enabled") = true)
        .def_rw("stable_id", &termin::gui_native::TableRowData::stable_id)
        .def_rw("cells", &termin::gui_native::TableRowData::cells)
        .def_rw("enabled", &termin::gui_native::TableRowData::enabled);

    nb::class_<termin::gui_native::TableRow>(m, "TableRow")
        .def_prop_ro("id", [](const termin::gui_native::TableRow& self) { return self.id; })
        .def_prop_ro("data", [](const termin::gui_native::TableRow& self) { return self.data; });

    nb::class_<termin::gui_native::TableModel>(m, "TableModel")
        .def(nb::init<>())
        .def_prop_ro("row_count", &termin::gui_native::TableModel::size)
        .def_prop_ro("revision", &termin::gui_native::TableModel::revision)
        .def_prop_ro("rows", [](const termin::gui_native::TableModel& self) { return self.rows(); })
        .def("contains", &termin::gui_native::TableModel::contains, nb::arg("row"))
        .def("index_of", &termin::gui_native::TableModel::index_of, nb::arg("row"))
        .def(
            "row_at",
            [](const termin::gui_native::TableModel& self, size_t index) {
                return self.row_at(index);
            },
            nb::arg("index"))
        .def(
            "row",
            [](const termin::gui_native::TableModel& self, termin::gui_native::TableRowId row) {
                return self.row(row);
            },
            nb::arg("row"))
        .def("set_rows", &termin::gui_native::TableModel::set_rows, nb::arg("rows"))
        .def("append", &termin::gui_native::TableModel::append, nb::arg("row"))
        .def("insert", &termin::gui_native::TableModel::insert, nb::arg("index"), nb::arg("row"))
        .def("update", &termin::gui_native::TableModel::update, nb::arg("row"), nb::arg("data"))
        .def("erase", &termin::gui_native::TableModel::erase, nb::arg("row"))
        .def("clear", &termin::gui_native::TableModel::clear);

    nb::enum_<termin::gui_native::TableColumnPolicy>(m, "TableColumnPolicy")
        .value("Fixed", termin::gui_native::TableColumnPolicy::Fixed)
        .value("Stretch", termin::gui_native::TableColumnPolicy::Stretch);

    nb::class_<termin::gui_native::TableColumn>(m, "TableColumn")
        .def(nb::init<>())
        .def(
            "__init__",
            [](termin::gui_native::TableColumn* self, std::string stable_id, std::string header,
               termin::gui_native::TableColumnPolicy policy, float width, float min_width,
               float max_width, float stretch, bool resizable) {
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
            nb::arg("width") = 0.0f, nb::arg("min_width") = 40.0f, nb::arg("max_width") = 0.0f,
            nb::arg("stretch") = 1.0f, nb::arg("resizable") = true)
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
        .def_prop_ro(
            "columns",
            [](const termin::gui_native::TableColumnModel& self) { return self.columns(); })
        .def(
            "column",
            [](const termin::gui_native::TableColumnModel& self, size_t index) {
                return self.column(index);
            },
            nb::arg("index"))
        .def("set_columns", &termin::gui_native::TableColumnModel::set_columns, nb::arg("columns"))
        .def("append", &termin::gui_native::TableColumnModel::append, nb::arg("column"))
        .def("insert", &termin::gui_native::TableColumnModel::insert, nb::arg("index"),
             nb::arg("column"))
        .def("update", &termin::gui_native::TableColumnModel::update, nb::arg("index"),
             nb::arg("column"))
        .def("resize", &termin::gui_native::TableColumnModel::resize, nb::arg("index"),
             nb::arg("width"))
        .def("erase", &termin::gui_native::TableColumnModel::erase, nb::arg("index"))
        .def("clear", &termin::gui_native::TableColumnModel::clear);

    nb::class_<termin::gui_native::TableColumnLayout>(m, "TableColumnLayout")
        .def_prop_ro("x", [](const termin::gui_native::TableColumnLayout& self) { return self.x; })
        .def_prop_ro("width",
                     [](const termin::gui_native::TableColumnLayout& self) { return self.width; });

    nb::class_<TableWidgetRef>(m, "TableWidget")
        .def_prop_ro("widget", [](const TableWidgetRef& self) { return self.widget; })
        .def_prop_ro("handle",
                     [](const TableWidgetRef& self) { return WidgetHandle{self.widget.handle}; })
        .def_prop_rw(
            "model", [](const TableWidgetRef& self) { return self.get().model(); },
            [](const TableWidgetRef& self, std::shared_ptr<termin::gui_native::TableModel> model) {
                self.get().set_model(std::move(model));
            })
        .def_prop_rw(
            "column_model", [](const TableWidgetRef& self) { return self.get().column_model(); },
            [](const TableWidgetRef& self,
               std::shared_ptr<termin::gui_native::TableColumnModel> columns) {
                self.get().set_column_model(std::move(columns));
            })
        .def_prop_rw(
            "selection_mode",
            [](const TableWidgetRef& self) { return self.get().selection().mode(); },
            [](const TableWidgetRef& self, termin::gui_native::SelectionMode mode) {
                self.get().set_selection_mode(mode);
                self.widget.throw_pending_exception();
            })
        .def_prop_ro(
            "selected_indices",
            [](const TableWidgetRef& self) { return self.get().selection().selected_indices(); })
        .def_prop_ro("current_index",
                     [](const TableWidgetRef& self) -> int64_t {
                         const size_t index = self.get().selection().current();
                         return index == termin::gui_native::SelectionModel::npos
                                    ? -1
                                    : static_cast<int64_t>(index);
                     })
        .def_prop_rw(
            "scroll_y", [](const TableWidgetRef& self) { return self.get().scroll_y(); },
            [](const TableWidgetRef& self, float value) { self.get().set_scroll_y(value); })
        .def_prop_ro("content_height",
                     [](const TableWidgetRef& self) { return self.get().content_height(); })
        .def_prop_ro("visible_range",
                     [](const TableWidgetRef& self) {
                         const auto [first, last] = self.get().visible_range();
                         return std::vector<size_t>{first, last};
                     })
        .def_prop_ro("column_layout",
                     [](const TableWidgetRef& self) { return self.get().column_layout(); })
        .def(
            "set_row_height",
            [](const TableWidgetRef& self, float height) { self.get().set_row_height(height); },
            nb::arg("height"))
        .def(
            "set_header_height",
            [](const TableWidgetRef& self, float height) { self.get().set_header_height(height); },
            nb::arg("height"))
        .def(
            "select",
            [](const TableWidgetRef& self, size_t index, bool toggle, bool extend, bool additive) {
                const bool changed = self.get().select_row(index, toggle, extend, additive);
                self.widget.throw_pending_exception();
                return changed;
            },
            nb::arg("index"), nb::arg("toggle") = false, nb::arg("extend") = false,
            nb::arg("additive") = false)
        .def("clear_selection",
             [](const TableWidgetRef& self) {
                 const bool changed = self.get().clear_selection();
                 self.widget.throw_pending_exception();
                 return changed;
             })
        .def(
            "ensure_visible",
            [](const TableWidgetRef& self, size_t index) { self.get().ensure_visible(index); },
            nb::arg("index"))
        .def(
            "connect_selection_changed",
            [](const TableWidgetRef& self, nb::object callback) {
                auto state = self.widget.state;
                return self.get().selection_changed().connect(
                    [state, callback = std::move(callback)](termin::gui_native::TableWidget&,
                                                            const std::vector<size_t>& selected) {
                        try {
                            nb::gil_scoped_acquire gil;
                            callback(selected);
                        } catch (...) {
                            if (state && !state->pending_exception) {
                                state->pending_exception = std::current_exception();
                            }
                            tc_log_error(
                                "[termin-gui-native/python] TableWidget selection callback failed");
                        }
                    });
            },
            nb::arg("callback"))
        .def(
            "connect_activated",
            [](const TableWidgetRef& self, nb::object callback) {
                auto state = self.widget.state;
                return self.get().activated().connect([state, callback = std::move(callback)](
                                                          termin::gui_native::TableWidget&,
                                                          size_t index,
                                                          termin::gui_native::TableRowId row,
                                                          const termin::gui_native::TableRowData&
                                                              data) {
                    try {
                        nb::gil_scoped_acquire gil;
                        callback(index, row, data);
                    } catch (...) {
                        if (state && !state->pending_exception) {
                            state->pending_exception = std::current_exception();
                        }
                        tc_log_error(
                            "[termin-gui-native/python] TableWidget activation callback failed");
                    }
                });
            },
            nb::arg("callback"))
        .def(
            "connect_header_clicked",
            [](const TableWidgetRef& self, nb::object callback) {
                auto state = self.widget.state;
                return self.get().header_clicked().connect(
                    [state, callback = std::move(callback)](
                        termin::gui_native::TableWidget&, size_t index,
                        const termin::gui_native::TableColumn& column) {
                        try {
                            nb::gil_scoped_acquire gil;
                            callback(index, column);
                        } catch (...) {
                            if (state && !state->pending_exception) {
                                state->pending_exception = std::current_exception();
                            }
                            tc_log_error(
                                "[termin-gui-native/python] TableWidget header callback failed");
                        }
                    });
            },
            nb::arg("callback"))
        .def(
            "connect_column_resized",
            [](const TableWidgetRef& self, nb::object callback) {
                auto state = self.widget.state;
                return self.get().column_resized().connect(
                    [state, callback = std::move(callback)](termin::gui_native::TableWidget&,
                                                            size_t index, float width) {
                        try {
                            nb::gil_scoped_acquire gil;
                            callback(index, width);
                        } catch (...) {
                            if (state && !state->pending_exception) {
                                state->pending_exception = std::current_exception();
                            }
                            tc_log_error(
                                "[termin-gui-native/python] TableWidget resize callback failed");
                        }
                    });
            },
            nb::arg("callback"))
        .def(
            "connect_context_menu_requested",
            [](const TableWidgetRef& self, nb::object callback) {
                auto state = self.widget.state;
                return self.get().context_menu_requested().connect(
                    [state, callback = std::move(callback)](termin::gui_native::TableWidget&,
                                                            int64_t index, float x, float y) {
                        try {
                            nb::gil_scoped_acquire gil;
                            callback(index, x, y);
                        } catch (...) {
                            if (state && !state->pending_exception)
                                state->pending_exception = std::current_exception();
                            tc_log_error(
                                "[termin-gui-native/python] TableWidget context callback failed");
                        }
                    });
            },
            nb::arg("callback"));

    nb::class_<ComboBoxRef>(m, "ComboBox")
        .def_prop_ro("widget", [](const ComboBoxRef& self) { return self.widget; })
        .def_prop_ro("handle",
                     [](const ComboBoxRef& self) { return WidgetHandle{self.widget.handle}; })
        .def_prop_ro("item_count", [](const ComboBoxRef& self) { return self.get().item_count(); })
        .def_prop_rw(
            "selected_index", [](const ComboBoxRef& self) { return self.get().selected_index(); },
            [](const ComboBoxRef& self, int value) {
                self.get().set_selected_index(value);
                self.widget.throw_pending_exception();
            })
        .def_prop_ro("selected_text",
                     [](const ComboBoxRef& self) { return self.get().selected_text(); })
        .def_prop_ro("open", [](const ComboBoxRef& self) { return self.get().open(); })
        .def(
            "add_item",
            [](const ComboBoxRef& self, const std::string& item) { self.get().add_item(item); },
            nb::arg("item"))
        .def(
            "item_text",
            [](const ComboBoxRef& self, size_t index) { return self.get().item_text(index); },
            nb::arg("index"))
        .def("clear", [](const ComboBoxRef& self) { self.get().clear_items(); })
        .def(
            "connect_changed",
            [](const ComboBoxRef& self, nb::object callback) {
                auto state = self.widget.state;
                return self.get().changed().connect([state, callback = std::move(callback)](
                                                        termin::gui_native::ComboBox&, int index,
                                                        const std::string& text) {
                    try {
                        nb::gil_scoped_acquire gil;
                        callback(index, text);
                    } catch (...) {
                        if (state && !state->pending_exception)
                            state->pending_exception = std::current_exception();
                        tc_log_error("[termin-gui-native/python] ComboBox changed callback failed");
                    }
                });
            },
            nb::arg("callback"));

    nb::class_<IconButtonRef>(m, "IconButton")
        .def_prop_ro("widget", [](const IconButtonRef& self) { return self.widget; })
        .def_prop_ro("handle", [](const IconButtonRef& self) { return WidgetHandle {self.widget.handle}; })
        .def_prop_rw("active",
            [](const IconButtonRef& self) { return self.get().active(); },
            [](const IconButtonRef& self, bool value) { self.get().set_active(value); })
        .def("set_icon", [](const IconButtonRef& self, const std::string& icon) {
            self.get().set_icon(icon);
        }, nb::arg("icon"))
        .def("set_texture", [](const IconButtonRef& self, tgfx::TextureHandle texture) {
            self.get().set_texture(texture.id);
        }, nb::arg("texture"))
        .def("connect_clicked", [](const IconButtonRef& self, nb::object callback) {
            auto state = self.widget.state;
            return self.get().clicked().connect([state, callback = std::move(callback)](
                                                   termin::gui_native::IconButton&) {
                try {
                    nb::gil_scoped_acquire gil;
                    callback();
                } catch (...) {
                    if (state && !state->pending_exception) state->pending_exception = std::current_exception();
                    tc_log_error("[termin-gui-native/python] IconButton click callback failed");
                }
            });
        }, nb::arg("callback"));

    nb::class_<ProgressBarRef>(m, "ProgressBar")
        .def_prop_ro("widget", [](const ProgressBarRef& self) { return self.widget; })
        .def_prop_ro("handle", [](const ProgressBarRef& self) {
            return WidgetHandle {self.widget.handle};
        })
        .def_prop_rw("value", [](const ProgressBarRef& self) { return self.get().value(); },
                     [](const ProgressBarRef& self, float value) {
                         self.get().set_value(value);
                     });

    nb::class_<ImageWidgetRef>(m, "ImageWidget")
        .def_prop_ro("widget", [](const ImageWidgetRef& self) { return self.widget; })
        .def_prop_ro("handle", [](const ImageWidgetRef& self) { return WidgetHandle {self.widget.handle}; })
        .def_prop_ro("intrinsic_size", [](const ImageWidgetRef& self) {
            return self.get().intrinsic_size();
        })
        .def("set_texture", [](const ImageWidgetRef& self,
                                tgfx::TextureHandle texture,
                                std::optional<tc_ui_size> intrinsic_size) {
            self.get().set_texture(texture.id, intrinsic_size.value_or(tc_ui_size{}));
        }, nb::arg("texture"), nb::arg("intrinsic_size").none() = nb::none())
        .def("clear_texture", [](const ImageWidgetRef& self) {
            self.get().clear_texture();
        })
        .def("set_tint", [](const ImageWidgetRef& self, tc_ui_color tint) {
            self.get().set_tint(termin::gui_native::Color {tint.r, tint.g, tint.b, tint.a});
        }, nb::arg("tint"))
        .def("set_preserve_aspect", [](const ImageWidgetRef& self, bool preserve) {
            self.get().set_preserve_aspect(preserve);
        }, nb::arg("preserve"));

    nb::class_<CanvasRef>(m, "Canvas")
        .def_prop_ro("widget", [](const CanvasRef& self) { return self.widget; })
        .def_prop_ro("handle", [](const CanvasRef& self) { return WidgetHandle {self.widget.handle}; })
        .def_prop_ro("zoom", [](const CanvasRef& self) { return self.get().zoom(); })
        .def("set_texture", [](const CanvasRef& self,
                                tgfx::TextureHandle texture,
                                std::optional<tc_ui_size> image_size) {
            self.get().set_texture(texture.id, image_size.value_or(tc_ui_size{}));
        }, nb::arg("texture"), nb::arg("image_size").none() = nb::none())
        .def("set_overlay_texture", [](const CanvasRef& self, tgfx::TextureHandle texture) {
            self.get().set_overlay_texture(texture.id);
        }, nb::arg("texture"))
        .def("set_zoom", [](const CanvasRef& self, float zoom, tc_ui_point anchor) {
            self.get().set_zoom(zoom, anchor);
        }, nb::arg("zoom"), nb::arg("anchor"))
        .def("fit_in_view", [](const CanvasRef& self) { self.get().fit_in_view(); })
        .def("widget_to_image", [](const CanvasRef& self, tc_ui_point point) {
            return self.get().widget_to_image(point);
        }, nb::arg("point"))
        .def("image_to_widget", [](const CanvasRef& self, tc_ui_point point) {
            return self.get().image_to_widget(point);
        }, nb::arg("point"))
        .def("set_paint_callback", [](const CanvasRef& self, nb::object callback) {
            if (callback.is_none()) {
                self.get().set_paint_callback({});
                return;
            }
            auto state = self.widget.state;
            self.get().set_paint_callback(
                [state, callback = std::move(callback)](
                    termin::gui_native::Canvas&,
                    tc_ui_paint_context* context) {
                    try {
                        nb::gil_scoped_acquire gil;
                        PaintContext borrowed(context, false);
                        callback(std::move(borrowed));
                    } catch (...) {
                        if (state && !state->pending_exception) state->pending_exception = std::current_exception();
                        tc_log_error("[termin-gui-native/python] Canvas paint callback failed");
                    }
                }
            );
        }, nb::arg("callback"));

    nb::enum_<tc_ui_draw_command_type>(m, "DrawCommandType")
        .value("FillRect", TC_UI_DRAW_FILL_RECT)
        .value("StrokeRect", TC_UI_DRAW_STROKE_RECT)
        .value("Line", TC_UI_DRAW_LINE)
        .value("PushClip", TC_UI_DRAW_PUSH_CLIP)
        .value("PopClip", TC_UI_DRAW_POP_CLIP)
        .value("Text", TC_UI_DRAW_TEXT)
        .value("FillRoundedRect", TC_UI_DRAW_FILL_ROUNDED_RECT)
        .value("StrokeRoundedRect", TC_UI_DRAW_STROKE_ROUNDED_RECT)
        .value("FillCircle", TC_UI_DRAW_FILL_CIRCLE)
        .value("StrokeCircle", TC_UI_DRAW_STROKE_CIRCLE)
        .value("Arc", TC_UI_DRAW_ARC)
        .value("Polyline", TC_UI_DRAW_POLYLINE)
        .value("Texture", TC_UI_DRAW_TEXTURE);

}
