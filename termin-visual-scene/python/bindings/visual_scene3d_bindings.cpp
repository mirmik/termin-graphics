#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

#include <nanobind/nanobind.h>
#include <nanobind/stl/string.h>

#include <termin/geom/color.hpp>
#include <tgfx/tgfx_mesh3.hpp>

#include "termin_visual_scene/interaction3d.hpp"
#include "termin_visual_scene/items/group_item3d.hpp"
#include "termin_visual_scene/items/point_cloud_item3d.hpp"
#include "termin_visual_scene/items/primitive_item3d.hpp"
#include "termin_visual_scene/items/static_mesh_item3d.hpp"

namespace nb = nanobind;

namespace termin::visual::python {
    namespace {

        [[noreturn]] void stale_reference(const char* message) {
            PyErr_SetString(PyExc_ReferenceError, message);
            throw nb::python_error();
        }

        bool same_handle(VisualItem3DHandle left, VisualItem3DHandle right) {
            return tc_visual_item3d_handle_eq(left, right);
        }

        bool same_scene(TcVisualScene3D left, TcVisualScene3D right) {
            const auto left_handle = left.handle();
            const auto right_handle = right.handle();
            return left_handle.index == right_handle.index && left_handle.generation == right_handle.generation;
        }

        struct VisualItemRef3D {
            TcVisualScene3D scene;
            VisualItem3DHandle handle = tc_visual_item3d_handle_invalid();

            TcVisualScene3D owner() const {
                if (!scene.valid()) {
                    stale_reference("TcVisualScene3D has been destroyed");
                }
                return scene;
            }

            tc_visual_item3d& item() const {
                auto value = owner();
                tc_visual_item3d* resolved = value.resolve(handle);
                if (!resolved) {
                    stale_reference("VisualItemRef3D is stale");
                }
                return *resolved;
            }

            bool valid() const {
                return scene.valid() && scene.contains(handle);
            }
        };

        struct PrimitiveItemRef3D : VisualItemRef3D {};
        struct StaticMeshItemRef3D : VisualItemRef3D {};
        struct PointCloudItemRef3D : VisualItemRef3D {};

        struct HitResultRef3D {
            TcVisualScene3D scene;
            HitResult3D hit{};
        };

        struct ActionEventRef3D {
            TcVisualScene3D scene;
            ActionEvent3D event{};
        };

        struct PointerDispatchRef3D {
            TcVisualScene3D scene;
            PointerDispatch3D dispatch{};
        };

        struct PythonSceneInteraction3D {
            SceneInteraction3D interaction;
        };

        template <typename Ref> Ref typed_ref(TcVisualScene3D scene, VisualItem3DHandle handle) {
            Ref result;
            result.scene = scene;
            result.handle = handle;
            return result;
        }

        nb::object wrap_item(TcVisualScene3D scene, VisualItem3DHandle handle) {
            if (tc_visual_item3d_handle_is_invalid(handle) || !scene.contains(handle)) {
                return nb::none();
            }
            const char* type_name = tc_visual_item3d_type_name_in_scene(scene.handle(), handle);
            const std::string_view type = type_name ? type_name : "";
            if (type == "termin.visual.Primitive3D") {
                return nb::cast(typed_ref<PrimitiveItemRef3D>(scene, handle));
            }
            if (type == "termin.visual.StaticMesh3D") {
                return nb::cast(typed_ref<StaticMeshItemRef3D>(scene, handle));
            }
            if (type == "termin.visual.PointCloud3D") {
                return nb::cast(typed_ref<PointCloudItemRef3D>(scene, handle));
            }
            return nb::cast(VisualItemRef3D{scene, handle});
        }

        template <typename Item, typename Ref> Item& require_item(const Ref& ref, std::string_view expected_type) {
            tc_visual_item3d& value = ref.item();
            const char* type_name = tc_visual_item3d_type_name(&value);
            if (value.native_language != TC_LANGUAGE_CXX || !type_name ||
                std::string_view(type_name) != expected_type) {
                throw nb::type_error("visual item concrete type no longer matches its typed reference");
            }
            return *static_cast<Item*>(value.body);
        }

