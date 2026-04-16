// camera_bindings.cpp - OrbitCamera bindings.

#include <nanobind/nanobind.h>
#include <nanobind/ndarray.h>
#include <nanobind/stl/tuple.h>

#include "tcplot/orbit_camera.hpp"

namespace nb = nanobind;

namespace tcplot_bindings {

// Returning a numpy (4,4) float array is the shape callers expect from
// the prior Python camera3d.py. Allocated in Python heap so nanobind
// manages ownership.
static nb::object mat_to_ndarray(const float m[16]) {
    // Allocate a new float buffer in Python-managed heap and hand it to
    // nanobind as an ndarray. nanobind copies into a numpy array via
    // the ndarray->numpy() conversion.
    size_t shape[2] = {4, 4};
    // We need persistent memory — allocate via nb::make_tuple? Easier:
    // return a nested tuple and let the Python wrapper convert.
    //
    // Easiest clean path: return a flat tuple of 16 floats and let the
    // Python re-export layer reshape it into np.array((4,4)).
    // This sidesteps ndarray lifetime concerns.
    nb::list result;
    for (int i = 0; i < 16; i++) result.append(m[i]);
    return nb::tuple(result);
}

void bind_camera(nb::module_& m) {
    nb::class_<tcplot::OrbitCamera>(m, "OrbitCamera")
        .def(nb::init<>())

        // Public state fields.
        .def_prop_rw("target",
            [](const tcplot::OrbitCamera& c) {
                return std::make_tuple(c.target[0], c.target[1], c.target[2]);
            },
            [](tcplot::OrbitCamera& c, std::tuple<float, float, float> t) {
                auto [x, y, z] = t;
                c.target[0] = x;
                c.target[1] = y;
                c.target[2] = z;
            })
        .def_rw("distance",  &tcplot::OrbitCamera::distance)
        .def_rw("azimuth",   &tcplot::OrbitCamera::azimuth)
        .def_rw("elevation", &tcplot::OrbitCamera::elevation)
        .def_rw("fov_y",     &tcplot::OrbitCamera::fov_y)
        .def_rw("near",      &tcplot::OrbitCamera::near)
        .def_rw("far",       &tcplot::OrbitCamera::far)
        .def_rw("min_distance",  &tcplot::OrbitCamera::min_distance)
        .def_rw("max_distance",  &tcplot::OrbitCamera::max_distance)
        .def_rw("min_elevation", &tcplot::OrbitCamera::min_elevation)
        .def_rw("max_elevation", &tcplot::OrbitCamera::max_elevation)

        // Computed properties. Return flat 16-tuples; the Python
        // re-export layer reshapes to (4,4) numpy for parity with the
        // old camera3d.py API (view_matrix / mvp return np.ndarray).
        .def_prop_ro("eye", [](const tcplot::OrbitCamera& c) {
            float e[3];
            c.compute_eye(e);
            return std::make_tuple(e[0], e[1], e[2]);
        })

        .def("view_matrix", [](const tcplot::OrbitCamera& c) {
            float m[16];
            c.view_matrix(m);
            return mat_to_ndarray(m);
        })

        .def("projection_matrix", [](const tcplot::OrbitCamera& c, float aspect) {
            float m[16];
            c.projection_matrix(aspect, m);
            return mat_to_ndarray(m);
        }, nb::arg("aspect"))

        .def("mvp", [](const tcplot::OrbitCamera& c, float aspect) {
            float m[16];
            c.mvp(aspect, m);
            return mat_to_ndarray(m);
        }, nb::arg("aspect"))

        .def("orbit", &tcplot::OrbitCamera::orbit,
             nb::arg("d_azimuth"), nb::arg("d_elevation"))
        .def("zoom", &tcplot::OrbitCamera::zoom, nb::arg("factor"))
        .def("pan",  &tcplot::OrbitCamera::pan, nb::arg("dx"), nb::arg("dy"))

        .def("fit_bounds",
             [](tcplot::OrbitCamera& c,
                std::tuple<float, float, float> lo,
                std::tuple<float, float, float> hi) {
                 const float lo_f[3] = {std::get<0>(lo), std::get<1>(lo), std::get<2>(lo)};
                 const float hi_f[3] = {std::get<0>(hi), std::get<1>(hi), std::get<2>(hi)};
                 c.fit_bounds(lo_f, hi_f);
             },
             nb::arg("bounds_min"), nb::arg("bounds_max"));
}

}  // namespace tcplot_bindings
