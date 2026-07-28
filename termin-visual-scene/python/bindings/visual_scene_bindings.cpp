#include <functional>
#include <memory>
#include <stdexcept>
#include <utility>

#include <nanobind/nanobind.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/vector.h>

#include <tcbase/tc_log.hpp>
#include <tcbase/tc_trent_json.hpp>

#include "termin_visual_scene/scene_inspection2d.hpp"

namespace nb = nanobind;

namespace {

using termin::visual::GraphicItemHandle;
using termin::visual::GraphicItemSnapshot2D;
using termin::visual::GraphicItemState2D;
using termin::visual::VisualScene2D;

struct SceneLifetime2D {
    VisualScene2D* scene = nullptr;
};

[[noreturn]] void stale_reference(const char* message) {
    PyErr_SetString(PyExc_ReferenceError, message);
    throw nb::python_error();
}

struct GraphicItemRef2D {
    std::shared_ptr<SceneLifetime2D> lifetime;
    GraphicItemHandle handle = tc_graphic_item_handle_invalid();

    bool valid() const {
        return lifetime && lifetime->scene
            && lifetime->scene->contains(handle);
    }

    VisualScene2D& scene() const {
        if (!lifetime || !lifetime->scene) {
            stale_reference("VisualScene2D has been destroyed");
        }
        if (!lifetime->scene->contains(handle)) {
            stale_reference("GraphicItemRef2D is stale");
        }
        return *lifetime->scene;
    }

    GraphicItemSnapshot2D snapshot() const {
        auto value = scene().snapshot(handle);
        if (!value) {
            stale_reference("GraphicItemRef2D is stale");
        }
        return std::move(*value);
    }
};

class PythonVisualScene2D {
public:
    PythonVisualScene2D()
        : scene_(std::make_unique<VisualScene2D>()),
          lifetime_(std::make_shared<SceneLifetime2D>()) {
        lifetime_->scene = scene_.get();
    }

    ~PythonVisualScene2D() {
        lifetime_->scene = nullptr;
        scene_.reset();
    }

    PythonVisualScene2D(const PythonVisualScene2D&) = delete;
    PythonVisualScene2D& operator=(const PythonVisualScene2D&) = delete;

