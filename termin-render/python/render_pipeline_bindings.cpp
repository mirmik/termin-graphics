// render_pipeline_bindings.cpp - nanobind bindings for RenderPipeline
#include <nanobind/nanobind.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/vector.h>
#include <nanobind/stl/optional.h>
#include <nanobind/stl/pair.h>
#include <nanobind/stl/tuple.h>
#include <nanobind/stl/array.h>
#include <tcbase/tc_log.hpp>

extern "C" {
#include "render/tc_pipeline_pool.h"
}

#include "termin/render/render_pipeline.hpp"
#include "termin/render/tc_pass.hpp"
#include "termin/render/graph_compiler.hpp"
#include "termin/render/fbo_pool.hpp"
#include "tgfx2/i_render_device.hpp"

namespace nb = nanobind;

namespace termin {

void bind_render_pipeline(nb::module_& m) {
    nb::class_<RenderPipeline>(m, "RenderPipeline")
        .def(nb::init<const std::string&>(), nb::arg("name") = "default")

        .def_static("from_handle", [](uint32_t index, uint32_t generation) {
            tc_pipeline_handle h;
            h.index = index;
            h.generation = generation;
            return RenderPipeline(h);
        }, nb::arg("index"), nb::arg("generation"))

        // Constructor with passes and specs
        .def("__init__", [](RenderPipeline* self,
                           const std::string& name,
                           nb::list init_passes,
                           const std::vector<ResourceSpec>& init_specs) {
            new (self) RenderPipeline(name);
            for (size_t i = 0; i < nb::len(init_passes); i++) {
                nb::object pass_obj = init_passes[i];
                if (nb::hasattr(pass_obj, "_tc_pass")) {
                    nb::object tc_pass_obj = pass_obj.attr("_tc_pass");
                    if (!tc_pass_obj.is_none()) {
                        if (nb::isinstance<TcPassRef>(tc_pass_obj)) {
                            TcPassRef ref = nb::cast<TcPassRef>(tc_pass_obj);
                            if (ref.valid()) {
                                self->add_pass(ref.ptr());
                            }
                        } else if (nb::isinstance<TcPass>(tc_pass_obj)) {
                            TcPass* pass = nb::cast<TcPass*>(tc_pass_obj);
                            if (pass && pass->ptr()) {
                                self->add_pass(pass->ptr());
                            }
                        }
                    }
                }
            }
            for (const auto& spec : init_specs) {
                self->add_spec(spec);
            }
        }, nb::arg("name") = "default",
           nb::arg("_init_passes") = nb::list(),
           nb::arg("_init_specs") = std::vector<ResourceSpec>{})

        .def_prop_rw("name", &RenderPipeline::name, &RenderPipeline::set_name)

        .def_prop_ro("_tc_pipeline", [](RenderPipeline& self) {
            return self.ptr();
        }, nb::rv_policy::reference)

        .def_prop_ro("_pipeline_handle", [](RenderPipeline& self) -> tc_pipeline_handle {
            return self.handle();
        })

        .def_prop_ro("pass_count", &RenderPipeline::pass_count)

        .def("add_pass", [](RenderPipeline& self, TcPassRef pass_ref) {
            if (pass_ref.valid()) self.add_pass(pass_ref.ptr());
        })
        .def("add_pass", [](RenderPipeline& self, TcPass* pass) {
            if (pass && pass->ptr()) self.add_pass(pass->ptr());
        })
        .def("add_pass", [](RenderPipeline& self, nb::object pass_obj) {
            if (nb::hasattr(pass_obj, "_tc_pass")) {
                nb::object tc_pass_obj = pass_obj.attr("_tc_pass");
                if (!tc_pass_obj.is_none()) {
                    if (nb::isinstance<TcPassRef>(tc_pass_obj)) {
                        TcPassRef ref = nb::cast<TcPassRef>(tc_pass_obj);
                        if (ref.valid()) self.add_pass(ref.ptr());
                    } else if (nb::isinstance<TcPass>(tc_pass_obj)) {
                        TcPass* pass = nb::cast<TcPass*>(tc_pass_obj);
                        if (pass && pass->ptr()) self.add_pass(pass->ptr());
                    }
                }
            }
        })
        .def("remove_pass", [](RenderPipeline& self, TcPassRef pass_ref) {
            if (pass_ref.valid()) self.remove_pass(pass_ref.ptr());
        })
        .def("remove_pass", [](RenderPipeline& self, nb::object pass_obj) {
            if (nb::hasattr(pass_obj, "_tc_pass")) {
                nb::object tc_pass_obj = pass_obj.attr("_tc_pass");
                if (!tc_pass_obj.is_none() && nb::isinstance<TcPassRef>(tc_pass_obj)) {
                    TcPassRef ref = nb::cast<TcPassRef>(tc_pass_obj);
                    if (ref.valid()) self.remove_pass(ref.ptr());
                }
            }
        })
        .def("remove_passes_by_name", &RenderPipeline::remove_passes_by_name, nb::arg("name"))
        .def("insert_pass_before", [](RenderPipeline& self, TcPassRef pass_ref, TcPassRef before_ref) {
            if (pass_ref.valid()) {
                self.insert_pass_before(pass_ref.ptr(), before_ref.valid() ? before_ref.ptr() : nullptr);
            }
        })
        .def("insert_pass_before", [](RenderPipeline& self, nb::object pass_obj, nb::object before_obj) {
            tc_pass* pass_ptr = nullptr;
            tc_pass* before_ptr = nullptr;
            if (nb::hasattr(pass_obj, "_tc_pass")) {
                nb::object tc_pass_obj = pass_obj.attr("_tc_pass");
                if (!tc_pass_obj.is_none() && nb::isinstance<TcPassRef>(tc_pass_obj)) {
                    TcPassRef ref = nb::cast<TcPassRef>(tc_pass_obj);
                    if (ref.valid()) pass_ptr = ref.ptr();
                }
            }
            if (!before_obj.is_none() && nb::hasattr(before_obj, "_tc_pass")) {
                nb::object tc_pass_obj = before_obj.attr("_tc_pass");
                if (!tc_pass_obj.is_none() && nb::isinstance<TcPassRef>(tc_pass_obj)) {
                    TcPassRef ref = nb::cast<TcPassRef>(tc_pass_obj);
                    if (ref.valid()) before_ptr = ref.ptr();
                }
            }
            if (pass_ptr) self.insert_pass_before(pass_ptr, before_ptr);
        })
        .def("get_pass", [](RenderPipeline& self, const std::string& name) {
            return TcPassRef(self.get_pass(name));
        })
        .def("get_pass_by_name", [](RenderPipeline& self, const std::string& name) {
            return TcPassRef(self.get_pass(name));
        })
        .def("get_pass_at", [](RenderPipeline& self, size_t index) {
            return TcPassRef(self.get_pass_at(index));
        })

        .def_prop_ro("passes", [](RenderPipeline& self) {
            std::vector<TcPassRef> result;
            for (size_t i = 0; i < self.pass_count(); i++) {
                tc_pass* p = self.get_pass_at(i);
                if (p) result.push_back(TcPassRef(p));
            }
            return result;
        })

        .def("add_spec", &RenderPipeline::add_spec)
        .def("clear_specs", &RenderPipeline::clear_specs)
        .def_prop_ro("spec_count", &RenderPipeline::spec_count)
        .def("get_spec_at", [](RenderPipeline& self, size_t index) -> nb::object {
            const ResourceSpec* spec = self.get_spec_at(index);
            if (!spec) return nb::none();
            return nb::cast(*spec);
        })
        .def_prop_ro("pipeline_specs", [](RenderPipeline& self) { return self.specs(); })

        .def("destroy", [](RenderPipeline&) {})

        .def("get_fbo_keys", [](RenderPipeline& self) { return self.fbo_pool().keys(); })
        .def("clear_fbo_pool", [](RenderPipeline& self) { self.fbo_pool().clear(); })

        // Returns a dict describing the named FBOPool entry, or None
        // when the key is unknown. Resolves aliases via get_color_tgfx2.
        // Framegraph debugger consumes width/height to show per-resource
        // info; format / has_depth / native handles are exposed for
        // future preview hooks.
        .def("get_fbo", [](RenderPipeline& self, const std::string& key) -> nb::object {
            const FBOPool& pool = self.fbo_pool();
            auto it = pool.alias_to_canonical.find(key);
            const std::string& resolved = (it != pool.alias_to_canonical.end()) ? it->second : key;
            for (const auto& entry : pool.entries) {
                if (entry.key != resolved) continue;
                nb::dict d;
                d["key"] = entry.key;
                d["width"] = entry.width;
                d["height"] = entry.height;
                d["samples"] = entry.samples;
                d["has_depth"] = entry.has_depth;
                d["color_format"] = static_cast<int>(entry.color_format);
                d["depth_format"] = static_cast<int>(entry.depth_format);
                uintptr_t color_native = 0;
                uintptr_t depth_native = 0;
                if (entry.native_device) {
                    if (entry.color_tgfx2) {
                        color_native = entry.native_device->native_texture_handle(entry.color_tgfx2);
                    }
                    if (entry.has_depth && entry.depth_tgfx2) {
                        depth_native = entry.native_device->native_texture_handle(entry.depth_tgfx2);
                    }
                }
                d["color_native_handle"] = color_native;
                d["depth_native_handle"] = depth_native;
                return d;
            }
            return nb::none();
        })

        .def_prop_ro("is_dirty", &RenderPipeline::is_dirty)
        .def("mark_dirty", &RenderPipeline::mark_dirty)
        .def("__len__", &RenderPipeline::pass_count)

        .def("serialize", [](RenderPipeline& self) -> nb::dict {
            nb::dict result;
            result["name"] = self.name();
            nb::list passes_list;
            for (size_t i = 0; i < self.pass_count(); i++) {
                tc_pass* p = self.get_pass_at(i);
                if (!p) continue;
                TcPassRef ref(p);
                nb::object serialized = nb::cast(ref).attr("serialize")();
                if (!serialized.is_none()) passes_list.append(serialized);
            }
            result["passes"] = passes_list;
            nb::list specs_list;
            for (const auto& spec : self.specs()) {
                nb::dict spec_dict;
                spec_dict["resource"] = spec.resource;
                spec_dict["resource_type"] = spec.resource_type;
                if (spec.size) {
                    nb::list sz; sz.append(spec.size->first); sz.append(spec.size->second);
                    spec_dict["size"] = sz;
                }
                if (spec.clear_color) {
                    const auto& cc = *spec.clear_color;
                    nb::list color; color.append(cc[0]); color.append(cc[1]); color.append(cc[2]); color.append(cc[3]);
                    spec_dict["clear_color"] = color;
                }
                if (spec.clear_depth) spec_dict["clear_depth"] = *spec.clear_depth;
                if (spec.format) spec_dict["format"] = *spec.format;
                if (spec.samples != 1) spec_dict["samples"] = spec.samples;
                specs_list.append(spec_dict);
            }
            result["pipeline_specs"] = specs_list;
            return result;
        })

        .def_static("deserialize", [](nb::dict data, nb::object resource_manager) -> RenderPipeline* {
            nb::module_ core_module = nb::module_::import_("termin.visualization.render.framegraph.core");
            nb::object deserialize_pass = core_module.attr("deserialize_pass");
            std::string name = "default";
            if (data.contains("name")) name = nb::cast<std::string>(data["name"]);
            RenderPipeline* pipeline = new RenderPipeline(name);
            if (data.contains("passes")) {
                nb::list passes = nb::cast<nb::list>(data["passes"]);
                for (size_t i = 0; i < nb::len(passes); i++) {
                    try {
                        nb::dict pass_data = nb::cast<nb::dict>(passes[i]);
                        nb::object frame_pass = deserialize_pass(pass_data, resource_manager);
                        if (!frame_pass.is_none() && nb::hasattr(frame_pass, "_tc_pass")) {
                            nb::object tc_pass_obj = frame_pass.attr("_tc_pass");
                            if (!tc_pass_obj.is_none() && nb::isinstance<TcPassRef>(tc_pass_obj)) {
                                TcPassRef ref = nb::cast<TcPassRef>(tc_pass_obj);
                                if (ref.valid()) pipeline->add_pass(ref.ptr());
                            }
                        }
                    } catch (const std::exception& e) {
                        tc::Log::error("RenderPipeline::deserialize: failed to deserialize pass %zu: %s", i, e.what());
                        throw;
                    }
                }
            }
            if (data.contains("pipeline_specs")) {
                nb::list specs = nb::cast<nb::list>(data["pipeline_specs"]);
                for (size_t i = 0; i < nb::len(specs); i++) {
                    nb::dict spec_data = nb::cast<nb::dict>(specs[i]);
                    ResourceSpec spec;
                    if (spec_data.contains("resource")) spec.resource = nb::cast<std::string>(spec_data["resource"]);
                    if (spec_data.contains("resource_type")) spec.resource_type = nb::cast<std::string>(spec_data["resource_type"]);
                    if (spec_data.contains("size")) {
                        nb::list sz = nb::cast<nb::list>(spec_data["size"]);
                        spec.size = std::make_pair(nb::cast<int>(sz[0]), nb::cast<int>(sz[1]));
                    }
                    if (spec_data.contains("clear_color")) {
                        nb::list cc = nb::cast<nb::list>(spec_data["clear_color"]);
                        spec.clear_color = std::array<double, 4>{nb::cast<double>(cc[0]), nb::cast<double>(cc[1]), nb::cast<double>(cc[2]), nb::cast<double>(cc[3])};
                    }
                    if (spec_data.contains("clear_depth")) spec.clear_depth = nb::cast<float>(spec_data["clear_depth"]);
                    if (spec_data.contains("format")) spec.format = nb::cast<std::string>(spec_data["format"]);
                    if (spec_data.contains("samples")) spec.samples = nb::cast<int>(spec_data["samples"]);
                    pipeline->add_spec(spec);
                }
            }
            return pipeline;
        }, nb::rv_policy::take_ownership)

        .def("copy", [](RenderPipeline& self, nb::object resource_manager) -> nb::object {
            nb::dict data = nb::cast<nb::dict>(nb::cast(&self).attr("serialize")());
            nb::module_ render_module = nb::module_::import_("termin._native.render");
            nb::object RenderPipelineClass = render_module.attr("RenderPipeline");
            return RenderPipelineClass.attr("deserialize")(data, resource_manager);
        }, nb::arg("resource_manager"));

    m.def("compile_graph_from_json", [](const std::string& json_str) {
        return tc::compile_graph(json_str);
    }, nb::arg("json_str"), nb::rv_policy::take_ownership);
}

} // namespace termin