        tc_visual_item3d* parent_item(TcVisualScene3D scene, nb::object parent) {
            if (parent.is_none()) {
                return nullptr;
            }
            const auto& ref = nb::cast<const VisualItemRef3D&>(parent);
            if (!same_scene(scene, ref.owner())) {
                throw nb::value_error("parent belongs to another TcVisualScene3D");
            }
            return &ref.item();
        }

        VisualItem3DHandle require_adopted(std::optional<VisualItem3DHandle> handle) {
            if (!handle) {
                throw nb::value_error("visual item was rejected");
            }
            return *handle;
        }

        VisualItem3DHandle
        adopt_owned(TcVisualScene3D scene, std::unique_ptr<VisualItem3D> item, tc_visual_item3d* parent) {
            auto* owned = item.release();
            return require_adopted(scene.adopt(owned->c_item(), &VisualItem3D::delete_owned_item, parent));
        }

        termin::SrgbColor require_srgb(termin::SrgbColor value, const char* label) {
            if (!std::isfinite(value.r) || !std::isfinite(value.g) || !std::isfinite(value.b) ||
                !std::isfinite(value.a)) {
                throw nb::value_error((std::string(label) + " must be finite").c_str());
            }
            return value;
        }

        termin::LinearColor linear_color(termin::SrgbColor value, const char* label) {
            return termin::srgb_to_linear(require_srgb(value, label));
        }

        termin::Vec3f vec3f(nb::handle value, const char* label) {
            nb::sequence sequence = nb::cast<nb::sequence>(value);
            if (nb::len(sequence) != 3) {
                throw nb::value_error((std::string(label) + " must contain (x, y, z)").c_str());
            }
            return {nb::cast<float>(sequence[0]), nb::cast<float>(sequence[1]), nb::cast<float>(sequence[2])};
        }

        std::vector<termin::Vec3f> positions(nb::sequence values, const char* label) {
            std::vector<termin::Vec3f> result;
            result.reserve(nb::len(values));
            for (nb::handle value : values) {
                result.push_back(vec3f(value, label));
            }
            return result;
        }

        std::vector<std::uint32_t> triangle_indices(nb::sequence triangles) {
            std::vector<std::uint32_t> result;
            result.reserve(nb::len(triangles) * 3);
            for (nb::handle triangle_value : triangles) {
                nb::sequence triangle = nb::cast<nb::sequence>(triangle_value);
                if (nb::len(triangle) != 3) {
                    throw nb::value_error("each triangle must contain three vertex indexes");
                }
                result.push_back(nb::cast<std::uint32_t>(triangle[0]));
                result.push_back(nb::cast<std::uint32_t>(triangle[1]));
                result.push_back(nb::cast<std::uint32_t>(triangle[2]));
            }
            return result;
        }

        std::vector<termin::LinearColor> colors(nb::object values, std::size_t count, const char* label) {
            if (values.is_none()) {
                return std::vector<termin::LinearColor>(count, {1.0f, 1.0f, 1.0f, 1.0f});
            }
            nb::sequence sequence = nb::cast<nb::sequence>(values);
            if (nb::len(sequence) != count) {
                throw nb::value_error((std::string(label) + " count must match point/vertex count").c_str());
            }
            std::vector<termin::LinearColor> result;
            result.reserve(count);
            for (nb::handle value : sequence) {
                result.push_back(linear_color(nb::cast<termin::SrgbColor>(value), label));
            }
            return result;
        }

        std::vector<std::uint64_t> triangle_parts(nb::object values, std::size_t triangle_count) {
            if (values.is_none()) {
                return {};
            }
            nb::sequence sequence = nb::cast<nb::sequence>(values);
            if (nb::len(sequence) != triangle_count) {
                throw nb::value_error("triangle_parts count must match triangle count");
            }
            std::vector<std::uint64_t> result;
            result.reserve(triangle_count);
            for (nb::handle value : sequence) {
                result.push_back(nb::cast<std::uint64_t>(value));
            }
            return result;
        }

