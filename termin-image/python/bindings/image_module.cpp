#include <Python.h>
#include <nanobind/nanobind.h>
#include <nanobind/stl/string.h>

#include <cstdint>
#include <span>
#include <stdexcept>
#include <vector>

#include "termin/image/image_decode.hpp"

namespace nb = nanobind;

namespace {

std::span<const std::uint8_t> bytes_span(PyObject* object, Py_buffer* view) {
    if (PyObject_GetBuffer(object, view, PyBUF_CONTIG_RO) != 0) {
        throw nb::python_error();
    }
    return {
        static_cast<const std::uint8_t*>(view->buf),
        static_cast<std::size_t>(view->len),
    };
}

nb::dict decode_rgba8(nb::object data, const std::string& source_hint) {
    Py_buffer view;
    std::span<const std::uint8_t> input = bytes_span(data.ptr(), &view);
    try {
        termin::image::DecodedImage decoded = termin::image::decode_rgba8(input, source_hint);
        PyBuffer_Release(&view);

        nb::dict result;
        result["width"] = decoded.width;
        result["height"] = decoded.height;
        result["channels"] = decoded.channels;
        result["format"] = decoded.format;
        result["data"] = nb::bytes(
            reinterpret_cast<const char*>(decoded.pixels.data()),
            decoded.pixels.size()
        );
        return result;
    } catch (...) {
        PyBuffer_Release(&view);
        throw;
    }
}

nb::bytes encode_png_rgba8(nb::object data, int width, int height) {
    Py_buffer view;
    std::span<const std::uint8_t> input = bytes_span(data.ptr(), &view);
    try {
        std::vector<std::uint8_t> encoded = termin::image::encode_png_rgba8(input, width, height);
        PyBuffer_Release(&view);
        return nb::bytes(reinterpret_cast<const char*>(encoded.data()), encoded.size());
    } catch (...) {
        PyBuffer_Release(&view);
        throw;
    }
}

} // namespace

NB_MODULE(_image_native, m) {
    m.doc() = "termin-image native image codec bindings";
    m.def("decode_rgba8", &decode_rgba8, nb::arg("data"), nb::arg("source_hint") = "");
    m.def("encode_png_rgba8", &encode_png_rgba8, nb::arg("data"), nb::arg("width"), nb::arg("height"));
}
