#include <cstdint>
#include <cmath>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

#include <nanobind/nanobind.h>
#include <nanobind/stl/string.h>

#include <termin/geom/color.hpp>

#include "termin_visual_scene/builtin_items2d.hpp"
#include "termin_visual_scene/interaction2d.hpp"

namespace nb = nanobind;

namespace {

    using termin::visual::GraphicItem2D;
    using termin::visual::GraphicItemHandle;
    using termin::visual::PolylineItem2D;
    using termin::visual::TcVisualScene;

    [[noreturn]] void stale_reference(const char* message) {
        PyErr_SetString(PyExc_ReferenceError, message);
        throw nb::python_error();
    }

    bool same_handle(GraphicItemHandle left, GraphicItemHandle right) {
        return left.scene_id == right.scene_id && left.index == right.index && left.generation == right.generation;
    }

    struct GraphicItemRef2D {
        TcVisualScene scene;
        GraphicItemHandle handle = tc_graphic_item_handle_invalid();

        TcVisualScene owner() const {
            if (!scene.valid()) {
                stale_reference("TcVisualScene has been destroyed");
            }
            return scene;
        }

        tc_graphic_item& item() const {
            auto value = owner();
            tc_graphic_item* item = value.resolve(handle);
            if (!item) {
                stale_reference("GraphicItemRef2D is stale");
            }
            return *item;
        }

        GraphicItem2D& object() const {
            tc_graphic_item& value = item();
            return *static_cast<GraphicItem2D*>(value.body);
        }

        bool valid() const {
            return scene.valid() && scene.contains(handle);
        }
    };

    struct PolylineItemRef2D : GraphicItemRef2D {};

    termin::SrgbColor validate_color(termin::SrgbColor authored) {
        if (!std::isfinite(authored.r) || !std::isfinite(authored.g) || !std::isfinite(authored.b) ||
            !std::isfinite(authored.a)) {
            throw nb::value_error("color must be finite");
        }
        return authored;
    }

    termin::LinearColor parse_color(termin::SrgbColor authored) {
        return termin::srgb_to_linear(validate_color(authored));
    }

    termin::Vec2f parse_point(nb::tuple value) {
        if (value.size() != 2) {
            throw nb::value_error("point must contain (x, y)");
        }
        return {
            nb::cast<float>(value[0]),
            nb::cast<float>(value[1]),
        };
    }

    termin::Rect2f parse_rect(nb::tuple value) {
        if (value.size() != 4) {
            throw nb::value_error("rect must contain (x, y, width, height)");
        }
        return {
            nb::cast<float>(value[0]),
            nb::cast<float>(value[1]),
            nb::cast<float>(value[2]),
            nb::cast<float>(value[3]),
        };
    }

    std::vector<termin::Vec2f> parse_points(nb::sequence values) {
        std::vector<termin::Vec2f> result;
        result.reserve(nb::len(values));
        for (nb::handle value : values) {
            result.push_back(parse_point(nb::cast<nb::tuple>(value)));
        }
        return result;
    }

    GraphicItem2D* parent_object(TcVisualScene scene, nb::object parent) {
        if (parent.is_none())
            return nullptr;
        const auto& ref = nb::cast<const GraphicItemRef2D&>(parent);
        auto parent_scene = ref.owner();
        if (parent_scene.handle().index != scene.handle().index ||
            parent_scene.handle().generation != scene.handle().generation) {
            throw nb::value_error("parent belongs to another TcVisualScene");
        }
        return &ref.object();
    }

    GraphicItemRef2D wrap(TcVisualScene scene, std::optional<GraphicItemHandle> handle) {
        if (!handle) {
            throw nb::value_error("graphic item was rejected");
        }
        return {scene, *handle};
    }

    nb::tuple affine_tuple(const termin::Affine2f& value) {
        return nb::make_tuple(value.m00, value.m01, value.m10, value.m11, value.tx, value.ty);
    }