        std::shared_ptr<const PrimitiveGeometry3D> primitive_geometry(nb::sequence vertex_values,
                                                                      nb::sequence triangles,
                                                                      nb::object color_values,
                                                                      nb::object part_values) {
            const auto parsed_positions = positions(vertex_values, "vertex");
            const auto parsed_colors = colors(color_values, parsed_positions.size(), "colors");
            auto geometry = std::make_shared<PrimitiveGeometry3D>();
            geometry->vertices.reserve(parsed_positions.size());
            for (std::size_t index = 0; index < parsed_positions.size(); ++index) {
                geometry->vertices.push_back({parsed_positions[index], parsed_colors[index]});
            }
            geometry->triangles = triangle_indices(triangles);
            geometry->triangle_parts = triangle_parts(part_values, geometry->triangles.size() / 3);
            return geometry;
        }

        std::vector<float> size_scales(nb::object values, std::size_t count) {
            if (values.is_none()) {
                return std::vector<float>(count, 1.0f);
            }
            nb::sequence sequence = nb::cast<nb::sequence>(values);
            if (nb::len(sequence) != count) {
                throw nb::value_error("size_scales count must match point count");
            }
            std::vector<float> result;
            result.reserve(count);
            for (nb::handle value : sequence) {
                result.push_back(nb::cast<float>(value));
            }
            return result;
        }

        std::shared_ptr<const PointCloudData3D>
        point_cloud(nb::sequence point_values, nb::object color_values, nb::object size_values) {
            const auto parsed_positions = positions(point_values, "point");
            const auto parsed_colors = colors(color_values, parsed_positions.size(), "colors");
            const auto parsed_sizes = size_scales(size_values, parsed_positions.size());
            auto cloud = std::make_shared<PointCloudData3D>();
            cloud->points.reserve(parsed_positions.size());
            for (std::size_t index = 0; index < parsed_positions.size(); ++index) {
                cloud->points.push_back({parsed_positions[index], parsed_sizes[index], parsed_colors[index]});
            }
            return cloud;
        }

        std::shared_ptr<const termin::Mesh3> mesh_snapshot(const termin::Mesh3& mesh) {
            return std::make_shared<termin::Mesh3>(mesh);
        }

        nb::object bounds_value(const VisualItemRef3D& ref, bool world) {
            VisualBounds3D bounds{};
            if (!tc_visual_item3d_local_bounds_in_scene(ref.owner().handle(), ref.handle, &bounds)) {
                return nb::none();
            }
            if (world) {
                const auto transform = ref.owner().world_transform(ref.item());
                const termin::Vec3 corners[8] = {
                    {bounds.min.x, bounds.min.y, bounds.min.z},
                    {bounds.max.x, bounds.min.y, bounds.min.z},
                    {bounds.min.x, bounds.max.y, bounds.min.z},
                    {bounds.max.x, bounds.max.y, bounds.min.z},
                    {bounds.min.x, bounds.min.y, bounds.max.z},
                    {bounds.max.x, bounds.min.y, bounds.max.z},
                    {bounds.min.x, bounds.max.y, bounds.max.z},
                    {bounds.max.x, bounds.max.y, bounds.max.z},
                };
                bounds.min = transform.transform_point(corners[0]);
                bounds.max = bounds.min;
                for (std::size_t index = 1; index < 8; ++index) {
                    const auto point = transform.transform_point(corners[index]);
                    bounds.min.x = std::min(bounds.min.x, point.x);
                    bounds.min.y = std::min(bounds.min.y, point.y);
                    bounds.min.z = std::min(bounds.min.z, point.z);
                    bounds.max.x = std::max(bounds.max.x, point.x);
                    bounds.max.y = std::max(bounds.max.y, point.y);
                    bounds.max.z = std::max(bounds.max.z, point.z);
                }
            }
            return nb::make_tuple(bounds.min.x, bounds.min.y, bounds.min.z, bounds.max.x, bounds.max.y, bounds.max.z);
        }

