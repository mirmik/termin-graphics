#include "gui_native_bindings_shared.hpp"

using namespace termin::gui_native::python_bindings;

void bind_gui_native_scene_views(nb::module_& m) {
    nb::class_<termin::gui_native::SceneTransform>(m, "SceneTransform")
        .def(nb::init<>())
        .def(
            "__init__",
            [](termin::gui_native::SceneTransform* self, float origin_x, float origin_y, float zoom) {
                new (self) termin::gui_native::SceneTransform{origin_x, origin_y, zoom};
            },
            nb::arg("origin_x"),
            nb::arg("origin_y"),
            nb::arg("zoom"))
        .def_rw("origin_x", &termin::gui_native::SceneTransform::origin_x)
        .def_rw("origin_y", &termin::gui_native::SceneTransform::origin_y)
        .def_rw("zoom", &termin::gui_native::SceneTransform::zoom)
        .def("world_to_screen", &termin::gui_native::SceneTransform::world_to_screen, nb::arg("point"))
        .def("screen_to_world", &termin::gui_native::SceneTransform::screen_to_world, nb::arg("point"));

    nb::class_<termin::gui_native::CollectionItem>(m, "CollectionItem")
        .def(nb::init<>())
        .def(
            "__init__",
            [](termin::gui_native::CollectionItem* self,
               std::string stable_id,
               std::string text,
               std::string subtitle,
               bool enabled,
               uint32_t texture_id,
               std::string icon,
               bool primary_toggle,
               bool primary_checked,
               std::string primary_toggle_label,
               bool secondary_toggle,
               bool secondary_checked,
               std::string secondary_toggle_label) {
                new (self) termin::gui_native::CollectionItem{std::move(stable_id),
                                                              std::move(text),
                                                              std::move(subtitle),
                                                              enabled,
                                                              texture_id,
                                                              std::move(icon),
                                                              primary_toggle,
                                                              primary_checked,
                                                              std::move(primary_toggle_label),
                                                              secondary_toggle,
                                                              secondary_checked,
                                                              std::move(secondary_toggle_label)};
            },
            nb::arg("stable_id"),
            nb::arg("text"),
            nb::arg("subtitle") = "",
            nb::arg("enabled") = true,
            nb::arg("texture_id") = 0,
            nb::arg("icon") = "",
            nb::arg("primary_toggle") = false,
            nb::arg("primary_checked") = false,
            nb::arg("primary_toggle_label") = "",
            nb::arg("secondary_toggle") = false,
            nb::arg("secondary_checked") = false,
            nb::arg("secondary_toggle_label") = "")
        .def_rw("stable_id", &termin::gui_native::CollectionItem::stable_id)
        .def_rw("text", &termin::gui_native::CollectionItem::text)
        .def_rw("subtitle", &termin::gui_native::CollectionItem::subtitle)
        .def_rw("enabled", &termin::gui_native::CollectionItem::enabled)
        .def_rw("texture_id", &termin::gui_native::CollectionItem::texture_id)
        .def_rw("icon", &termin::gui_native::CollectionItem::icon)
        .def_rw("primary_toggle", &termin::gui_native::CollectionItem::primary_toggle)
        .def_rw("primary_checked", &termin::gui_native::CollectionItem::primary_checked)
        .def_rw("primary_toggle_label", &termin::gui_native::CollectionItem::primary_toggle_label)
        .def_rw("secondary_toggle", &termin::gui_native::CollectionItem::secondary_toggle)
        .def_rw("secondary_checked", &termin::gui_native::CollectionItem::secondary_checked)
        .def_rw("secondary_toggle_label", &termin::gui_native::CollectionItem::secondary_toggle_label);

    nb::class_<termin::gui_native::CollectionModel>(m, "CollectionModel")
        .def(nb::init<>())
        .def_prop_ro("item_count", &termin::gui_native::CollectionModel::size)
        .def_prop_ro("revision", &termin::gui_native::CollectionModel::revision)
        .def_prop_ro("items", [](const termin::gui_native::CollectionModel& self) { return self.items(); })
        .def(
            "item",
            [](const termin::gui_native::CollectionModel& self, size_t index) { return self.item(index); },
            nb::arg("index"))
        .def("set_items", &termin::gui_native::CollectionModel::set_items, nb::arg("items"))
        .def("append", &termin::gui_native::CollectionModel::append, nb::arg("item"))
        .def("update", &termin::gui_native::CollectionModel::update, nb::arg("index"), nb::arg("item"))
        .def("erase", &termin::gui_native::CollectionModel::erase, nb::arg("index"))
        .def("clear", &termin::gui_native::CollectionModel::clear);

    nb::class_<termin::gui_native::RichTextStyle>(m, "RichTextStyle")
        .def(nb::init<>())
        .def(
            "__init__",
            [](termin::gui_native::RichTextStyle* self, std::optional<tc_ui_srgb_color> color, bool bold, bool italic) {
                new (self) termin::gui_native::RichTextStyle{std::move(color), bold, italic};
            },
            nb::arg("color") = nb::none(),
            nb::arg("bold") = false,
            nb::arg("italic") = false)
        .def_rw("color", &termin::gui_native::RichTextStyle::color)
        .def_rw("bold", &termin::gui_native::RichTextStyle::bold)
        .def_rw("italic", &termin::gui_native::RichTextStyle::italic);

    nb::class_<termin::gui_native::RichTextSegment>(m, "RichTextSegment")
        .def(nb::init<>())
        .def(
            "__init__",
            [](termin::gui_native::RichTextSegment* self,
               std::string text,
               std::optional<termin::gui_native::RichTextStyle> style) {
                new (self) termin::gui_native::RichTextSegment{std::move(text),
                                                               style.value_or(termin::gui_native::RichTextStyle{})};
            },
            nb::arg("text"),
            nb::arg("style").none() = nb::none())
        .def_rw("text", &termin::gui_native::RichTextSegment::text)
        .def_rw("style", &termin::gui_native::RichTextSegment::style);

    nb::class_<termin::gui_native::RichTextModel>(m, "RichTextModel")
        .def(nb::init<>())
        .def_prop_ro("revision", &termin::gui_native::RichTextModel::revision)
        .def_prop_ro("text", &termin::gui_native::RichTextModel::text)
        .def_prop_ro("lines", [](const termin::gui_native::RichTextModel& self) { return self.lines(); })
        .def("set_text", &termin::gui_native::RichTextModel::set_text, nb::arg("text"))
        .def("set_lines", &termin::gui_native::RichTextModel::set_lines, nb::arg("lines"))
        .def("set_html", &termin::gui_native::RichTextModel::set_html, nb::arg("html"))
        .def("clear", &termin::gui_native::RichTextModel::clear);

    nb::class_<RichTextViewRef>(m, "RichTextView")
        .def_prop_ro("widget", [](const RichTextViewRef& self) { return self.widget; })
        .def_prop_ro("handle", [](const RichTextViewRef& self) { return WidgetHandle{self.widget.handle}; })
        .def_prop_rw(
            "model",
            [](const RichTextViewRef& self) { return self.get().model(); },
            [](const RichTextViewRef& self, std::shared_ptr<termin::gui_native::RichTextModel> model) {
                self.get().set_model(std::move(model));
            })
        .def_prop_rw(
            "placeholder",
            [](const RichTextViewRef& self) { return self.get().placeholder(); },
            [](const RichTextViewRef& self, const std::string& value) { self.get().set_placeholder(value); })
        .def_prop_rw(
            "word_wrap",
            [](const RichTextViewRef& self) { return self.get().word_wrap(); },
            [](const RichTextViewRef& self, bool value) { self.get().set_word_wrap(value); })
        .def_prop_rw(
            "show_scrollbar",
            [](const RichTextViewRef& self) { return self.get().show_scrollbar(); },
            [](const RichTextViewRef& self, bool value) { self.get().set_show_scrollbar(value); })
        .def_prop_rw(
            "line_height",
            [](const RichTextViewRef& self) { return self.get().line_height(); },
            [](const RichTextViewRef& self, float value) { self.get().set_line_height(value); })
        .def_prop_rw(
            "scroll_y",
            [](const RichTextViewRef& self) { return self.get().scroll_y(); },
            [](const RichTextViewRef& self, float value) { self.get().set_scroll_y(value); })
        .def_prop_ro("content_height", [](const RichTextViewRef& self) { return self.get().content_height(); })
        .def_prop_ro("visual_line_count", [](const RichTextViewRef& self) { return self.get().visual_line_count(); })
        .def_prop_ro("has_selection", [](const RichTextViewRef& self) { return self.get().has_selection(); })
        .def_prop_ro("selected_text", [](const RichTextViewRef& self) { return self.get().selected_text(); })
        .def(
            "select",
            [](const RichTextViewRef& self, size_t anchor, size_t cursor) { self.get().select(anchor, cursor); },
            nb::arg("anchor"),
            nb::arg("cursor"))
        .def("select_all", [](const RichTextViewRef& self) { self.get().select_all(); })
        .def("clear_selection", [](const RichTextViewRef& self) { self.get().clear_selection(); });

    nb::class_<termin::gui_native::FrameTimeModel>(m, "FrameTimeModel")
        .def(nb::init<>())
        .def_prop_rw(
            "max_samples",
            [](const termin::gui_native::FrameTimeModel& self) { return self.max_samples(); },
            &termin::gui_native::FrameTimeModel::set_max_samples)
        .def_prop_ro("samples", [](const termin::gui_native::FrameTimeModel& self) { return self.samples(); })
        .def_prop_ro("revision", &termin::gui_native::FrameTimeModel::revision)
        .def("add_sample", &termin::gui_native::FrameTimeModel::add_sample, nb::arg("milliseconds"))
        .def("set_samples", &termin::gui_native::FrameTimeModel::set_samples, nb::arg("samples"))
        .def("clear", &termin::gui_native::FrameTimeModel::clear);

    nb::class_<FrameTimeGraphRef>(m, "FrameTimeGraph")
        .def_prop_ro("widget", [](const FrameTimeGraphRef& self) { return self.widget; })
        .def_prop_ro("handle", [](const FrameTimeGraphRef& self) { return WidgetHandle{self.widget.handle}; })
        .def_prop_rw(
            "model",
            [](const FrameTimeGraphRef& self) { return self.get().model(); },
            [](const FrameTimeGraphRef& self, std::shared_ptr<termin::gui_native::FrameTimeModel> model) {
                self.get().set_model(std::move(model));
            })
        .def_prop_ro("target_frame_ms", [](const FrameTimeGraphRef& self) { return self.get().target_frame_ms(); })
        .def_prop_ro("warning_frame_ms", [](const FrameTimeGraphRef& self) { return self.get().warning_frame_ms(); })
        .def(
            "set_thresholds",
            [](const FrameTimeGraphRef& self, float target, float warning) {
                self.get().set_thresholds(target, warning);
            },
            nb::arg("target_frame_ms"),
            nb::arg("warning_frame_ms"));

    nb::class_<termin::gui_native::FrameTimelineSample>(m, "FrameTimelineSample")
        .def(nb::init<>())
        .def(
            "__init__",
            [](termin::gui_native::FrameTimelineSample* self,
               int64_t stable_id,
               float interval_ms,
               float active_ms,
               float lateness_ms,
               float target_ms,
               bool hitch,
               bool gap_before) {
                new (self) termin::gui_native::FrameTimelineSample{
                    stable_id, interval_ms, active_ms, lateness_ms, target_ms, hitch, gap_before};
            },
            nb::arg("stable_id"),
            nb::arg("interval_ms"),
            nb::arg("active_ms"),
            nb::arg("lateness_ms") = 0.0f,
            nb::arg("target_ms") = 0.0f,
            nb::arg("hitch") = false,
            nb::arg("gap_before") = false)
        .def_rw("stable_id", &termin::gui_native::FrameTimelineSample::stable_id)
        .def_rw("interval_ms", &termin::gui_native::FrameTimelineSample::interval_ms)
        .def_rw("active_ms", &termin::gui_native::FrameTimelineSample::active_ms)
        .def_rw("lateness_ms", &termin::gui_native::FrameTimelineSample::lateness_ms)
        .def_rw("target_ms", &termin::gui_native::FrameTimelineSample::target_ms)
        .def_rw("hitch", &termin::gui_native::FrameTimelineSample::hitch)
        .def_rw("gap_before", &termin::gui_native::FrameTimelineSample::gap_before);

    nb::class_<termin::gui_native::FrameTimelineModel>(m, "FrameTimelineModel")
        .def(nb::init<>())
        .def_prop_ro("samples", [](const termin::gui_native::FrameTimelineModel& self) { return self.samples(); })
        .def_prop_ro("revision", &termin::gui_native::FrameTimelineModel::revision)
        .def("set_samples", &termin::gui_native::FrameTimelineModel::set_samples, nb::arg("samples"))
        .def("append_samples",
             &termin::gui_native::FrameTimelineModel::append_samples,
             nb::arg("samples"),
             nb::arg("max_samples") = 0)
        .def("clear", &termin::gui_native::FrameTimelineModel::clear);

    nb::class_<FrameTimelineWidgetRef>(m, "FrameTimelineWidget")
        .def_prop_ro("widget", [](const FrameTimelineWidgetRef& self) { return self.widget; })
        .def_prop_ro("handle", [](const FrameTimelineWidgetRef& self) { return WidgetHandle{self.widget.handle}; })
        .def_prop_rw(
            "model",
            [](const FrameTimelineWidgetRef& self) { return self.get().model(); },
            [](const FrameTimelineWidgetRef& self, std::shared_ptr<termin::gui_native::FrameTimelineModel> model) {
                self.get().set_model(std::move(model));
            })
        .def_prop_rw(
            "window_size",
            [](const FrameTimelineWidgetRef& self) { return self.get().window_size(); },
            [](const FrameTimelineWidgetRef& self, size_t count) { self.get().set_window_size(count); })
        .def_prop_rw(
            "scroll_offset",
            [](const FrameTimelineWidgetRef& self) { return self.get().scroll_offset(); },
            [](const FrameTimelineWidgetRef& self, size_t count) { self.get().set_scroll_offset(count); })
        .def_prop_rw(
            "follow_latest",
            [](const FrameTimelineWidgetRef& self) { return self.get().follow_latest(); },
            [](const FrameTimelineWidgetRef& self, bool enabled) { self.get().set_follow_latest(enabled); })
        .def_prop_ro("selected_id", [](const FrameTimelineWidgetRef& self) { return self.get().selected_id(); })
        .def_prop_ro("visible_range",
                     [](const FrameTimelineWidgetRef& self) {
                         const auto [first, last] = self.get().visible_range();
                         return std::vector<size_t>{first, last};
                     })
        .def(
            "select",
            [](const FrameTimelineWidgetRef& self, int64_t stable_id) {
                const bool changed = self.get().select(stable_id);
                self.widget.throw_pending_exception();
                return changed;
            },
            nb::arg("stable_id"))
        .def("clear_selection",
             [](const FrameTimelineWidgetRef& self) {
                 const bool changed = self.get().clear_selection();
                 self.widget.throw_pending_exception();
                 return changed;
             })
        .def(
            "set_warning_ratio",
            [](const FrameTimelineWidgetRef& self, float ratio) { self.get().set_warning_ratio(ratio); },
            nb::arg("ratio"))
        .def(
            "connect_selection_changed",
            [](const FrameTimelineWidgetRef& self, nb::object callback) {
                auto state = self.widget.state;
                return self.get().selection_changed().connect(
                    [state, callback = std::move(callback)](termin::gui_native::FrameTimelineWidget&,
                                                            int64_t stable_id) {
                        try {
                            nb::gil_scoped_acquire gil;
                            callback(stable_id);
                        } catch (...) {
                            if (state && !state->pending_exception)
                                state->pending_exception = std::current_exception();
                            tc_log_error("[termin-gui-native/python] FrameTimelineWidget "
                                         "selection callback failed");
                        }
                    });
            },
            nb::arg("callback"))
        .def(
            "disconnect_selection_changed",
            [](const FrameTimelineWidgetRef& self, size_t connection) {
                return self.get().selection_changed().disconnect(connection);
            },
            nb::arg("connection"));

    nb::class_<termin::gui_native::ViewportSurfaceSize>(m, "ViewportSurfaceSize")
        .def(nb::init<>())
        .def(
            "__init__",
            [](termin::gui_native::ViewportSurfaceSize* self, int width, int height) {
                new (self) termin::gui_native::ViewportSurfaceSize{width, height};
            },
            nb::arg("width"),
            nb::arg("height"))
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
               termin::gui_native::ViewportExternalDragPhase phase,
               std::string mime_type,
               std::string payload,
               float x,
               float y) {
                new (self) termin::gui_native::ViewportExternalDragEvent{
                    phase, std::move(mime_type), std::move(payload), x, y};
            },
            nb::arg("phase"),
            nb::arg("mime_type"),
            nb::arg("payload"),
            nb::arg("x"),
            nb::arg("y"))
        .def_rw("phase", &termin::gui_native::ViewportExternalDragEvent::phase)
        .def_rw("mime_type", &termin::gui_native::ViewportExternalDragEvent::mime_type)
        .def_rw("payload", &termin::gui_native::ViewportExternalDragEvent::payload)
        .def_rw("x", &termin::gui_native::ViewportExternalDragEvent::x)
        .def_rw("y", &termin::gui_native::ViewportExternalDragEvent::y);

    nb::class_<Viewport3DRef>(m, "Viewport3D")
        .def_prop_ro("widget", [](const Viewport3DRef& self) { return self.widget; })
        .def_prop_ro("handle", [](const Viewport3DRef& self) { return WidgetHandle{self.widget.handle}; })
        .def_prop_ro("has_surface", [](const Viewport3DRef& self) { return self.get().has_surface(); })
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
                self.get().set_surface_host(
                    std::make_shared<PythonViewportSurfaceHost>(std::move(host), self.widget.state));
                self.widget.throw_pending_exception();
            },
            nb::arg("host"))
        .def("detach_surface", [](const Viewport3DRef& self) { self.get().detach_surface(); })
        .def(
            "connect_before_resize",
            [](const Viewport3DRef& self, nb::object callback) {
                const std::shared_ptr<DocumentState> state = self.widget.state;
                return self.get().before_resize().connect(
                    [state, callback = std::move(callback)](termin::gui_native::Viewport3D&,
                                                            termin::gui_native::ViewportSurfaceSize previous,
                                                            termin::gui_native::ViewportSurfaceSize next) {
                        if (!state || tc_ui_document_handle_is_invalid(state->document))
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
            "disconnect_before_resize",
            [](const Viewport3DRef& self, size_t connection_id) {
                return self.get().before_resize().disconnect(connection_id);
            },
            nb::arg("connection_id"))
        .def(
            "set_external_drag_handler",
            [](const Viewport3DRef& self, nb::object callback) {
                if (callback.is_none()) {
                    self.get().set_external_drag_handler({});
                    return;
                }
                const std::shared_ptr<DocumentState> state = self.widget.state;
                self.get().set_external_drag_handler([state, callback = std::move(callback)](
                                                         const termin::gui_native::ViewportExternalDragEvent& event) {
                    if (!state || tc_ui_document_handle_is_invalid(state->document))
                        return false;
                    nb::gil_scoped_acquire gil;
                    try {
                        return nb::cast<bool>(callback(event));
                    } catch (...) {
                        tc_log_error("[termin-gui-native/python] Viewport3D "
                                     "external drag callback failed");
                        if (!state->pending_exception)
                            state->pending_exception = std::current_exception();
                        return false;
                    }
                });
            },
            nb::arg("callback").none())
        .def(
            "dispatch_external_drag",
            [](const Viewport3DRef& self, const termin::gui_native::ViewportExternalDragEvent& event) {
                const bool handled = self.get().dispatch_external_drag(event);
                self.widget.throw_pending_exception();
                return handled;
            },
            nb::arg("event"));

    nb::class_<SceneViewRef>(m, "SceneView")
        .def_prop_ro("widget", [](const SceneViewRef& self) { return self.widget; })
        .def_prop_ro("handle", [](const SceneViewRef& self) { return WidgetHandle{self.widget.handle}; })
        .def_prop_rw(
            "scene",
            [](const SceneViewRef& self) { return self.get().scene(); },
            [](const SceneViewRef& self, termin::visual::TcVisualScene scene) { self.get().set_scene(scene); })
        .def("invalidate_scene", [](const SceneViewRef& self) { self.get().invalidate_scene(); })
        .def_prop_ro("transform", [](const SceneViewRef& self) { return self.get().transform(); })
        .def_prop_rw(
            "offset",
            [](const SceneViewRef& self) { return self.get().offset(); },
            [](const SceneViewRef& self, tc_ui_point offset) {
                self.get().set_offset(offset);
                self.widget.throw_pending_exception();
            })
        .def_prop_ro("zoom", [](const SceneViewRef& self) { return self.get().zoom(); })
        .def_prop_ro("min_zoom", [](const SceneViewRef& self) { return self.get().min_zoom(); })
        .def_prop_ro("max_zoom", [](const SceneViewRef& self) { return self.get().max_zoom(); })
        .def_prop_rw(
            "zoom_factor",
            [](const SceneViewRef& self) { return self.get().zoom_factor(); },
            [](const SceneViewRef& self, float factor) { self.get().set_zoom_factor(factor); })
        .def_prop_rw(
            "show_grid",
            [](const SceneViewRef& self) { return self.get().show_grid(); },
            [](const SceneViewRef& self, bool show) { self.get().set_show_grid(show); })
        .def_prop_rw(
            "grid_step",
            [](const SceneViewRef& self) { return self.get().grid_step(); },
            [](const SceneViewRef& self, float step) { self.get().set_grid_step(step); })
        .def(
            "set_zoom",
            [](const SceneViewRef& self, float zoom, tc_ui_point anchor) {
                self.get().set_zoom(zoom, anchor);
                self.widget.throw_pending_exception();
            },
            nb::arg("zoom"),
            nb::arg("anchor"))
        .def(
            "set_zoom_range",
            [](const SceneViewRef& self, float minimum, float maximum) {
                self.get().set_zoom_range(minimum, maximum);
                self.widget.throw_pending_exception();
            },
            nb::arg("minimum"),
            nb::arg("maximum"))
        .def(
            "set_scene_colors",
            [](const SceneViewRef& self, tc_ui_srgb_color background, tc_ui_srgb_color grid, tc_ui_srgb_color axes) {
                self.get().set_scene_colors(
                    termin::gui_native::Color{background.r, background.g, background.b, background.a},
                    termin::gui_native::Color{grid.r, grid.g, grid.b, grid.a},
                    termin::gui_native::Color{axes.r, axes.g, axes.b, axes.a});
            },
            nb::arg("background"),
            nb::arg("grid"),
            nb::arg("axes"))
        .def(
            "world_to_screen",
            [](const SceneViewRef& self, tc_ui_point point) { return self.get().world_to_screen(point); },
            nb::arg("point"))
        .def(
            "screen_to_world",
            [](const SceneViewRef& self, tc_ui_point point) { return self.get().screen_to_world(point); },
            nb::arg("point"))
        .def(
            "set_pointer_handler",
            [](const SceneViewRef& self, nb::object callback) {
                if (callback.is_none()) {
                    self.get().set_pointer_handler({});
                    return;
                }
                const auto state = self.widget.state;
                self.get().set_pointer_handler(
                    [state, callback = std::move(callback)](
                        termin::gui_native::SceneView&, tc_ui_point world, const tc_ui_pointer_event& event) {
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
            [](const SceneViewRef& self, nb::object callback) {
                if (callback.is_none()) {
                    self.get().set_key_handler({});
                    return;
                }
                const auto state = self.widget.state;
                self.get().set_key_handler([state, callback = std::move(callback)](termin::gui_native::SceneView&,
                                                                                   const tc_ui_key_event& event) {
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
            [](const SceneViewRef& self, nb::object callback) {
                if (callback.is_none()) {
                    self.get().set_text_handler({});
                    return;
                }
                const auto state = self.widget.state;
                self.get().set_text_handler([state, callback = std::move(callback)](termin::gui_native::SceneView&,
                                                                                    const tc_ui_text_event& event) {
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
            "set_widget_portal",
            [](const SceneViewRef& self, termin::visual::GraphicItemHandle item, WidgetHandle widget) {
                return self.get().set_widget_portal(item, widget.handle);
            },
            nb::arg("item"),
            nb::arg("widget"))
        .def(
            "clear_widget_portal",
            [](const SceneViewRef& self, termin::visual::GraphicItemHandle item) {
                return self.get().clear_widget_portal(item);
            },
            nb::arg("item"))
        .def(
            "connect_transform_changed",
            [](const SceneViewRef& self, nb::object callback) {
                const auto state = self.widget.state;
                return self.get().transform_changed().connect(
                    [state, callback = std::move(callback)](termin::gui_native::SceneView&,
                                                            const termin::gui_native::SceneTransform& transform) {
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
            nb::arg("callback"))
        .def(
            "disconnect_transform_changed",
            [](const SceneViewRef& self, size_t connection) {
                return self.get().transform_changed().disconnect(connection);
            },
            nb::arg("connection"));
}
