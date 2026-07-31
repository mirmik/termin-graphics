#include "gui_native_bindings_shared.hpp"

using namespace termin::gui_native::python_bindings;

namespace {

template <typename Ref, typename... Bases>
void bind_box_layout_api(nb::class_<Ref, Bases...>& cls) {
    cls.def_prop_ro("widget", [](const Ref& self) { return self.widget; })
        .def_prop_ro("handle",
                     [](const Ref& self) { return WidgetHandle{self.widget.handle}; })
        .def(
            "add_child",
            [](const Ref& self, const WidgetRef& child,
               termin::gui_native::LayoutPolicy policy, float value) {
                if (self.widget.state != child.state) {
                    throw std::invalid_argument(
                        "BoxLayout child belongs to another document");
                }
                self.get().add_child(child.handle, policy, value);
                self.widget.throw_pending_exception();
            },
            nb::arg("child"),
            nb::arg("policy") = termin::gui_native::LayoutPolicy::Stretch,
            nb::arg("value") = 0.0f)
        .def("add_fixed_child",
             [](const Ref& self, const WidgetRef& child, float extent) {
                 self.get().add_fixed_child(child.handle, extent);
                 self.widget.throw_pending_exception();
             },
             nb::arg("child"), nb::arg("extent"))
        .def("add_preferred_child",
             [](const Ref& self, const WidgetRef& child) {
                 self.get().add_preferred_child(child.handle);
                 self.widget.throw_pending_exception();
             },
             nb::arg("child"))
        .def("add_flex_child",
             [](const Ref& self, const WidgetRef& child, float flex) {
                 self.get().add_flex_child(child.handle, flex);
                 self.widget.throw_pending_exception();
             },
             nb::arg("child"), nb::arg("flex") = 1.0f)
        .def("add_stretch_child",
             [](const Ref& self, const WidgetRef& child) {
                 self.get().add_stretch_child(child.handle);
                 self.widget.throw_pending_exception();
             },
             nb::arg("child"))
        .def("set_padding",
             [](const Ref& self, termin::gui_native::EdgeInsets padding) {
                 self.get().set_padding(padding);
             },
             nb::arg("padding"))
        .def("set_layout_padding",
             [](const Ref& self, termin::gui_native::EdgeInsets padding) {
                 self.get().set_padding(padding);
             },
             nb::arg("padding"))
        .def("set_spacing",
             [](const Ref& self, float spacing) {
                 self.get().set_spacing(spacing);
             },
             nb::arg("spacing"))
        .def("set_layout_spacing",
             [](const Ref& self, float spacing) {
                 self.get().set_spacing(spacing);
             },
             nb::arg("spacing"))
        .def("set_background",
             [](const Ref& self, tc_ui_color color) {
                 self.get().set_background(
                     {color.r, color.g, color.b, color.a});
             },
             nb::arg("color"))
        .def("set_layout_background",
             [](const Ref& self, tc_ui_color color) {
                 self.get().set_background(
                     {color.r, color.g, color.b, color.a});
             },
             nb::arg("color"))
        .def("set_border",
             [](const Ref& self, tc_ui_color color, float thickness) {
                 self.get().set_border(
                     {color.r, color.g, color.b, color.a}, thickness);
             },
             nb::arg("color"), nb::arg("thickness") = 1.0f)
        .def("set_layout_border",
             [](const Ref& self, tc_ui_color color, float thickness) {
                 self.get().set_border(
                     {color.r, color.g, color.b, color.a}, thickness);
             },
             nb::arg("color"), nb::arg("thickness") = 1.0f)
        .def("set_corner_radius",
             [](const Ref& self, float radius) {
                 self.get().set_corner_radius(radius);
             },
             nb::arg("radius"))
        .def("set_layout_corner_radius",
             [](const Ref& self, float radius) {
                 self.get().set_corner_radius(radius);
             },
             nb::arg("radius"))
        .def("set_child_extent_limits",
             [](const Ref& self, const WidgetRef& child, float minimum,
                float maximum) {
                 return self.get().set_child_extent_limits(
                     child.handle, minimum, maximum);
             },
             nb::arg("child"), nb::arg("minimum"),
             nb::arg("maximum") = 0.0f);
}

} // namespace