    VisualScene2D& scene() { return *scene_; }
    const VisualScene2D& scene() const { return *scene_; }
    const std::shared_ptr<SceneLifetime2D>& lifetime() const {
        return lifetime_;
    }

private:
    std::unique_ptr<VisualScene2D> scene_;
    std::shared_ptr<SceneLifetime2D> lifetime_;
};

GraphicItemHandle parent_handle(
    const PythonVisualScene2D& owner,
    nb::object parent) {
    if (parent.is_none()) return tc_graphic_item_handle_invalid();
    const auto& ref = nb::cast<const GraphicItemRef2D&>(parent);
    if (ref.lifetime != owner.lifetime()) {
        throw nb::value_error("parent belongs to another VisualScene2D");
    }
    if (!ref.valid()) stale_reference("parent item is stale");
    return ref.handle;
}

tgfx::Color4f parse_color(nb::tuple value) {
    if (value.size() != 4) {
        throw nb::value_error("color must contain (r, g, b, a)");
    }
    tgfx::Color4f result{
        nb::cast<float>(value[0]),
        nb::cast<float>(value[1]),
        nb::cast<float>(value[2]),
        nb::cast<float>(value[3]),
    };
    if (!result.is_finite()) {
        throw nb::value_error("color must be finite");
    }
    return result;
}

GraphicItemRef2D wrap_created(
    PythonVisualScene2D& owner,
    std::optional<GraphicItemHandle> handle) {
    if (!handle) throw nb::value_error("graphic item was rejected");
    return {owner.lifetime(), *handle};
}

nb::tuple affine_tuple(const termin::Affine2f& value) {
    return nb::make_tuple(
        value.m00,
        value.m01,
        value.m10,
        value.m11,
        value.tx,
        value.ty);
}

nb::object bounds_value(
    const std::optional<termin::Bounds2f>& value) {
    if (!value) return nb::none();
    return nb::make_tuple(value->x0, value->y0, value->x1, value->y1);
}

nb::dict detached_snapshot(const GraphicItemSnapshot2D& value) {
    nb::dict result;
    result["type"] = termin::visual::payload_type_name(value.payload);
    result["stable_order"] = value.stable_order;
    result["local_transform"] = affine_tuple(value.state.local_transform);
    result["world_transform"] = affine_tuple(value.world_transform);
    result["visible"] = value.state.visible;
    result["enabled"] = value.state.enabled;
    result["opacity"] = value.state.opacity;
    result["z_order"] = value.state.z_order;
    result["effective_visible"] = value.effective_visible;
    result["effective_enabled"] = value.effective_enabled;
    result["effective_opacity"] = value.effective_opacity;
    result["revision"] = value.revision;
    result["topology_revision"] = value.topology_revision;
    result["depth"] = value.depth;
    result["diagnostics"] =
        static_cast<std::uint32_t>(value.diagnostics);
    result["local_bounds"] = bounds_value(value.local_bounds);
    result["world_bounds"] = bounds_value(value.world_bounds);
    return result;
}

nb::dict detached_inspection(const PythonVisualScene2D& owner) {
    const auto inspected = owner.scene().inspection();
    nb::dict result;
    result["schema_version"] = inspected.schema_version;
    result["scene_revision"] = inspected.scene_revision;
    nb::list items;
    for (const auto& item : inspected.items) {
        nb::dict encoded;
        encoded["record_index"] = item.record_index;
        encoded["parent_index"] = item.parent_index
            ? nb::cast(*item.parent_index)
            : nb::none();
        encoded["children"] = nb::cast(item.children);
        encoded["type"] = item.type_name;
        encoded["stable_id"] = item.stable_id;
        encoded["local_transform"] =
            affine_tuple(item.state.local_transform);
        encoded["world_transform"] =
            affine_tuple(item.world_transform);
        encoded["visible"] = item.state.visible;
        encoded["enabled"] = item.state.enabled;
        encoded["opacity"] = item.state.opacity;
        encoded["z_order"] = item.state.z_order;
        encoded["effective_visible"] = item.effective_visible;
        encoded["effective_enabled"] = item.effective_enabled;
        encoded["effective_opacity"] = item.effective_opacity;
        encoded["revision"] = item.revision;
        encoded["topology_revision"] = item.topology_revision;
        encoded["depth"] = item.depth;
        encoded["diagnostics"] =
            static_cast<std::uint32_t>(item.diagnostics);
        encoded["local_bounds"] = bounds_value(item.local_bounds);
        encoded["world_bounds"] = bounds_value(item.world_bounds);
        items.append(std::move(encoded));
    }
    result["items"] = std::move(items);
    return result;
}

void update_state(
    const GraphicItemRef2D& ref,
    const std::function<void(GraphicItemState2D&)>& mutation) {
    GraphicItemState2D state = ref.snapshot().state;
    mutation(state);
    if (!ref.scene().set_state(ref.handle, std::move(state))) {
        throw nb::value_error("graphic item state was rejected");
    }
}

}  // namespace

