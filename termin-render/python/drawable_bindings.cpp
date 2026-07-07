#include <nanobind/nanobind.h>

#include <termin/entity/entity.hpp>
#include <termin/render/drawable.hpp>
#include <termin/render/python_render_item.hpp>

extern "C" {
#include <tgfx/resources/tc_material.h>
#include <tgfx/resources/tc_material_registry.h>
}

namespace nb = nanobind;

namespace termin {

PythonRenderItem PythonRenderItem::mesh(
    const TcMesh& mesh,
    tc_material_phase* phase,
    int geometry_id,
    size_t submesh_index)
{
    PythonRenderItem result{};
    result.item.kind = TC_RENDER_ITEM_KIND_MESH;
    result.item.flags = TC_RENDER_ITEM_FLAG_NONE;
    result.item.geometry_id = geometry_id;
    result.item.material_phase = phase;
    result.item.material = tc_material_handle_invalid();
    result.item.material_phase_index = SIZE_MAX;
    if (phase) {
        result.item.flags |= TC_RENDER_ITEM_FLAG_HAS_MATERIAL_PHASE;
        tc_material_find_phase_ref(phase, &result.item.material, &result.item.material_phase_index);
    }
    result.item.payload.mesh.mesh = mesh.get();
    result.item.payload.mesh.mesh_handle = mesh.handle;
    result.item.payload.mesh.submesh_index = submesh_index;
    return result;
}

void bind_drawable(nb::module_& m) {
    nb::module_::import_("termin.materials._materials_native");
    nb::module_::import_("tmesh._tmesh_native");

    m.attr("RENDER_ITEM_KIND_MESH") =
        nb::int_(static_cast<int>(TC_RENDER_ITEM_KIND_MESH));
    m.attr("RENDER_ITEM_COLLECT_ALLOW_MISSING_MATERIAL_PHASE") =
        nb::int_(static_cast<int>(TC_RENDER_ITEM_COLLECT_FLAG_ALLOW_MISSING_MATERIAL_PHASE));

    nb::class_<GeometryDrawCall>(m, "GeometryDrawCall")
        .def(nb::init<>())
        .def("__init__", [](GeometryDrawCall* self, tc_material_phase* phase, int geometry_id) {
            new (self) GeometryDrawCall{phase, geometry_id};
        }, nb::arg("phase"), nb::arg("geometry_id") = 0)
        .def_prop_rw("phase",
            [](GeometryDrawCall& self) { return self.resolve_phase(); },
            [](GeometryDrawCall& self, tc_material_phase* phase) { self.bind_phase_ref(phase); })
        .def_rw("geometry_id", &GeometryDrawCall::geometry_id);

    nb::class_<PhaseDrawCall>(m, "PhaseDrawCall")
        .def(nb::init<>())
        .def_rw("entity", &PhaseDrawCall::entity)
        .def_prop_rw("phase",
            [](PhaseDrawCall& self) { return self.resolve_phase(); },
            [](PhaseDrawCall& self, tc_material_phase* phase) {
                self.phase = phase;
                self.material = tc_material_handle_invalid();
                self.phase_index = SIZE_MAX;
                tc_material_find_phase_ref(phase, &self.material, &self.phase_index);
            })
        .def_rw("priority", &PhaseDrawCall::priority)
        .def_rw("geometry_id", &PhaseDrawCall::geometry_id);

    nb::class_<PythonRenderItem>(m, "RenderItem")
        .def(nb::init<>())
        .def_static("mesh",
            [](const TcMesh& mesh,
               nb::object phase_obj,
               int geometry_id,
               size_t submesh_index) {
                tc_material_phase* phase = nullptr;
                if (!phase_obj.is_none()) {
                    phase = nb::cast<tc_material_phase*>(phase_obj);
                }
                return PythonRenderItem::mesh(mesh, phase, geometry_id, submesh_index);
            },
            nb::arg("mesh"),
            nb::arg("phase") = nb::none(),
            nb::arg("geometry_id") = 0,
            nb::arg("submesh_index") = 0)
        .def_prop_ro("kind", [](const PythonRenderItem& self) {
            return self.item.kind;
        })
        .def_prop_ro("geometry_id", [](const PythonRenderItem& self) {
            return self.item.geometry_id;
        });
}

} // namespace termin