        nb::object optional_item(TcVisualScene3D scene, VisualItem3DHandle handle) {
            return tc_visual_item3d_handle_is_invalid(handle) ? nb::none() : wrap_item(scene, handle);
        }

        nb::object optional_hit(TcVisualScene3D scene, const std::optional<HitResult3D>& hit) {
            return hit ? nb::cast(HitResultRef3D{scene, *hit}) : nb::none();
        }

        void require_callable_or_none(const nb::object& callback, const char* label) {
            if (!callback.is_none() && !nb::isinstance<nb::callable>(callback)) {
                throw nb::type_error((std::string(label) + " must be callable or None").c_str());
            }
        }

    } // namespace

    void bind_visual_scene3d(nb::module_& m) {
        nb::module_::import_("tcbase._geom_native");
        nb::module_::import_("tmesh._tmesh_native");
        nb::module_::import_("tgfx._tgfx_native");

        nb::class_<VisualItem3DHandle>(m, "VisualItem3DHandle")
            .def_prop_ro("scene_id", [](VisualItem3DHandle value) { return value.scene_id; })
            .def_prop_ro("index", [](VisualItem3DHandle value) { return value.index; })
            .def_prop_ro("generation", [](VisualItem3DHandle value) { return value.generation; })
            .def("__eq__", &same_handle);

        nb::class_<VisualItemRef3D>(m, "VisualItemRef3D")
            .def_prop_ro("valid", &VisualItemRef3D::valid)
            .def_prop_ro("handle", [](const VisualItemRef3D& self) { return self.handle; })
            .def_prop_ro("type_name",
                         [](const VisualItemRef3D& self) {
                             const char* value = tc_visual_item3d_type_name(&self.item());
                             return value ? value : "";
                         })
            .def_prop_ro("parent",
                         [](const VisualItemRef3D& self) {
                             tc_visual_item3d* parent = self.item().parent;
                             return parent ? wrap_item(self.scene, parent->handle) : nb::none();
                         })
            .def_prop_ro("children",
                         [](const VisualItemRef3D& self) {
                             nb::list result;
                             const auto& item = self.item();
                             for (std::size_t index = 0; index < item.child_count; ++index) {
                                 result.append(wrap_item(self.scene, item.children[index]->handle));
                             }
                             return result;
                         })
            .def_prop_rw(
                "local_transform",
                [](const VisualItemRef3D& self) {
                    termin::Affine3d value{};
                    if (!tc_visual_item3d_get_local_transform(self.owner().handle(), self.handle, &value)) {
                        stale_reference("VisualItemRef3D is stale");
                    }
                    return value;
                },
                [](const VisualItemRef3D& self, termin::Affine3d value) {
                    self.item();
                    if (!tc_visual_item3d_set_local_transform(self.scene.handle(), self.handle, value)) {
                        throw nb::value_error("local_transform must be finite");
                    }
                })
            .def_prop_ro("world_transform",
                         [](const VisualItemRef3D& self) { return self.owner().world_transform(self.item()); })
            .def_prop_rw(
                "visible",
                [](const VisualItemRef3D& self) {
                    bool value = false;
                    if (!tc_visual_item3d_get_visible(self.owner().handle(), self.handle, &value)) {
                        stale_reference("VisualItemRef3D is stale");
                    }
                    return value;
                },
                [](const VisualItemRef3D& self, bool value) {
                    if (!tc_visual_item3d_set_visible(self.owner().handle(), self.handle, value)) {
                        stale_reference("VisualItemRef3D is stale");
                    }
                })
            .def_prop_rw(
                "enabled",
                [](const VisualItemRef3D& self) {
                    bool value = false;
                    if (!tc_visual_item3d_get_enabled(self.owner().handle(), self.handle, &value)) {
                        stale_reference("VisualItemRef3D is stale");
                    }
                    return value;
                },
                [](const VisualItemRef3D& self, bool value) {
                    if (!tc_visual_item3d_set_enabled(self.owner().handle(), self.handle, value)) {
                        stale_reference("VisualItemRef3D is stale");
                    }
                })
            .def_prop_ro("effective_visible",
                         [](const VisualItemRef3D& self) { return self.owner().effective_visible(self.item()); })
            .def_prop_ro("effective_enabled",
                         [](const VisualItemRef3D& self) { return self.owner().effective_enabled(self.item()); })
            .def_prop_ro("local_bounds", [](const VisualItemRef3D& self) { return bounds_value(self, false); })
            .def_prop_ro("world_bounds", [](const VisualItemRef3D& self) { return bounds_value(self, true); })
            .def(
                "reparent",
                [](const VisualItemRef3D& self, nb::object parent) {
                    if (parent.is_none()) {
                        return tc_visual_item3d_set_parent_in_scene(
                            self.owner().handle(), self.handle, tc_visual_item3d_handle_invalid(), 0);
                    }
                    const auto& parent_ref = nb::cast<const VisualItemRef3D&>(parent);
                    if (!same_scene(self.owner(), parent_ref.owner())) {
                        throw nb::value_error("parent belongs to another TcVisualScene3D");
                    }
                    return tc_visual_item3d_set_parent_in_scene(
                        self.scene.handle(),
                        self.handle,
                        parent_ref.handle,
                        tc_visual_item3d_child_count_in_scene(self.scene.handle(), parent_ref.handle));
                },
                nb::arg("parent").none() = nb::none())
            .def("destroy", [](const VisualItemRef3D& self) { return self.owner().destroy(self.handle); })
            .def("__bool__", &VisualItemRef3D::valid)
            .def("__eq__", [](const VisualItemRef3D& self, const VisualItemRef3D& other) {
                return same_scene(self.scene, other.scene) && same_handle(self.handle, other.handle);
            });

        nb::class_<PrimitiveItemRef3D, VisualItemRef3D>(m, "PrimitiveItemRef3D")
            .def_prop_rw(
                "depth_test",
                [](const PrimitiveItemRef3D& self) {
                    return require_item<PrimitiveItem3D>(self, "termin.visual.Primitive3D").depth_test();
                },
                [](const PrimitiveItemRef3D& self, bool value) {
                    require_item<PrimitiveItem3D>(self, "termin.visual.Primitive3D").set_depth_test(value);
                })
            .def(
                "set_geometry",
                [](const PrimitiveItemRef3D& self,
                   nb::sequence vertices,
                   nb::sequence triangles,
                   nb::object color_values,
                   nb::object part_values) {
                    require_item<PrimitiveItem3D>(self, "termin.visual.Primitive3D")
                        .set_geometry(primitive_geometry(vertices, triangles, color_values, part_values));
                },
                nb::arg("vertices"),
                nb::arg("triangles"),
                nb::arg("colors").none() = nb::none(),
                nb::arg("triangle_parts").none() = nb::none());

        nb::class_<StaticMeshItemRef3D, VisualItemRef3D>(m, "StaticMeshItemRef3D")
            .def_prop_rw(
                "depth_test",
                [](const StaticMeshItemRef3D& self) {
                    return require_item<StaticMeshItem3D>(self, "termin.visual.StaticMesh3D").depth_test();
                },
                [](const StaticMeshItemRef3D& self, bool value) {
                    require_item<StaticMeshItem3D>(self, "termin.visual.StaticMesh3D").set_depth_test(value);
                })
            .def_prop_ro("tint",
                         [](const StaticMeshItemRef3D& self) {
                             return require_item<StaticMeshItem3D>(self, "termin.visual.StaticMesh3D").tint();
                         })
            .def("set_mesh",
                 [](const StaticMeshItemRef3D& self, const termin::Mesh3& mesh) {
                     require_item<StaticMeshItem3D>(self, "termin.visual.StaticMesh3D").set_mesh(mesh_snapshot(mesh));
                 })
            .def("set_tint", [](const StaticMeshItemRef3D& self, termin::SrgbColor tint) {
                require_item<StaticMeshItem3D>(self, "termin.visual.StaticMesh3D").set_tint(linear_color(tint, "tint"));
            });

        nb::class_<PointCloudItemRef3D, VisualItemRef3D>(m, "PointCloudItemRef3D")
            .def_prop_rw(
                "style",
                [](const PointCloudItemRef3D& self) {
                    return require_item<PointCloudItem3D>(self, "termin.visual.PointCloud3D").style();
                },
                [](const PointCloudItemRef3D& self, tgfx::PointCloudStyle style) {
                    require_item<PointCloudItem3D>(self, "termin.visual.PointCloud3D").set_style(style);
                })
            .def_prop_rw(
                "pick_radius",
                [](const PointCloudItemRef3D& self) {
                    return require_item<PointCloudItem3D>(self, "termin.visual.PointCloud3D").pick_radius();
                },
                [](const PointCloudItemRef3D& self, double value) {
                    require_item<PointCloudItem3D>(self, "termin.visual.PointCloud3D").set_pick_radius(value);
                })
            .def(
                "set_points",
                [](const PointCloudItemRef3D& self,
                   nb::sequence points,
                   nb::object color_values,
                   nb::object size_values) {
                    require_item<PointCloudItem3D>(self, "termin.visual.PointCloud3D")
                        .set_cloud(point_cloud(points, color_values, size_values));
                },
                nb::arg("points"),
                nb::arg("colors").none() = nb::none(),
                nb::arg("size_scales").none() = nb::none());

        nb::class_<HitResultRef3D>(m, "HitResult3D")
            .def_prop_ro("item", [](const HitResultRef3D& self) { return wrap_item(self.scene, self.hit.item); })
            .def_prop_ro("distance", [](const HitResultRef3D& self) { return self.hit.distance; })
            .def_prop_ro("part", [](const HitResultRef3D& self) { return self.hit.part; })
            .def_prop_ro("world_point", [](const HitResultRef3D& self) { return self.hit.world_point; })
            .def_prop_ro("local_point", [](const HitResultRef3D& self) { return self.hit.local_point; });

        nb::enum_<PointerEventKind3D>(m, "PointerEventKind3D")
            .value("Move", PointerEventKind3D::Move)
            .value("Down", PointerEventKind3D::Down)
            .value("Up", PointerEventKind3D::Up)
            .value("Cancel", PointerEventKind3D::Cancel);

        nb::class_<PointerEvent3D>(m, "PointerEvent3D")
            .def(nb::init<>())
            .def_rw("pointer", &PointerEvent3D::pointer)
            .def_rw("kind", &PointerEvent3D::kind)
            .def_rw("world_ray", &PointerEvent3D::world_ray)
            .def_rw("button", &PointerEvent3D::button);

        nb::class_<ActionEventRef3D>(m, "ActionEvent3D")
            .def_prop_ro("target",
                         [](const ActionEventRef3D& self) { return wrap_item(self.scene, self.event.target); })
            .def_prop_ro("pointer", [](const ActionEventRef3D& self) { return self.event.pointer; })
            .def_prop_ro("part", [](const ActionEventRef3D& self) { return self.event.part; })
            .def_prop_ro("action", [](const ActionEventRef3D& self) { return self.event.action; });

        nb::class_<PointerDispatchRef3D>(m, "PointerDispatch3D")
            .def_prop_ro("event", [](const PointerDispatchRef3D& self) { return self.dispatch.event; })
            .def_prop_ro(
                "target",
                [](const PointerDispatchRef3D& self) { return optional_item(self.scene, self.dispatch.target); })
            .def_prop_ro("hit",
                         [](const PointerDispatchRef3D& self) { return optional_hit(self.scene, self.dispatch.hit); })
            .def_prop_ro(
                "hovered",
                [](const PointerDispatchRef3D& self) { return optional_item(self.scene, self.dispatch.hovered); })
            .def_prop_ro(
                "pressed",
                [](const PointerDispatchRef3D& self) { return optional_item(self.scene, self.dispatch.pressed); })
            .def_prop_ro(
                "captured",
                [](const PointerDispatchRef3D& self) { return optional_item(self.scene, self.dispatch.captured); })
            .def_prop_ro("used_fallback", [](const PointerDispatchRef3D& self) { return self.dispatch.used_fallback; })
            .def_prop_ro("callback_failed",
                         [](const PointerDispatchRef3D& self) { return self.dispatch.callback_failed; })
            .def_prop_ro("action", [](const PointerDispatchRef3D& self) -> nb::object {
                return self.dispatch.action ? nb::cast(ActionEventRef3D{self.scene, *self.dispatch.action})
                                            : nb::none();
            });

        m.def("tc_visual_scene3d_create", [] {
            const auto handle = tc_visual_scene3d_create();
            if (tc_visual_scene3d_handle_is_invalid(handle)) {
                throw std::runtime_error("failed to create 3D visual scene");
            }
            return TcVisualScene3D{handle};
        });
        m.def("tc_visual_scene3d_destroy", [](TcVisualScene3D& scene) {
            tc_visual_scene3d_destroy(scene.handle());
            scene = TcVisualScene3D{};
        });

        nb::class_<TcVisualScene3D>(m, "TcVisualScene3D")
            .def_prop_ro("valid", &TcVisualScene3D::valid)
            .def_prop_ro("size", &TcVisualScene3D::size)
            .def_prop_ro("items",
                         [](TcVisualScene3D self) {
                             nb::list result;
                             for (auto* item : self.items()) {
                                 result.append(wrap_item(self, item->handle));
                             }
                             return result;
                         })
            .def(
                "create_group",
                [](TcVisualScene3D self, nb::object parent) {
                    const auto handle = adopt_owned(self, std::make_unique<GroupItem3D>(), parent_item(self, parent));
                    return VisualItemRef3D{self, handle};
                },
                nb::arg("parent").none() = nb::none())
            .def(
                "create_primitive",
                [](TcVisualScene3D self,
                   nb::sequence vertices,
                   nb::sequence triangles,
                   nb::object color_values,
                   nb::object part_values,
                   bool depth_test,
                   nb::object parent) {
                    const auto handle =
                        adopt_owned(self,
                                    std::make_unique<PrimitiveItem3D>(
                                        primitive_geometry(vertices, triangles, color_values, part_values), depth_test),
                                    parent_item(self, parent));
                    return typed_ref<PrimitiveItemRef3D>(self, handle);
                },
                nb::arg("vertices"),
                nb::arg("triangles"),
                nb::arg("colors").none() = nb::none(),
                nb::arg("triangle_parts").none() = nb::none(),
                nb::arg("depth_test") = true,
                nb::arg("parent").none() = nb::none())
            .def(
                "create_static_mesh",
                [](TcVisualScene3D self,
                   const termin::Mesh3& mesh,
                   termin::SrgbColor tint,
                   bool depth_test,
                   nb::object parent) {
                    const auto handle = adopt_owned(
                        self,
                        std::make_unique<StaticMeshItem3D>(mesh_snapshot(mesh), linear_color(tint, "tint"), depth_test),
                        parent_item(self, parent));
                    return typed_ref<StaticMeshItemRef3D>(self, handle);
                },
                nb::arg("mesh"),
                nb::arg("tint") = termin::SrgbColor::white(),
                nb::arg("depth_test") = true,
                nb::arg("parent").none() = nb::none())
            .def(
                "create_point_cloud",
                [](TcVisualScene3D self,
                   nb::sequence points,
                   nb::object color_values,
                   nb::object size_values,
                   tgfx::PointCloudStyle style,
                   double pick_radius,
                   nb::object parent) {
                    const auto handle =
                        adopt_owned(self,
                                    std::make_unique<PointCloudItem3D>(
                                        point_cloud(points, color_values, size_values), style, pick_radius),
                                    parent_item(self, parent));
                    return typed_ref<PointCloudItemRef3D>(self, handle);
                },
                nb::arg("points"),
                nb::arg("colors").none() = nb::none(),
                nb::arg("size_scales").none() = nb::none(),
                nb::arg("style") = tgfx::PointCloudStyle{},
                nb::arg("pick_radius") = 0.05,
                nb::arg("parent").none() = nb::none())
            .def("clear", &TcVisualScene3D::clear)
            .def(
                "destroy",
                [](TcVisualScene3D self, const VisualItemRef3D& item) {
                    if (!same_scene(self, item.owner())) {
                        return false;
                    }
                    return self.destroy(item.handle);
                },
                nb::arg("item"))
            .def(
                "hit_test",
                [](TcVisualScene3D self, termin::Ray3 ray) -> nb::object {
                    const auto hit = termin::visual::hit_test(self, ray);
                    return optional_hit(self, hit);
                },
                nb::arg("ray"));

        nb::class_<PythonSceneInteraction3D>(m, "SceneInteraction3D")
            .def(nb::init<>())
            .def(
                "route",
                [](PythonSceneInteraction3D& self,
                   TcVisualScene3D scene,
                   PointerId3D pointer,
                   PointerEventKind3D kind,
                   termin::Ray3 world_ray,
                   std::uint32_t button) {
                    if (!scene.valid()) {
                        stale_reference("TcVisualScene3D has been destroyed");
                    }
                    return PointerDispatchRef3D{
                        scene,
                        self.interaction.route(scene, PointerEvent3D{pointer, kind, world_ray, button}),
                    };
                },
                nb::arg("scene"),
                nb::arg("pointer"),
                nb::arg("kind"),
                nb::arg("world_ray"),
                nb::arg("button") = 0)
            .def("capture",
                 [](PythonSceneInteraction3D& self,
                    TcVisualScene3D scene,
                    PointerId3D pointer,
                    const VisualItemRef3D& target) {
                     if (!same_scene(scene, target.owner())) {
                         return false;
                     }
                     return self.interaction.capture(scene, pointer, target.handle);
                 })
            .def("release",
                 [](PythonSceneInteraction3D& self, PointerId3D pointer) { self.interaction.release(pointer); })
            .def("cancel_all", [](PythonSceneInteraction3D& self) { self.interaction.cancel_all(); })
            .def(
                "set_action_handler",
                [](PythonSceneInteraction3D& self, const VisualItemRef3D& item, nb::object callback) {
                    require_callable_or_none(callback, "action handler");
                    if (callback.is_none()) {
                        self.interaction.clear_action_handler(item.handle);
                        return;
                    }
                    const auto scene = item.owner();
                    self.interaction.set_action_handler(
                        item.handle, [scene, callback = std::move(callback)](const ActionEvent3D& event) {
                            nb::gil_scoped_acquire gil;
                            callback(ActionEventRef3D{scene, event});
                        });
                },
                nb::arg("item"),
                nb::arg("callback").none())
            .def(
                "set_fallback_handler",
                [](PythonSceneInteraction3D& self, nb::object callback) {
                    require_callable_or_none(callback, "fallback handler");
                    if (callback.is_none()) {
                        self.interaction.set_fallback_handler({});
                        return;
                    }
                    self.interaction.set_fallback_handler(
                        [callback = std::move(callback)](const PointerEvent3D& event) {
                            nb::gil_scoped_acquire gil;
                            callback(event);
                        });
                },
                nb::arg("callback").none())
            .def("hovered",
                 [](const PythonSceneInteraction3D& self, TcVisualScene3D scene, PointerId3D pointer) {
                     return optional_item(scene, self.interaction.hovered(pointer));
                 })
            .def("pressed",
                 [](const PythonSceneInteraction3D& self, TcVisualScene3D scene, PointerId3D pointer) {
                     return optional_item(scene, self.interaction.pressed(pointer));
                 })
            .def("captured", [](const PythonSceneInteraction3D& self, TcVisualScene3D scene, PointerId3D pointer) {
                return optional_item(scene, self.interaction.captured(pointer));
            });
    }

} // namespace termin::visual::python