NB_MODULE(_visual_scene_native, m) {
    m.doc() = "Handle-safe retained 2D visual scene bindings";

    nb::class_<GraphicItemRef2D>(m, "GraphicItemRef2D")
        .def_prop_ro("valid", &GraphicItemRef2D::valid)
        .def("snapshot", [](const GraphicItemRef2D& self) {
            return detached_snapshot(self.snapshot());
        })
        .def("set_transform",
             [](const GraphicItemRef2D& self,
                float m00,
                float m01,
                float m10,
                float m11,
                float tx,
                float ty) {
                 update_state(
                     self,
                     [&](GraphicItemState2D& state) {
                         state.local_transform =
                             {m00, m01, m10, m11, tx, ty};
                     });
             })
        .def("set_visible",
             [](const GraphicItemRef2D& self, bool value) {
                 update_state(
                     self,
                     [&](GraphicItemState2D& state) {
                         state.visible = value;
                     });
             })
        .def("set_enabled",
             [](const GraphicItemRef2D& self, bool value) {
                 update_state(
                     self,
                     [&](GraphicItemState2D& state) {
                         state.enabled = value;
                     });
             })
        .def("set_opacity",
             [](const GraphicItemRef2D& self, float value) {
                 update_state(
                     self,
                     [&](GraphicItemState2D& state) {
                         state.opacity = value;
                     });
             })
        .def("destroy_leaf", [](const GraphicItemRef2D& self) {
            return self.scene().destroy_leaf(self.handle);
        })
        .def("destroy_subtree", [](const GraphicItemRef2D& self) {
            return self.scene().destroy_subtree(self.handle);
        })
        .def("__bool__", &GraphicItemRef2D::valid)
        .def("__eq__",
             [](const GraphicItemRef2D& self,
                const GraphicItemRef2D& other) {
                 return self.lifetime == other.lifetime
                     && self.handle.scene_id == other.handle.scene_id
                     && self.handle.index == other.handle.index
                     && self.handle.generation == other.handle.generation;
             });

    nb::class_<PythonVisualScene2D>(m, "VisualScene2D")
        .def(nb::init<>())
        .def_prop_ro("size", [](const PythonVisualScene2D& self) {
            return self.scene().size();
        })
        .def_prop_ro("revision", [](const PythonVisualScene2D& self) {
            return self.scene().revision();
        })
        .def("create_group",
             [](PythonVisualScene2D& self, nb::object parent) {
                 return wrap_created(
                     self,
                     self.scene().create(
                         termin::visual::GroupItem2D{},
                         parent_handle(self, parent)));
             },
             nb::arg("parent").none() = nb::none())
        .def("create_rect",
             [](PythonVisualScene2D& self,
                float x,
                float y,
                float width,
                float height,
                nb::tuple color,
                nb::object parent) {
                 return wrap_created(
                     self,
                     self.scene().create(
                         termin::visual::RectItem2D{
                             {x, y, width, height},
                             {parse_color(color), tgfx::FillRule::NonZero},
                             std::nullopt,
                         },
                         parent_handle(self, parent)));
             },
             nb::arg("x"),
             nb::arg("y"),
             nb::arg("width"),
             nb::arg("height"),
             nb::arg("color") = nb::make_tuple(1.0f, 1.0f, 1.0f, 1.0f),
             nb::arg("parent").none() = nb::none())
        .def("create_ellipse",
             [](PythonVisualScene2D& self,
                float x,
                float y,
                float width,
                float height,
                nb::tuple color,
                nb::object parent) {
                 return wrap_created(
                     self,
                     self.scene().create(
                         termin::visual::EllipseItem2D{
                             {x, y, width, height},
                             {parse_color(color), tgfx::FillRule::NonZero},
                             std::nullopt,
                         },
                         parent_handle(self, parent)));
             },
             nb::arg("x"),
             nb::arg("y"),
             nb::arg("width"),
             nb::arg("height"),
             nb::arg("color") = nb::make_tuple(1.0f, 1.0f, 1.0f, 1.0f),
             nb::arg("parent").none() = nb::none())
        .def("clear", [](PythonVisualScene2D& self) {
            self.scene().clear();
        })
        .def("inspection", &detached_inspection)
        .def("serialize_json", [](const PythonVisualScene2D& self) {
            return tc::json::dump(self.scene().serialize());
        })
        .def("restore_json",
             [](PythonVisualScene2D& self, const std::string& text) {
                 try {
                     return self.scene().restore(tc::json::parse(text));
                 } catch (const std::exception& error) {
                     tc::Log::error(
                         "VisualScene2D Python restore_json failed: %s",
                         error.what());
                     throw;
                 }
             },
             nb::arg("text"));
}