void bind_gui_native_widgets(nb::module_& m) {
    nb::enum_<termin::gui_native::ScrollBarPolicy>(m, "ScrollBarPolicy")
        .value("Auto", termin::gui_native::ScrollBarPolicy::Auto)
        .value("Always", termin::gui_native::ScrollBarPolicy::Always)
        .value("Hidden", termin::gui_native::ScrollBarPolicy::Hidden);

    nb::class_<WidgetRef>(m, "WidgetRef")
        .def_prop_ro("handle", [](const WidgetRef& self) { return WidgetHandle{self.handle}; })
        .def_prop_ro("alive", &WidgetRef::alive)
        .def("__bool__", &WidgetRef::alive)
        .def_prop_rw(
            "bounds",
            [](const WidgetRef& self) { return tc_widget_bounds(self.resolve_checked()); },
            [](const WidgetRef& self, tc_ui_rect value) {
                tc_widget_set_bounds(self.resolve_checked(), value);
            })
        .def_prop_rw(
            "min_size",
            [](const WidgetRef& self) { return tc_widget_min_size(self.resolve_checked()); },
            [](const WidgetRef& self, tc_ui_size value) {
                tc_widget_set_min_size(self.resolve_checked(), value);
            })
        .def_prop_rw(
            "preferred_size",
            [](const WidgetRef& self) { return tc_widget_preferred_size(self.resolve_checked()); },
            [](const WidgetRef& self, tc_ui_size value) {
                tc_widget_set_preferred_size(self.resolve_checked(), value);
            })
        .def_prop_rw(
            "max_size",
            [](const WidgetRef& self) { return tc_widget_max_size(self.resolve_checked()); },
            [](const WidgetRef& self, tc_ui_size value) {
                tc_widget_set_max_size(self.resolve_checked(), value);
            })
        .def_prop_rw(
            "layout_spec",
            [](const WidgetRef& self) {
                return tc_widget_layout_spec(self.resolve_checked());
            },
            [](const WidgetRef& self, const tc_ui_widget_layout_spec& value) {
                if (!tc_widget_set_layout_spec(self.resolve_checked(), &value)) {
                    throw std::invalid_argument("invalid native UI widget layout spec");
                }
            })
        .def_prop_rw(
            "visible",
            [](const WidgetRef& self) { return tc_widget_is_visible(self.resolve_checked()); },
            [](const WidgetRef& self, bool value) {
                tc_widget_set_visible(self.resolve_checked(), value);
            })
        .def_prop_rw(
            "enabled",
            [](const WidgetRef& self) { return tc_widget_is_enabled(self.resolve_checked()); },
            [](const WidgetRef& self, bool value) {
                tc_widget_set_enabled(self.resolve_checked(), value);
            })
        .def_prop_rw(
            "mouse_transparent",
            [](const WidgetRef& self) {
                return tc_widget_is_mouse_transparent(self.resolve_checked());
            },
            [](const WidgetRef& self, bool value) {
                tc_widget_set_mouse_transparent(self.resolve_checked(), value);
            })
        .def_prop_rw(
            "focusable",
            [](const WidgetRef& self) { return tc_widget_is_focusable(self.resolve_checked()); },
            [](const WidgetRef& self, bool value) {
                tc_widget_set_focusable(self.resolve_checked(), value);
            })
        .def_prop_rw(
            "cursor_intent",
            [](const WidgetRef& self) {
                return tc_widget_cursor_intent(self.resolve_checked());
            },
            [](const WidgetRef& self, tc_ui_cursor_intent cursor) {
                if (!tc_widget_set_cursor_intent(self.resolve_checked(), cursor)) {
                    throw std::invalid_argument("invalid native UI cursor intent");
                }
                self.throw_pending_exception();
            })
        .def_prop_rw(
            "style_role",
            [](const WidgetRef& self) { return tc_widget_style_role(self.resolve_checked()); },
            [](const WidgetRef& self, tc_ui_style_role role) {
                tc_widget_set_style_role(self.resolve_checked(), role);
            })
        .def_prop_rw(
            "style_override",
            [](const WidgetRef& self) { return tc_widget_style_override(self.resolve_checked()); },
            [](const WidgetRef& self, const tc_ui_style_override& style_override) {
                if (!tc_widget_set_style_override(self.resolve_checked(), &style_override)) {
                    throw std::invalid_argument("invalid native UI style override");
                }
            })
        .def("clear_style_override",
             [](const WidgetRef& self) { tc_widget_clear_style_override(self.resolve_checked()); })
        .def("style_state",
             [](const WidgetRef& self) {
                 return tc_ui_document_widget_style_state(self.state->document,
                                                          self.resolve_checked());
             })
        .def(
            "resolve_style",
            [](const WidgetRef& self, uint32_t extra_state) {
                tc_ui_style style{};
                if (!tc_ui_document_resolve_style(self.state->document, self.resolve_checked(),
                                                  extra_state, &style)) {
                    throw std::runtime_error("failed to resolve native UI widget style");
                }
                return style;
            },
            nb::arg("extra_state") = 0)
        .def_prop_rw(
            "stable_id",
            [](const WidgetRef& self) {
                const char* value = tc_widget_stable_id(self.resolve_checked());
                return value ? std::string(value) : std::string();
            },
            [](const WidgetRef& self, const std::string& value) {
                if (!tc_widget_set_stable_id(self.resolve_checked(), value.c_str()))
                    throw std::runtime_error("failed to set widget stable id");
            })
        .def_prop_rw(
            "name",
            [](const WidgetRef& self) {
                const char* value = tc_widget_name(self.resolve_checked());
                return value ? std::string(value) : std::string();
            },
            [](const WidgetRef& self, const std::string& value) {
                if (!tc_widget_set_name(self.resolve_checked(), value.c_str()))
                    throw std::runtime_error("failed to set widget name");
            })
        .def_prop_rw(
            "debug_name",
            [](const WidgetRef& self) {
                const char* value = tc_widget_debug_name(self.resolve_checked());
                return value ? std::string(value) : std::string();
            },
            [](const WidgetRef& self, const std::string& value) {
                if (!tc_widget_set_debug_name(self.resolve_checked(), value.c_str()))
                    throw std::runtime_error("failed to set widget debug name");
            })
        .def_prop_ro("type_name",
                     [](const WidgetRef& self) {
                         const char* type_name = tc_widget_type_name(self.resolve_checked());
                         return type_name ? std::string(type_name) : std::string();
                     })
        .def_prop_ro("native_language",
                     [](const WidgetRef& self) { return self.resolve_checked()->native_language; })
        .def_prop_ro(
            "ownership",
            [](const WidgetRef& self) { return tc_widget_ownership(self.resolve_checked()); })
        .def_prop_ro(
            "dirty_flags",
            [](const WidgetRef& self) { return tc_widget_dirty_flags(self.resolve_checked()); })
        .def(
            "mark_dirty",
            [](const WidgetRef& self, uint32_t flags) {
                tc_widget_mark_dirty(self.resolve_checked(), flags);
            },
            nb::arg("flags"))
        .def(
            "clear_dirty",
            [](const WidgetRef& self, uint32_t flags) {
                tc_widget_clear_dirty(self.resolve_checked(), flags);
            },
            nb::arg("flags"))
        .def_prop_ro("parent",
                     [](const WidgetRef& self) -> nb::object {
                         tc_widget* parent = tc_widget_parent(self.resolve_checked());
                         return parent ? nb::cast(WidgetRef{self.state, parent->handle})
                                       : nb::none();
                     })
        .def_prop_ro("children",
                     [](const WidgetRef& self) {
                         nb::list children;
                         tc_widget* widget = self.resolve_checked();
                         for (size_t index = 0; index < tc_widget_child_count(widget); ++index) {
                             tc_widget* child = tc_widget_child_at(widget, index);
                             if (child) {
                                 children.append(WidgetRef{self.state, child->handle});
                             }
                         }
                         return children;
                     })
        .def(
            "append_child",
            [](const WidgetRef& self, const WidgetRef& child) {
                return tc_widget_append_child(self.resolve_checked(),
                                              child.resolve_checked());
            },
            nb::arg("child"))
        .def(
            "insert_child",
            [](const WidgetRef& self, size_t index, const WidgetRef& child) {
                return tc_widget_insert_child(self.resolve_checked(), index,
                                              child.resolve_checked());
            },
            nb::arg("index"), nb::arg("child"))
        .def(
            "remove_child",
            [](const WidgetRef& self, const WidgetRef& child) {
                return tc_widget_remove_child(self.resolve_checked(), child.resolve_checked());
            },
            nb::arg("child"))
        .def("detach",
             [](const WidgetRef& self) { return tc_widget_detach(self.resolve_checked()); })
        .def(
            "measure",
            [](const WidgetRef& self, tc_ui_constraints constraints) {
                tc_widget* widget = self.resolve_checked();
                tc_ui_size result = constraints.min_size;
                if (widget->vtable && widget->vtable->measure) {
                    result = widget->vtable->measure(widget, self.state->document, constraints);
                }
                self.throw_pending_exception();
                return result;
            },
            nb::arg("constraints"))
        .def(
            "layout",
            [](const WidgetRef& self, tc_ui_rect rect) {
                tc_widget* widget = self.resolve_checked();
                if (widget->vtable && widget->vtable->layout) {
                    widget->vtable->layout(widget, self.state->document, rect);
                }
                self.throw_pending_exception();
            },
            nb::arg("rect"))
        .def(
            "paint",
            [](const WidgetRef& self, PaintContext& context) {
                tc_widget* widget = self.resolve_checked();
                if (widget->vtable && widget->vtable->paint) {
                    widget->vtable->paint(widget, self.state->document, context.get());
                }
                self.throw_pending_exception();
            },
            nb::arg("context"))
        .def(
            "hit_test",
            [](const WidgetRef& self, float x, float y) {
                tc_widget* widget = self.resolve_checked();
                tc_widget_handle result = tc_widget_handle_invalid();
                if (widget->vtable && widget->vtable->hit_test) {
                    result = widget->vtable->hit_test(widget, self.state->document, x, y);
                }
                self.throw_pending_exception();
                return WidgetHandle{result};
            },
            nb::arg("x"), nb::arg("y"))
        .def(
            "dispatch_pointer_event",
            [](const WidgetRef& self, const tc_ui_pointer_event& event) {
                tc_widget* widget = self.resolve_checked();
                tc_ui_event_result result = TC_UI_EVENT_IGNORED;
                if (widget->vtable && widget->vtable->pointer_event) {
                    result = widget->vtable->pointer_event(widget, self.state->document, &event);
                }
                self.throw_pending_exception();
                return result;
            },
            nb::arg("event"))
        .def(
            "dispatch_key_event",
            [](const WidgetRef& self, const tc_ui_key_event& event) {
                tc_widget* widget = self.resolve_checked();
                tc_ui_event_result result = TC_UI_EVENT_IGNORED;
                if (widget->vtable && widget->vtable->key_event) {
                    result = widget->vtable->key_event(widget, self.state->document, &event);
                }
                self.throw_pending_exception();
                return result;
            },
            nb::arg("event"))
        .def("dispatch_text_event", [](const WidgetRef& self, const std::string& text) {
            tc_widget* widget = self.resolve_checked();
            const tc_ui_text_event event {text.c_str()};
            tc_ui_event_result result = TC_UI_EVENT_IGNORED;
            if (widget->vtable && widget->vtable->text_event) {
                result = widget->vtable->text_event(widget, self.state->document, &event);
            }
            self.throw_pending_exception();
            return result;
        });

    nb::class_<BoxLayoutRef, WidgetRef> box_layout(m, "BoxLayout");
    bind_box_layout_api(box_layout);
    nb::class_<HStackRef, WidgetRef> hstack(m, "HStack");
    bind_box_layout_api(hstack);
    nb::class_<VStackRef, WidgetRef> vstack(m, "VStack");
    bind_box_layout_api(vstack);

    nb::class_<GridLayoutRef, WidgetRef>(m, "GridLayout")
        .def_prop_ro("widget", [](const GridLayoutRef& self) { return self.widget; })
        .def_prop_ro("handle",
                     [](const GridLayoutRef& self) {
                         return WidgetHandle{self.widget.handle};
                     })
        .def("set_padding",
             [](const GridLayoutRef& self,
                termin::gui_native::EdgeInsets padding) {
                 self.get().set_padding(padding);
             },
             nb::arg("padding"))
        .def("set_spacing",
             [](const GridLayoutRef& self, float columns, float rows) {
                 self.get().set_spacing(columns, rows);
             },
             nb::arg("columns"), nb::arg("rows"))
        .def("set_background",
             [](const GridLayoutRef& self, tc_ui_color color) {
                 self.get().set_background(
                     {color.r, color.g, color.b, color.a});
             },
             nb::arg("color"))
        .def("set_border",
             [](const GridLayoutRef& self, tc_ui_color color, float thickness) {
                 self.get().set_border(
                     {color.r, color.g, color.b, color.a}, thickness);
             },
             nb::arg("color"), nb::arg("thickness") = 1.0f)
        .def("add_column",
             [](const GridLayoutRef& self,
                termin::gui_native::LayoutPolicy policy, float value) {
                 return self.get().add_column(policy, value);
             },
             nb::arg("policy") = termin::gui_native::LayoutPolicy::Stretch,
             nb::arg("value") = 0.0f)
        .def("add_row",
             [](const GridLayoutRef& self,
                termin::gui_native::LayoutPolicy policy, float value) {
                 return self.get().add_row(policy, value);
             },
             nb::arg("policy") = termin::gui_native::LayoutPolicy::Stretch,
             nb::arg("value") = 0.0f)
        .def("add_child",
             [](const GridLayoutRef& self, const WidgetRef& child, size_t row,
                size_t column, size_t row_span, size_t column_span) {
                 if (self.widget.state != child.state) {
                     throw std::invalid_argument(
                         "GridLayout child belongs to another document");
                 }
                 self.get().add_child(child.handle, row, column, row_span,
                                      column_span);
                 self.widget.throw_pending_exception();
             },
             nb::arg("child"), nb::arg("row"), nb::arg("column"),
             nb::arg("row_span") = 1, nb::arg("column_span") = 1)
        .def("set_column_extent_limits",
             [](const GridLayoutRef& self, size_t column, float minimum,
                float maximum) {
                 return self.get().set_column_extent_limits(
                     column, minimum, maximum);
             },
             nb::arg("column"), nb::arg("minimum"), nb::arg("maximum") = 0.0f)
        .def("set_row_extent_limits",
             [](const GridLayoutRef& self, size_t row, float minimum,
                float maximum) {
                 return self.get().set_row_extent_limits(
                     row, minimum, maximum);
             },
             nb::arg("row"), nb::arg("minimum"), nb::arg("maximum") = 0.0f);

    nb::class_<PanelRef, WidgetRef>(m, "Panel")
        .def_prop_ro("widget", [](const PanelRef& self) { return self.widget; })
        .def_prop_ro("handle",
                     [](const PanelRef& self) {
                         return WidgetHandle{self.widget.handle};
                     })
        .def("set_fill",
             [](const PanelRef& self, tc_ui_color color) {
                 self.get().set_fill({color.r, color.g, color.b, color.a});
             },
             nb::arg("color"))
        .def("set_border",
             [](const PanelRef& self, tc_ui_color color, float thickness) {
                 self.get().set_border(
                     {color.r, color.g, color.b, color.a}, thickness);
             },
             nb::arg("color"), nb::arg("thickness") = 1.0f);

    nb::class_<LabelRef, WidgetRef>(m, "Label")
        .def_prop_ro("widget", [](const LabelRef& self) { return self.widget; })
        .def_prop_ro("handle",
                     [](const LabelRef& self) {
                         return WidgetHandle{self.widget.handle};
                     })
        .def_prop_rw(
            "text", [](const LabelRef& self) { return self.get().text(); },
            [](const LabelRef& self, const std::string& text) {
                self.get().set_text(text);
            })
        .def("set_color",
             [](const LabelRef& self, tc_ui_color color) {
                 self.get().set_color({color.r, color.g, color.b, color.a});
             },
             nb::arg("color"))
        .def("set_font_size",
             [](const LabelRef& self, float font_size) {
                 self.get().set_font_size(font_size);
             },
             nb::arg("font_size"));

    nb::class_<SliderRef, WidgetRef>(m, "Slider")
        .def_prop_ro("widget", [](const SliderRef& self) { return self.widget; })
        .def_prop_ro("handle",
                     [](const SliderRef& self) {
                         return WidgetHandle{self.widget.handle};
                     })
        .def_prop_rw(
            "value", [](const SliderRef& self) { return self.get().value(); },
            [](const SliderRef& self, float value) {
                self.get().set_value(value);
                self.widget.throw_pending_exception();
            })
        .def_prop_ro("min_value",
                     [](const SliderRef& self) {
                         return self.get().min_value();
                     })
        .def_prop_ro("max_value",
                     [](const SliderRef& self) {
                         return self.get().max_value();
                     })
        .def_prop_rw(
            "step", [](const SliderRef& self) { return self.get().step(); },
            [](const SliderRef& self, float step) {
                self.get().set_step(step);
            })
        .def("set_range",
             [](const SliderRef& self, float minimum, float maximum) {
                 self.get().set_range(minimum, maximum);
                 self.widget.throw_pending_exception();
             },
             nb::arg("minimum"), nb::arg("maximum"))
        .def("connect_changed",
             [](const SliderRef& self, nb::object callback) {
                 auto state = self.widget.state;
                 return self.get().changed().connect(
                     [state, callback = std::move(callback)](
                         termin::gui_native::Slider&, float value) {
                         try {
                             nb::gil_scoped_acquire gil;
                             callback(value);
                         } catch (...) {
                             if (state && !state->pending_exception) {
                                 state->pending_exception =
                                     std::current_exception();
                             }
                             tc_log_error(
                                 "[termin-gui-native/python] Slider changed callback failed");
                         }
                     });
             },
             nb::arg("callback"))
        .def("disconnect_changed",
             [](const SliderRef& self, size_t connection) {
                 return self.get().changed().disconnect(connection);
             },
             nb::arg("connection"));

    nb::class_<SeparatorRef, WidgetRef>(m, "Separator")
        .def_prop_ro("widget",
                     [](const SeparatorRef& self) { return self.widget; })
        .def_prop_ro("handle",
                     [](const SeparatorRef& self) {
                         return WidgetHandle{self.widget.handle};
                     })
        .def("set_color",
             [](const SeparatorRef& self, tc_ui_color color) {
                 self.get().set_color({color.r, color.g, color.b, color.a});
             },
             nb::arg("color"))
        .def("set_thickness",
             [](const SeparatorRef& self, float thickness) {
                 self.get().set_thickness(thickness);
             },
             nb::arg("thickness"));

    nb::class_<SpacerRef, WidgetRef>(m, "Spacer")
        .def_prop_ro("widget", [](const SpacerRef& self) { return self.widget; })
        .def_prop_ro("handle",
                     [](const SpacerRef& self) {
                         return WidgetHandle{self.widget.handle};
                     });

    nb::class_<SwatchRef, WidgetRef>(m, "Swatch")
        .def_prop_ro("widget", [](const SwatchRef& self) { return self.widget; })
        .def_prop_ro("handle",
                     [](const SwatchRef& self) {
                         return WidgetHandle{self.widget.handle};
                     });

    nb::class_<TextInputRef>(m, "TextInput")
        .def_prop_ro("widget", [](const TextInputRef& self) { return self.widget; })
        .def_prop_ro("handle", [](const TextInputRef& self) {
            return WidgetHandle {self.widget.handle};
        })
        .def_prop_rw("text",
            [](const TextInputRef& self) { return self.get().text(); },
            [](const TextInputRef& self, const std::string& value) { self.get().set_text(value); })
        .def_prop_rw("placeholder",
            [](const TextInputRef& self) { return self.get().placeholder(); },
            [](const TextInputRef& self, const std::string& value) {
                self.get().set_placeholder(value);
            })
        .def_prop_rw("caret",
            [](const TextInputRef& self) { return self.get().caret(); },
            [](const TextInputRef& self, size_t value) { self.get().set_caret(value); })
        .def_prop_ro("has_selection", [](const TextInputRef& self) {
            return self.get().has_selection();
        })
        .def_prop_ro("selection_start", [](const TextInputRef& self) {
            return self.get().selection_start();
        })
        .def_prop_ro("selection_end", [](const TextInputRef& self) {
            return self.get().selection_end();
        })
        .def_prop_ro("selected_text", [](const TextInputRef& self) {
            return self.get().selected_text();
        })
        .def_prop_ro("scroll_x", [](const TextInputRef& self) { return self.get().scroll_x(); })
        .def("select", [](const TextInputRef& self, size_t anchor, size_t caret) {
            self.get().select(anchor, caret);
        }, nb::arg("anchor"), nb::arg("caret"))
        .def("select_all", [](const TextInputRef& self) { self.get().select_all(); })
        .def("clear_selection", [](const TextInputRef& self) { self.get().clear_selection(); })
        .def("connect_changed", [](const TextInputRef& self, nb::object callback) {
            auto state = self.widget.state;
            return self.get().changed().connect(
                [state, callback = std::move(callback)](
                    termin::gui_native::TextInput&, const std::string& text) {
                    try {
                        nb::gil_scoped_acquire gil;
                        callback(text);
                    } catch (...) {
                        if (state && !state->pending_exception)
                            state->pending_exception = std::current_exception();
                        tc_log_error(
                            "[termin-gui-native/python] TextInput changed callback failed");
                    }
                });
        }, nb::arg("callback"))
        .def("connect_submitted", [](const TextInputRef& self, nb::object callback) {
            auto state = self.widget.state;
            return self.get().submitted().connect(
                [state, callback = std::move(callback)](
                    termin::gui_native::TextInput&, const std::string& text) {
                    try {
                        nb::gil_scoped_acquire gil;
                        callback(text);
                    } catch (...) {
                        if (state && !state->pending_exception)
                            state->pending_exception = std::current_exception();
                        tc_log_error(
                            "[termin-gui-native/python] TextInput submitted callback failed");
                    }
                });
        }, nb::arg("callback"))
        .def("disconnect_changed",
             [](const TextInputRef& self, size_t connection) {
                 return self.get().changed().disconnect(connection);
             },
             nb::arg("connection"))
        .def("disconnect_submitted",
             [](const TextInputRef& self, size_t connection) {
                 return self.get().submitted().disconnect(connection);
             },
             nb::arg("connection"));

    nb::class_<TextAreaRef>(m, "TextArea")
        .def_prop_ro("widget", [](const TextAreaRef& self) { return self.widget; })
        .def_prop_ro("handle", [](const TextAreaRef& self) {
            return WidgetHandle {self.widget.handle};
        })
        .def_prop_rw("text",
            [](const TextAreaRef& self) { return self.get().text(); },
            [](const TextAreaRef& self, const std::string& value) { self.get().set_text(value); })
        .def_prop_rw("placeholder",
            [](const TextAreaRef& self) { return self.get().placeholder(); },
            [](const TextAreaRef& self, const std::string& value) {
                self.get().set_placeholder(value);
            })
        .def_prop_rw("caret",
            [](const TextAreaRef& self) { return self.get().caret(); },
            [](const TextAreaRef& self, size_t value) { self.get().set_caret(value); })
        .def_prop_ro("has_selection", [](const TextAreaRef& self) {
            return self.get().has_selection();
        })
        .def_prop_ro("selection_start", [](const TextAreaRef& self) {
            return self.get().selection_start();
        })
        .def_prop_ro("selection_end", [](const TextAreaRef& self) {
            return self.get().selection_end();
        })
        .def_prop_ro("selected_text", [](const TextAreaRef& self) {
            return self.get().selected_text();
        })
        .def_prop_ro("scroll_x", [](const TextAreaRef& self) { return self.get().scroll_x(); })
        .def_prop_ro("scroll_y", [](const TextAreaRef& self) { return self.get().scroll_y(); })
        .def("select", [](const TextAreaRef& self, size_t anchor, size_t caret) {
            self.get().select(anchor, caret);
        }, nb::arg("anchor"), nb::arg("caret"))
        .def("select_all", [](const TextAreaRef& self) { self.get().select_all(); })
        .def("clear_selection", [](const TextAreaRef& self) { self.get().clear_selection(); })
        .def("connect_changed", [](const TextAreaRef& self, nb::object callback) {
            auto state = self.widget.state;
            return self.get().changed().connect(
                [state, callback = std::move(callback)](
                    termin::gui_native::TextArea&, const std::string& text) {
                    try {
                        nb::gil_scoped_acquire gil;
                        callback(text);
                    } catch (...) {
                        if (state && !state->pending_exception)
                            state->pending_exception = std::current_exception();
                        tc_log_error(
                            "[termin-gui-native/python] TextArea changed callback failed");
                    }
                });
        }, nb::arg("callback"))
        .def("disconnect_changed",
             [](const TextAreaRef& self, size_t connection) {
                 return self.get().changed().disconnect(connection);
             },
             nb::arg("connection"));

    nb::class_<SpinBoxRef>(m, "SpinBox")
        .def_prop_ro("widget", [](const SpinBoxRef& self) { return self.widget; })
        .def_prop_ro("handle", [](const SpinBoxRef& self) { return WidgetHandle {self.widget.handle}; })
        .def_prop_rw("value",
            [](const SpinBoxRef& self) { return self.get().value(); },
            [](const SpinBoxRef& self, float value) {
                self.get().set_value(value);
                self.widget.throw_pending_exception();
            })
        .def_prop_ro("min_value", [](const SpinBoxRef& self) { return self.get().min_value(); })
        .def_prop_ro("max_value", [](const SpinBoxRef& self) { return self.get().max_value(); })
        .def_prop_rw("step",
            [](const SpinBoxRef& self) { return self.get().step(); },
            [](const SpinBoxRef& self, float value) { self.get().set_step(value); })
        .def_prop_rw("decimals",
            [](const SpinBoxRef& self) { return self.get().decimals(); },
            [](const SpinBoxRef& self, int value) { self.get().set_decimals(value); })
        .def_prop_ro("editing", [](const SpinBoxRef& self) { return self.get().editing(); })
        .def("set_range", [](const SpinBoxRef& self, float minimum, float maximum) {
            self.get().set_range(minimum, maximum);
            self.widget.throw_pending_exception();
        }, nb::arg("minimum"), nb::arg("maximum"))
        .def("connect_changed", [](const SpinBoxRef& self, nb::object callback) {
            auto state = self.widget.state;
            return self.get().changed().connect([state, callback = std::move(callback)](
                                                   termin::gui_native::SpinBox&,
                                                   float value) {
                try {
                    nb::gil_scoped_acquire gil;
                    callback(value);
                } catch (...) {
                    if (state && !state->pending_exception) state->pending_exception = std::current_exception();
                    tc_log_error("[termin-gui-native/python] SpinBox changed callback failed");
                }
            });
        }, nb::arg("callback"))
        .def("disconnect_changed",
             [](const SpinBoxRef& self, size_t connection) {
                 return self.get().changed().disconnect(connection);
             },
             nb::arg("connection"));

    nb::class_<SliderEditRef>(m, "SliderEdit")
        .def_prop_ro("widget", [](const SliderEditRef& self) { return self.widget; })
        .def_prop_ro("handle",
                     [](const SliderEditRef& self) { return WidgetHandle{self.widget.handle}; })
        .def_prop_rw(
            "value", [](const SliderEditRef& self) { return self.get().value(); },
            [](const SliderEditRef& self, float value) {
                self.get().set_value(value);
                self.widget.throw_pending_exception();
            })
        .def_prop_rw(
            "label", [](const SliderEditRef& self) { return self.get().label(); },
            [](const SliderEditRef& self, const std::string& value) {
                self.get().set_label(value);
            })
        .def_prop_ro(
            "slider_handle",
            [](const SliderEditRef& self) { return WidgetHandle{self.get().slider_handle()}; })
        .def_prop_ro(
            "spin_box_handle",
            [](const SliderEditRef& self) { return WidgetHandle{self.get().spin_box_handle()}; })
        .def(
            "set_range",
            [](const SliderEditRef& self, float minimum, float maximum) {
                self.get().set_range(minimum, maximum);
                self.widget.throw_pending_exception();
            },
            nb::arg("minimum"), nb::arg("maximum"))
        .def("set_step", [](const SliderEditRef& self, float step) { self.get().set_step(step); })
        .def("set_decimals",
             [](const SliderEditRef& self, int decimals) { self.get().set_decimals(decimals); })
        .def(
            "connect_changed",
            [](const SliderEditRef& self, nb::object callback) {
                auto state = self.widget.state;
                return self.get().changed().connect(
                    [state, callback = std::move(callback)](termin::gui_native::SliderEdit&,
                                                            float value) {
                        try {
                            nb::gil_scoped_acquire gil;
                            callback(value);
                        } catch (...) {
                            if (state && !state->pending_exception)
                                state->pending_exception = std::current_exception();
                            tc_log_error(
                                "[termin-gui-native/python] SliderEdit changed callback failed");
                        }
                    });
            },
            nb::arg("callback"))
        .def("disconnect_changed",
             [](const SliderEditRef& self, size_t connection) {
                 return self.get().changed().disconnect(connection);
             },
             nb::arg("connection"));

    nb::class_<ButtonRef>(m, "Button")
        .def_prop_ro("widget", [](const ButtonRef& self) { return self.widget; })
        .def_prop_ro("handle",
                     [](const ButtonRef& self) { return WidgetHandle{self.widget.handle}; })
        .def(
            "set_text",
            [](const ButtonRef& self, const std::string& text) { self.get().set_text(text); },
            nb::arg("text"))
        .def(
            "connect_clicked",
            [](const ButtonRef& self, nb::object callback) {
                auto state = self.widget.state;
                return self.get().clicked().connect(
                    [state, callback = std::move(callback)](termin::gui_native::Button&) {
                        try {
                            nb::gil_scoped_acquire gil;
                            callback();
                        } catch (...) {
                            if (state && !state->pending_exception) {
                                state->pending_exception = std::current_exception();
                            }
                            tc_log_error("[termin-gui-native/python] Button click callback failed");
                        }
                    });
            },
            nb::arg("callback"))
        .def("disconnect_clicked",
             [](const ButtonRef& self, size_t connection) {
                 return self.get().clicked().disconnect(connection);
             },
             nb::arg("connection"));

    nb::class_<CheckboxRef>(m, "Checkbox")
        .def_prop_ro("widget", [](const CheckboxRef& self) { return self.widget; })
        .def_prop_ro("handle",
                     [](const CheckboxRef& self) { return WidgetHandle{self.widget.handle}; })
        .def_prop_rw(
            "checked", [](const CheckboxRef& self) { return self.get().checked(); },
            [](const CheckboxRef& self, bool checked) {
                self.get().set_checked(checked);
                self.widget.throw_pending_exception();
            })
        .def(
            "connect_changed",
            [](const CheckboxRef& self, nb::object callback) {
                auto state = self.widget.state;
                return self.get().changed().connect([state, callback = std::move(callback)](
                                                        termin::gui_native::Checkbox&,
                                                        bool checked) {
                    try {
                        nb::gil_scoped_acquire gil;
                        callback(checked);
                    } catch (...) {
                        if (state && !state->pending_exception) {
                            state->pending_exception = std::current_exception();
                        }
                        tc_log_error("[termin-gui-native/python] Checkbox changed callback failed");
                    }
                });
            },
            nb::arg("callback"))
        .def("disconnect_changed",
             [](const CheckboxRef& self, size_t connection) {
                 return self.get().changed().disconnect(connection);
             },
             nb::arg("connection"));

    nb::class_<GroupBoxRef>(m, "GroupBox")
        .def_prop_ro("widget", [](const GroupBoxRef& self) { return self.widget; })
        .def_prop_ro("handle", [](const GroupBoxRef& self) {
            return WidgetHandle{self.widget.handle};
        })
        .def_prop_rw(
            "title", [](const GroupBoxRef& self) { return self.get().title(); },
            [](const GroupBoxRef& self, const std::string& title) {
                self.get().set_title(title);
            })
        .def_prop_ro("content_handle", [](const GroupBoxRef& self) {
            return WidgetHandle{self.get().content()};
        })
        .def(
            "set_content",
            [](const GroupBoxRef& self, const WidgetRef& content) {
                if (self.widget.state != content.state) {
                    throw std::invalid_argument(
                        "GroupBox content belongs to another document");
                }
                self.get().set_content(content.handle);
                self.widget.throw_pending_exception();
            },
            nb::arg("content"))
        .def(
            "set_padding",
            [](const GroupBoxRef& self, termin::gui_native::EdgeInsets padding) {
                self.get().set_padding(padding);
            },
            nb::arg("padding"))
        .def(
            "set_background",
            [](const GroupBoxRef& self, tc_ui_color color) {
                self.get().set_background({color.r, color.g, color.b, color.a});
            },
            nb::arg("color"))
        .def(
            "set_border",
            [](const GroupBoxRef& self, tc_ui_color color, float thickness) {
                self.get().set_border(
                    {color.r, color.g, color.b, color.a}, thickness);
            },
            nb::arg("color"), nb::arg("thickness") = 1.0f);

    nb::class_<ScrollAreaRef>(m, "ScrollArea")
        .def_prop_ro("widget",
                     [](const ScrollAreaRef &self) { return self.widget; })
        .def_prop_ro("handle",
                     [](const ScrollAreaRef &self) {
                       return WidgetHandle{self.widget.handle};
                     })
        .def_prop_ro("content_handle",
                     [](const ScrollAreaRef &self) {
                       return WidgetHandle{self.get().content()};
                     })
        .def_prop_rw(
            "scroll_x",
            [](const ScrollAreaRef &self) { return self.get().scroll_x(); },
            [](const ScrollAreaRef &self, float value) {
              self.get().set_scroll(value, self.get().scroll_y());
            })
        .def_prop_rw(
            "scroll_y",
            [](const ScrollAreaRef &self) { return self.get().scroll_y(); },
            [](const ScrollAreaRef &self, float value) {
              self.get().set_scroll(self.get().scroll_x(), value);
            })
        .def_prop_ro(
            "content_size",
            [](const ScrollAreaRef &self) { return self.get().content_size(); })
        .def_prop_ro(
            "horizontal_scroll_enabled",
            [](const ScrollAreaRef &self) {
              return self.get().horizontal_scroll_enabled();
            })
        .def_prop_ro(
            "vertical_scroll_enabled",
            [](const ScrollAreaRef &self) {
              return self.get().vertical_scroll_enabled();
            })
        .def_prop_ro(
            "horizontal_scrollbar_policy",
            [](const ScrollAreaRef &self) {
              return self.get().horizontal_scrollbar_policy();
            })
        .def_prop_ro(
            "vertical_scrollbar_policy",
            [](const ScrollAreaRef &self) {
              return self.get().vertical_scrollbar_policy();
            })
        .def_prop_ro(
            "horizontal_scrollbar_visible",
            [](const ScrollAreaRef &self) {
              return self.get().horizontal_scrollbar_visible();
            })
        .def_prop_ro(
            "vertical_scrollbar_visible",
            [](const ScrollAreaRef &self) {
              return self.get().vertical_scrollbar_visible();
            })
        .def(
            "set_scroll_axes",
            [](const ScrollAreaRef &self, bool horizontal, bool vertical) {
              self.get().set_scroll_axes(horizontal, vertical);
              self.widget.throw_pending_exception();
            },
            nb::arg("horizontal"), nb::arg("vertical"))
        .def(
            "set_scrollbar_policy",
            [](const ScrollAreaRef &self,
               termin::gui_native::ScrollBarPolicy horizontal,
               termin::gui_native::ScrollBarPolicy vertical) {
              self.get().set_scrollbar_policy(horizontal, vertical);
            },
            nb::arg("horizontal"), nb::arg("vertical"))
        .def(
            "ensure_visible",
            [](const ScrollAreaRef &self, const WidgetRef &descendant) {
              if (self.widget.state != descendant.state) {
                throw std::invalid_argument(
                    "ScrollArea descendant belongs to another document");
              }
              const bool changed = self.get().ensure_visible(descendant.handle);
              self.widget.throw_pending_exception();
              return changed;
            },
            nb::arg("descendant"))
        .def(
            "connect_changed",
            [](const ScrollAreaRef &self, nb::object callback) {
              auto state = self.widget.state;
              return self.get().changed().connect(
                  [state, callback = std::move(callback)](
                      termin::gui_native::ScrollArea&, float x, float y) {
                    try {
                      nb::gil_scoped_acquire gil;
                      callback(x, y);
                    } catch (...) {
                      if (state && !state->pending_exception) {
                        state->pending_exception = std::current_exception();
                      }
                      tc_log_error(
                          "[termin-gui-native/python] ScrollArea changed callback failed");
                    }
                  });
            },
            nb::arg("callback"))
        .def(
            "disconnect_changed",
            [](const ScrollAreaRef &self, size_t connection) {
              return self.get().changed().disconnect(connection);
            },
            nb::arg("connection"))
        .def(
            "set_content",
            [](const ScrollAreaRef &self, const WidgetRef &content) {
              if (self.widget.state != content.state) {
                throw std::invalid_argument(
                    "ScrollArea content belongs to another document");
              }
              self.get().set_content(content.handle);
              self.widget.throw_pending_exception();
            },
            nb::arg("content"));

    nb::class_<SplitterRef>(m, "Splitter")
        .def_prop_ro("widget", [](const SplitterRef& self) { return self.widget; })
        .def_prop_ro("handle", [](const SplitterRef& self) {
            return WidgetHandle{self.widget.handle};
        })
        .def_prop_rw(
            "split_fraction",
            [](const SplitterRef& self) { return self.get().split_fraction(); },
            [](const SplitterRef& self, float value) {
                self.get().set_split_fraction(value);
            })
        .def_prop_ro(
            "divider_thickness",
            [](const SplitterRef& self) { return self.get().divider_thickness(); })
        .def("set_split_fraction", [](const SplitterRef& self, float value) {
            self.get().set_split_fraction(value);
        }, nb::arg("value"))
        .def("set_first", [](const SplitterRef& self, const WidgetRef& widget) {
            if (self.widget.state != widget.state) {
                throw std::invalid_argument("Splitter first widget belongs to another document");
            }
            self.get().set_first(widget.handle);
            self.widget.throw_pending_exception();
        }, nb::arg("widget"))
        .def("set_second", [](const SplitterRef& self, const WidgetRef& widget) {
            if (self.widget.state != widget.state) {
                throw std::invalid_argument("Splitter second widget belongs to another document");
            }
            self.get().set_second(widget.handle);
            self.widget.throw_pending_exception();
        }, nb::arg("widget"))
        .def("set_min_extents", [](const SplitterRef& self, float first, float second) {
            self.get().set_min_extents(first, second);
        }, nb::arg("first"), nb::arg("second"))
        .def("set_divider_thickness", [](const SplitterRef& self, float thickness) {
            self.get().set_divider_thickness(thickness);
        }, nb::arg("thickness"));

    nb::enum_<termin::gui_native::CommandKind>(m, "CommandKind")
        .value("Action", termin::gui_native::CommandKind::Action)
        .value("Separator", termin::gui_native::CommandKind::Separator);

}