    nb::object bounds_value(const std::optional<termin::Bounds2f>& value) {
        if (!value)
            return nb::none();
        return nb::make_tuple(value->x0, value->y0, value->x1, value->y1);
    }

    std::optional<tgfx::StrokePaint> stroke(nb::object color, float width) {
        if (color.is_none())
            return std::nullopt;
        return tgfx::StrokePaint{
            parse_color(nb::cast<termin::SrgbColor>(color)),
            width,
        };
    }

} // namespace

NB_MODULE(_visual_scene_native, m) {
    m.doc() = "Direct retained 2D visual scene bindings";

    nb::class_<GraphicItemHandle>(m, "GraphicItemHandle")
        .def_prop_ro("scene_id", [](GraphicItemHandle value) { return value.scene_id; })
        .def_prop_ro("index", [](GraphicItemHandle value) { return value.index; })
        .def_prop_ro("generation", [](GraphicItemHandle value) { return value.generation; })
        .def("__eq__", &same_handle);

    nb::class_<GraphicItemRef2D>(m, "GraphicItemRef2D")
        .def_prop_ro("valid", &GraphicItemRef2D::valid)
        .def_prop_ro("handle", [](const GraphicItemRef2D& self) { return self.handle; })
        .def_prop_ro("type_name",
                     [](const GraphicItemRef2D& self) {
                         const char* value = tc_graphic_item_type_name(&self.item());
                         return value ? value : "";
                     })
        .def_prop_ro("parent",
                     [](const GraphicItemRef2D& self) -> nb::object {
                         tc_graphic_item* parent = self.item().parent;
                         if (!parent)
                             return nb::none();
                         return nb::cast(GraphicItemRef2D{self.scene, parent->handle});
                     })
        .def_prop_ro("children",
                     [](const GraphicItemRef2D& self) {
                         nb::list result;
                         const tc_graphic_item& item = self.item();
                         for (std::size_t index = 0; index < item.child_count; ++index) {
                             result.append(GraphicItemRef2D{
                                 self.scene,
                                 item.children[index]->handle,
                             });
                         }
                         return result;
                     })
        .def_prop_rw(
            "position",
            [](const GraphicItemRef2D& self) {
                const auto& transform = self.object().local_transform();
                return nb::make_tuple(transform.tx, transform.ty);
            },
            [](const GraphicItemRef2D& self, nb::tuple value) {
                const auto point = parse_point(value);
                auto transform = self.object().local_transform();
                transform.tx = point.x;
                transform.ty = point.y;
                self.object().set_local_transform(transform);
            })
        .def_prop_ro("local_transform",
                     [](const GraphicItemRef2D& self) { return affine_tuple(self.object().local_transform()); })
        .def_prop_ro(
            "world_transform",
            [](const GraphicItemRef2D& self) { return affine_tuple(self.owner().world_transform(self.item())); })
        .def_prop_rw(
            "visible",
            [](const GraphicItemRef2D& self) { return self.object().visible(); },
            [](const GraphicItemRef2D& self, bool value) { self.object().set_visible(value); })
        .def_prop_rw(
            "enabled",
            [](const GraphicItemRef2D& self) { return self.object().enabled(); },
            [](const GraphicItemRef2D& self, bool value) { self.object().set_enabled(value); })
        .def_prop_rw(
            "opacity",
            [](const GraphicItemRef2D& self) { return self.object().opacity(); },
            [](const GraphicItemRef2D& self, float value) { self.object().set_opacity(value); })
        .def_prop_rw(
            "z_order",
            [](const GraphicItemRef2D& self) { return self.object().z_order(); },
            [](const GraphicItemRef2D& self, std::int64_t value) { self.object().set_z_order(value); })
        .def_prop_ro("effective_visible",
                     [](const GraphicItemRef2D& self) { return self.owner().effective_visible(self.item()); })
        .def_prop_ro("local_bounds",
                     [](const GraphicItemRef2D& self) { return bounds_value(self.owner().local_bounds(self.item())); })
        .def_prop_ro("world_bounds",
                     [](const GraphicItemRef2D& self) { return bounds_value(self.owner().world_bounds(self.item())); })
        .def("set_transform",
             [](const GraphicItemRef2D& self, float m00, float m01, float m10, float m11, float tx, float ty) {
                 self.object().set_local_transform({m00, m01, m10, m11, tx, ty});
             })
        .def(
            "reparent",
            [](const GraphicItemRef2D& self, nb::object parent) {
                if (parent.is_none()) {
                    return self.object().detach();
                }
                const auto& parent_ref = nb::cast<const GraphicItemRef2D&>(parent);
                const auto parent_scene = parent_ref.owner().handle();
                const auto own_scene = self.owner().handle();
                if (parent_scene.index != own_scene.index || parent_scene.generation != own_scene.generation) {
                    throw nb::value_error("parent belongs to another TcVisualScene");
                }
                return parent_ref.object().append_child(self.object());
            },
            nb::arg("parent").none() = nb::none())
        .def("destroy", [](const GraphicItemRef2D& self) { return self.owner().destroy(self.handle); })
        .def("__bool__", &GraphicItemRef2D::valid)
        .def("__eq__", [](const GraphicItemRef2D& self, const GraphicItemRef2D& other) {
            const auto left = self.scene.handle();
            const auto right = other.scene.handle();
            return left.index == right.index && left.generation == right.generation &&
                   same_handle(self.handle, other.handle);
        });

    nb::class_<PolylineItemRef2D, GraphicItemRef2D>(m, "PolylineItemRef2D")
        .def(
            "set",
            [](const PolylineItemRef2D& self, nb::sequence points, termin::SrgbColor color, float width, bool closed) {
                tc_graphic_item& item = self.item();
                const char* type_name = tc_graphic_item_type_name(&item);
                if (!type_name || std::string_view(type_name) != "termin.visual.Polyline2D") {
                    throw nb::type_error("item is no longer a Polyline2D");
                }
                static_cast<PolylineItem2D*>(item.body)->set(
                    parse_points(points), tgfx::StrokePaint{parse_color(color), width}, closed);
            },
            nb::arg("points"),
            nb::arg("color"),
            nb::arg("width") = 1.0f,
            nb::arg("closed") = false);

    m.def("tc_visual_scene_create", [] {
        const auto handle = tc_visual_scene_create();
        if (tc_visual_scene_handle_is_invalid(handle)) {
            throw std::runtime_error("failed to create visual scene");
        }
        return TcVisualScene{handle};
    });
    m.def("tc_visual_scene_destroy", [](TcVisualScene& scene) {
        tc_visual_scene_destroy(scene.handle());
        scene = TcVisualScene{};
    });

    nb::class_<TcVisualScene>(m, "TcVisualScene")
        .def_prop_ro("valid", &TcVisualScene::valid)
        .def_prop_ro("size", &TcVisualScene::size)
        .def_prop_ro("items",
                     [](TcVisualScene self) {
                         nb::list result;
                         for (auto* item : self.items()) {
                             result.append(GraphicItemRef2D{self, item->handle});
                         }
                         return result;
                     })
        .def(
            "create_group",
            [](TcVisualScene self, nb::object parent) {
                return wrap(self,
                            self.adopt(std::make_unique<termin::visual::GroupItem2D>(), parent_object(self, parent)));
            },
            nb::arg("parent").none() = nb::none())
        .def(
            "create_rect",
            [](TcVisualScene self,
               nb::tuple rect,
               termin::SrgbColor fill,
               nb::object stroke_color,
               float stroke_width,
               nb::object parent) {
                return wrap(self,
                            self.adopt(std::make_unique<termin::visual::RectItem2D>(parse_rect(rect),
                                                                                    tgfx::FillPaint{parse_color(fill)},
                                                                                    stroke(stroke_color, stroke_width)),
                                       parent_object(self, parent)));
            },
            nb::arg("rect"),
            nb::arg("fill"),
            nb::arg("stroke") = nb::none(),
            nb::arg("stroke_width") = 1.0f,
            nb::arg("parent").none() = nb::none())
        .def(
            "create_rounded_rect",
            [](TcVisualScene self,
               nb::tuple rect,
               float radius,
               termin::SrgbColor fill,
               nb::object stroke_color,
               float stroke_width,
               nb::object parent) {
                return wrap(
                    self,
                    self.adopt(std::make_unique<termin::visual::RoundedRectItem2D>(parse_rect(rect),
                                                                                   radius,
                                                                                   tgfx::FillPaint{parse_color(fill)},
                                                                                   stroke(stroke_color, stroke_width)),
                               parent_object(self, parent)));
            },
            nb::arg("rect"),
            nb::arg("radius"),
            nb::arg("fill"),
            nb::arg("stroke") = nb::none(),
            nb::arg("stroke_width") = 1.0f,
            nb::arg("parent").none() = nb::none())
        .def(
            "create_ellipse",
            [](TcVisualScene self,
               nb::tuple rect,
               termin::SrgbColor fill,
               nb::object stroke_color,
               float stroke_width,
               nb::object parent) {
                return wrap(
                    self,
                    self.adopt(std::make_unique<termin::visual::EllipseItem2D>(parse_rect(rect),
                                                                               tgfx::FillPaint{parse_color(fill)},
                                                                               stroke(stroke_color, stroke_width)),
                               parent_object(self, parent)));
            },
            nb::arg("bounds"),
            nb::arg("fill"),
            nb::arg("stroke") = nb::none(),
            nb::arg("stroke_width") = 1.0f,
            nb::arg("parent").none() = nb::none())
        .def(
            "create_polyline",
            [](TcVisualScene self, nb::sequence points, termin::SrgbColor color, float width, bool closed, nb::object parent) {
                auto object = std::make_unique<PolylineItem2D>(
                    parse_points(points), tgfx::StrokePaint{parse_color(color), width}, closed);
                const auto handle = self.adopt(std::move(object), parent_object(self, parent));
                if (!handle) {
                    throw nb::value_error("graphic item was rejected");
                }
                PolylineItemRef2D result;
                result.scene = self;
                result.handle = *handle;
                return result;
            },
            nb::arg("points"),
            nb::arg("color"),
            nb::arg("width") = 1.0f,
            nb::arg("closed") = false,
            nb::arg("parent").none() = nb::none())
        .def(
            "create_text",
            [](TcVisualScene self,
               std::string text,
               nb::tuple origin,
               float size_px,
               termin::SrgbColor color,
               nb::tuple layout_bounds,
               nb::object parent) {
                const auto bounds = parse_rect(layout_bounds);
                return wrap(self,
                            self.adopt(std::make_unique<termin::visual::TextItem2D>(
                                           std::move(text),
                                           "ui://default-font",
                                           parse_point(origin),
                                           size_px,
                                           validate_color(color),
                                           tgfx::TextAnchor2D::Left,
                                           termin::Bounds2f{
                                               bounds.x, bounds.y, bounds.x + bounds.width, bounds.y + bounds.height}),
                                       parent_object(self, parent)));
            },
            nb::arg("text"),
            nb::arg("origin"),
            nb::arg("size_px"),
            nb::arg("color"),
            nb::arg("layout_bounds"),
            nb::arg("parent").none() = nb::none())
        .def("clear", &TcVisualScene::clear)
        .def(
            "destroy",
            [](TcVisualScene self, const GraphicItemRef2D& item) {
                const auto owner = item.owner().handle();
                const auto current = self.handle();
                if (owner.index != current.index || owner.generation != current.generation) {
                    return false;
                }
                return self.destroy(item.handle);
            },
            nb::arg("item"))
        .def(
            "hit_test",
            [](TcVisualScene self, float x, float y) -> nb::object {
                const auto hit = termin::visual::hit_test(self, {x, y});
                return hit ? nb::cast(GraphicItemRef2D{self, *hit}) : nb::none();
            },
            nb::arg("x"),
            nb::arg("y"));
}
