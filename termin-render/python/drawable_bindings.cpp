#include <nanobind/nanobind.h>

#include <termin/entity/entity.hpp>
#include <termin/render/drawable.hpp>

extern "C" {
#include <tgfx/resources/tc_material.h>
}

namespace nb = nanobind;

namespace termin {

void bind_drawable(nb::module_& m) {
    nb::module_::import_("termin.materials._materials_native");

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
}

} // namespace termin
