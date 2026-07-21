// render_pipeline_bindings.cpp - nanobind bindings for RenderPipeline
#include <nanobind/nanobind.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/vector.h>
#include <nanobind/stl/optional.h>
#include <nanobind/stl/pair.h>
#include <nanobind/stl/tuple.h>
#include <nanobind/stl/array.h>
#include <tcbase/tc_log.hpp>
#include <stdexcept>

extern "C" {
#include "render/tc_pass.h"
#include "render/tc_pipeline_pool.h"
}

#include "termin/render/render_pipeline.hpp"
#include "termin/render/builtin_passes.hpp"
#include "termin/render/tc_pass.hpp"
#include "termin/render/graph_compiler.hpp"
#include "termin/render/fbo_pool.hpp"
#include "termin/render/unknown_pass.hpp"
#include "termin/bindings/tc_value_helpers.hpp"
#include "unknown_pass_serialization.hpp"
#include "tgfx2/i_render_device.hpp"
#include "tgfx2/pixel_format_utils.hpp"

namespace nb = nanobind;

namespace termin {

namespace {

struct RenderPipelineCandidateDeleter {
    void operator()(RenderPipeline* pipeline) const {
        if (!pipeline) return;
        pipeline->destroy();
        delete pipeline;
    }
};

void python_pipeline_pass_deleter(tc_pass* pass) {
    if (!pass || !pass->body) return;
    PyObject* body = reinterpret_cast<PyObject*>(pass->body);
    nb::gil_scoped_acquire gil;
    Py_DECREF(body);
}

void adopt_python_pass(
    tc_pipeline_handle pipeline,
    tc_pass* pass,
    nb::handle owner,
    tc_pass* before = nullptr
) {
    if (!pass || owner.is_none()) {
        throw std::runtime_error("cannot adopt an invalid Python pass");
    }
    Py_INCREF(owner.ptr());
    const bool adopted = before
        ? tc_pipeline_adopt_pass_before(
            pipeline, pass, &python_pipeline_pass_deleter, before)
        : tc_pipeline_adopt_pass(
            pipeline, pass, &python_pipeline_pass_deleter);
    if (!adopted) {
        Py_DECREF(owner.ptr());
        throw std::runtime_error("failed to adopt Python pass into pipeline");
    }
}

TcPassRef tc_pass_ref_from_python_pass(nb::object pass_obj, const char* context) {
    nb::object tc_pass_obj = pass_obj.attr("_tc_pass");
    if (tc_pass_obj.is_none()) {
        throw std::runtime_error(std::string(context) + ": _tc_pass is None");
    }
    if (!nb::isinstance<TcPassRef>(tc_pass_obj)) {
        throw std::runtime_error(std::string(context) + ": _tc_pass must be TcPassRef");
    }
    return nb::cast<TcPassRef>(tc_pass_obj);
}

tc_pass* tc_pass_ptr_from_python_pass(nb::object pass_obj, const char* context) {
    TcPassRef ref = tc_pass_ref_from_python_pass(pass_obj, context);
    if (!ref.valid()) {
        throw std::runtime_error(std::string(context) + ": _tc_pass is invalid");
    }
    return ref.ptr();
}

tc_pass* make_unknown_pass_from_serialized(const std::string& original_type, nb::dict data) {
    if (!tc_pass_registry_has("UnknownPass")) register_builtin_render_pass_types();
    tc_pass* raw = tc_pass_registry_create("UnknownPass");
    if (!raw) throw std::runtime_error("failed to create UnknownPass");
    auto* unknown = dynamic_cast<UnknownPass*>(CxxFramePass::from_tc(raw));
    if (!unknown) {
        tc_pass_delete_unowned(raw);
        throw std::runtime_error("UnknownPass registry returned incompatible object");
    }

    unknown->original_type = original_type;
    tc_value_free(&unknown->original_data);
    unknown->original_data = data.contains("data")
        ? py_to_tc_value(data["data"])
        : tc_value_dict_new();
    if (data.contains("pass_name")) tc_pass_set_name(raw, nb::cast<std::string>(data["pass_name"]).c_str());
    if (data.contains("enabled")) raw->enabled = nb::cast<bool>(data["enabled"]);
    if (data.contains("passthrough")) raw->passthrough = nb::cast<bool>(data["passthrough"]);
    if (data.contains("viewport_name") && !data["viewport_name"].is_none()) {
        tc_pass_set_viewport_name(raw, nb::cast<std::string>(data["viewport_name"]).c_str());
    }

    if (data.contains("_unknown_graph")) {
        nb::dict graph = nb::cast<nb::dict>(data["_unknown_graph"]);
        if (graph.contains("reads")) unknown->original_reads = nb::cast<std::vector<std::string>>(graph["reads"]);
        if (graph.contains("writes")) unknown->original_writes = nb::cast<std::vector<std::string>>(graph["writes"]);
        if (graph.contains("inplace_aliases")) {
            unknown->original_inplace_aliases =
                nb::cast<std::vector<std::pair<std::string, std::string>>>(graph["inplace_aliases"]);
        }
        if (graph.contains("internal_symbols")) {
            unknown->original_internal_symbols =
                nb::cast<std::vector<std::string>>(graph["internal_symbols"]);
        }
        if (graph.contains("resource_specs")) {
            for (nb::handle value : nb::cast<nb::list>(graph["resource_specs"])) {
                nb::dict item = nb::cast<nb::dict>(value);
                ResourceSpec spec;
                if (item.contains("resource")) spec.resource = nb::cast<std::string>(item["resource"]);
                if (item.contains("resource_type")) spec.resource_type = nb::cast<std::string>(item["resource_type"]);
                if (item.contains("size")) spec.size = nb::cast<std::pair<int, int>>(item["size"]);
                if (item.contains("clear_color")) spec.clear_color = nb::cast<std::array<double, 4>>(item["clear_color"]);
                if (item.contains("clear_depth")) spec.clear_depth = nb::cast<float>(item["clear_depth"]);
                if (item.contains("format")) spec.format = nb::cast<std::string>(item["format"]);
                if (item.contains("samples")) spec.samples = nb::cast<int>(item["samples"]);
                if (item.contains("viewport_name")) spec.viewport_name = nb::cast<std::string>(item["viewport_name"]);
                if (item.contains("scale")) spec.scale = nb::cast<float>(item["scale"]);
                if (item.contains("filter")) spec.filter = static_cast<TextureFilter>(nb::cast<int>(item["filter"]));
                unknown->original_resource_specs.push_back(std::move(spec));
            }
        }
    }
    return raw;
}

std::string resource_type_for_texture(const RenderPipeline& pipeline, const PipelineTextureEntry& entry) {
    for (const auto& spec : pipeline.collect_specs()) {
        if (spec.resource == entry.key && !spec.resource_type.empty()) {
            return spec.resource_type;
        }
    }
    return tgfx::is_depth_format(entry.desc.format) ? "depth_texture" : "color_texture";
}

nb::object fbo_info(RenderPipeline& self, const std::string& key) {
    const FBOPool& pool = self.fbo_pool();
    auto it = pool.alias_to_canonical.find(key);
    const std::string& resolved = (it != pool.alias_to_canonical.end()) ? it->second : key;
    for (const auto& entry : pool.entries) {
        if (entry.key != resolved) {
            continue;
        }
        nb::dict d;
        d["key"] = entry.key;
        d["width"] = entry.desc.width;
        d["height"] = entry.desc.height;
        d["samples"] = entry.desc.samples;
        d["has_depth"] = entry.desc.has_depth;
        d["resource_type"] = "fbo";
        d["color_format"] = static_cast<int>(entry.desc.color_format);
        d["depth_format"] = static_cast<int>(entry.desc.depth_format);
        d["color_texture_handle"] = entry.color_tgfx2.id;
        d["depth_texture_handle"] = entry.depth_tgfx2.id;
        return d;
    }
    return nb::none();
}

} // namespace

void bind_render_pipeline(nb::module_& m) {
    nb::class_<TcPipelineTemplate>(m, "TcPipelineTemplate")
        .def_static("declare", &TcPipelineTemplate::declare, nb::arg("uuid"), nb::arg("name"))
        .def_static("find", &TcPipelineTemplate::find, nb::arg("uuid"))
        .def_prop_ro("is_valid", &TcPipelineTemplate::is_valid)
        .def_prop_ro("uuid", [](const TcPipelineTemplate& value) {
            return std::string(value.uuid());
        })
        .def_prop_ro("name", [](const TcPipelineTemplate& value) {
            return std::string(value.name());
        })
        .def_prop_ro("version", &TcPipelineTemplate::version)
        .def("serialize", [](const TcPipelineTemplate& value) {
            const tc_pipeline_template* pipeline_template = value.get();
            if (!pipeline_template) {
                throw std::runtime_error("cannot serialize an invalid pipeline template");
            }
            const size_t size = tc_pipeline_template_serialize(
                pipeline_template, nullptr, 0);
            if (size == 0) {
                throw std::runtime_error("failed to size pipeline template descriptor");
            }
            std::vector<uint8_t> bytes(size);
            if (tc_pipeline_template_serialize(
                    pipeline_template, bytes.data(), bytes.size()) != size) {
                throw std::runtime_error("failed to serialize pipeline template descriptor");
            }
            return nb::bytes(
                reinterpret_cast<const char*>(bytes.data()), bytes.size());
        })
        .def_prop_ro("passes", [](const TcPipelineTemplate& value) {
            nb::list result;
            const tc_pipeline_template* pipeline = value.get();
            if (!pipeline) return result;
            for (uint32_t i = 0; i < pipeline->pass_count; ++i) {
                nb::dict item;
                item["type"] = pipeline->passes[i].type_name;
                item["name"] = pipeline->passes[i].name;
                item["parameters"] = pipeline->passes[i].parameters
                    ? nb::cast(pipeline->passes[i].parameters) : nb::none();
                item["viewport_name"] = pipeline->passes[i].viewport_name
                    ? nb::cast(pipeline->passes[i].viewport_name) : nb::none();
                result.append(std::move(item));
            }
            return result;
        })
        .def_prop_ro("resources", [](const TcPipelineTemplate& value) {
            nb::list result;
            const tc_pipeline_template* pipeline = value.get();
            if (!pipeline) return result;
            for (uint32_t i = 0; i < pipeline->resource_count; ++i) {
                const tc_pipeline_template_resource_desc& resource = pipeline->resources[i];
                nb::dict item;
                item["name"] = resource.name;
                item["resource_type"] = resource.resource_type;
                item["format"] = resource.format ? nb::cast(resource.format) : nb::none();
                item["viewport_name"] = resource.viewport_name
                    ? nb::cast(resource.viewport_name) : nb::none();
                item["width"] = resource.width;
                item["height"] = resource.height;
                item["scale"] = resource.scale;
                item["samples"] = resource.samples;
                result.append(std::move(item));
            }
            return result;
        })
        .def_prop_ro("dependencies", [](const TcPipelineTemplate& value) {
            nb::list result;
            const tc_pipeline_template* pipeline = value.get();
            if (!pipeline) return result;
            for (uint32_t i = 0; i < pipeline->dependency_count; ++i) {
                nb::dict item;
                item["pass_index"] = pipeline->dependencies[i].pass_index;
                item["resource"] = pipeline->dependencies[i].resource;
                item["access"] = static_cast<uint32_t>(pipeline->dependencies[i].access);
                result.append(std::move(item));
            }
            return result;
        })
        .def_prop_ro("targets", [](const TcPipelineTemplate& value) {
            nb::list result;
            const tc_pipeline_template* pipeline = value.get();
            if (!pipeline) return result;
            for (uint32_t i = 0; i < pipeline->target_count; ++i) {
                nb::dict item;
                item["viewport_name"] = pipeline->targets[i].viewport_name
                    ? nb::cast(pipeline->targets[i].viewport_name) : nb::none();
                item["export_name"] = pipeline->targets[i].export_name
                    ? nb::cast(pipeline->targets[i].export_name) : nb::none();
                item["width"] = pipeline->targets[i].width;
                item["height"] = pipeline->targets[i].height;
                result.append(std::move(item));
            }
            return result;
        })
        .def_prop_ro("resource_views", [](const TcPipelineTemplate& value) {
            nb::dict result;
            const tc_pipeline_template* pipeline = value.get();
            if (!pipeline) return result;
            for (uint32_t i = 0; i < pipeline->resource_view_count; ++i) {
                const tc_pipeline_template_resource_view_desc& view =
                    pipeline->resource_views[i];
                nb::dict item;
                item["parent"] = view.parent;
                item["attachment"] = view.attachment == TC_PIPELINE_ATTACHMENT_DEPTH
                    ? "depth" : "color";
                result[view.name] = std::move(item);
            }
            return result;
        })
        .def_prop_ro("fbo_compositions", [](const TcPipelineTemplate& value) {
            nb::dict result;
            const tc_pipeline_template* pipeline = value.get();
            if (!pipeline) return result;
            for (uint32_t i = 0; i < pipeline->fbo_composition_count; ++i) {
                const tc_pipeline_template_fbo_composition_desc& composition =
                    pipeline->fbo_compositions[i];
                nb::dict item;
                item["color"] = composition.color ? composition.color : "";
                item["depth"] = composition.depth ? composition.depth : "";
                result[composition.name] = std::move(item);
            }
            return result;
        });

    nb::class_<RenderPipeline>(m, "RenderPipeline")
        .def(nb::init<const std::string&>(), nb::arg("name") = "default")
        .def(nb::init<const TcPipelineTemplate&>(), nb::arg("pipeline_template"))

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
                adopt_python_pass(
                    self->handle(),
                    tc_pass_ptr_from_python_pass(pass_obj, "RenderPipeline.__init__"),
                    pass_obj
                );
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

        .def_prop_ro("pipeline_template", [](RenderPipeline& self) {
            return TcPipelineTemplate(self.template_handle());
        })

        .def_prop_ro("pass_count", &RenderPipeline::pass_count)

        .def("add_pass", [](RenderPipeline& self, TcPassRef pass_ref) {
            if (!pass_ref.valid()) return;
            tc_pass* pass = pass_ref.ptr();
            if (pass->native_language == TC_LANGUAGE_PYTHON && pass->body) {
                adopt_python_pass(
                    self.handle(),
                    pass,
                    nb::handle(reinterpret_cast<PyObject*>(pass->body))
                );
            } else {
                self.add_pass(pass);
            }
        })
        .def("add_pass", [](RenderPipeline& self, TcPass* pass) {
            if (pass && pass->ptr()) {
                tc_pass* raw = pass->ptr();
                adopt_python_pass(
                    self.handle(),
                    raw,
                    nb::handle(reinterpret_cast<PyObject*>(raw->body))
                );
            }
        })
        .def("add_pass", [](RenderPipeline& self, nb::object pass_obj) {
            adopt_python_pass(
                self.handle(),
                tc_pass_ptr_from_python_pass(pass_obj, "RenderPipeline.add_pass"),
                pass_obj
            );
        })
        .def("remove_pass", [](RenderPipeline& self, TcPassRef pass_ref) {
            if (pass_ref.valid()) self.remove_pass(pass_ref.ptr());
        })
        .def("remove_pass", [](RenderPipeline& self, nb::object pass_obj) {
            self.remove_pass(tc_pass_ptr_from_python_pass(pass_obj, "RenderPipeline.remove_pass"));
        })
        .def("remove_passes_by_name", &RenderPipeline::remove_passes_by_name, nb::arg("name"))
        .def("insert_pass_before", [](RenderPipeline& self, TcPassRef pass_ref, TcPassRef before_ref) {
            if (pass_ref.valid()) {
                tc_pass* pass = pass_ref.ptr();
                tc_pass* before = before_ref.valid() ? before_ref.ptr() : nullptr;
                if (pass->native_language == TC_LANGUAGE_PYTHON && pass->body) {
                    adopt_python_pass(
                        self.handle(),
                        pass,
                        nb::handle(reinterpret_cast<PyObject*>(pass->body)),
                        before
                    );
                } else {
                    self.insert_pass_before(pass, before);
                }
            }
        })
        .def("insert_pass_before", [](RenderPipeline& self, nb::object pass_obj, nb::object before_obj) {
            tc_pass* pass_ptr = tc_pass_ptr_from_python_pass(pass_obj, "RenderPipeline.insert_pass_before(pass)");
            tc_pass* before_ptr = nullptr;
            if (!before_obj.is_none()) {
                before_ptr = tc_pass_ptr_from_python_pass(before_obj, "RenderPipeline.insert_pass_before(before)");
            }
            adopt_python_pass(self.handle(), pass_ptr, pass_obj, before_ptr);
        })
        .def("move_pass_before", [](RenderPipeline& self, nb::object pass_obj, nb::object before_obj) {
            tc_pass* pass_ptr = tc_pass_ptr_from_python_pass(
                pass_obj, "RenderPipeline.move_pass_before(pass)");
            tc_pass* before_ptr = nullptr;
            if (!before_obj.is_none()) {
                before_ptr = tc_pass_ptr_from_python_pass(
                    before_obj, "RenderPipeline.move_pass_before(before)");
            }
            if (!self.move_pass_before(pass_ptr, before_ptr)) {
                throw std::runtime_error("failed to move pass inside pipeline");
            }
        }, nb::arg("pass_obj"), nb::arg("before_obj") = nb::none())
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

        .def("destroy", &RenderPipeline::destroy)

        .def("get_fbo_keys", [](RenderPipeline& self) { return self.fbo_pool().keys(); })
        .def("clear_fbo_pool", [](RenderPipeline& self) { self.fbo_pool().clear(); })

        // Returns a dict describing the named FBOPool entry, or None
        // when the key is unknown. Resolves aliases via get_color_tgfx2.
        // Framegraph debugger consumes width/height to show per-resource
        // info; format / has_depth / native handles are exposed for
        // future preview hooks.
        .def("get_fbo", [](RenderPipeline& self, const std::string& key) -> nb::object {
            return fbo_info(self, key);
        })

        .def("get_resource_info", [](RenderPipeline& self, const std::string& key) -> nb::object {
            nb::object fbo = fbo_info(self, key);
            if (!fbo.is_none()) {
                return fbo;
            }

            PipelineRenderCache& cache = self.cache();
            auto alias_it = cache.texture_alias_to_canonical.find(key);
            const std::string& resolved =
                (alias_it != cache.texture_alias_to_canonical.end()) ? alias_it->second : key;
            for (const auto& entry : cache.texture_pool.entries) {
                if (entry.key != resolved) {
                    continue;
                }
                nb::dict d;
                d["key"] = key;
                d["canonical"] = entry.key;
                d["width"] = entry.desc.width;
                d["height"] = entry.desc.height;
                d["samples"] = 1;
                d["has_depth"] = false;
                d["resource_type"] = resource_type_for_texture(self, entry);
                d["color_format_name"] = std::string(tgfx::pixel_format_name(entry.desc.format));
                d["color_texture_handle"] = entry.handle.id;
                d["depth_texture_handle"] = 0;
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
                if (std::string(tc_pass_type_name(p)) == "UnknownPass") {
                    passes_list.append(serialize_unknown_pass_envelope(p));
                    continue;
                }
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
            nb::dict views;
            for (const auto& [name, view] : self.cache().resource_views) {
                nb::dict item;
                item["parent"] = view.parent;
                item["attachment"] = view.attachment == AttachmentKind::Depth ? "depth" : "color";
                views[name.c_str()] = std::move(item);
            }
            result["resource_views"] = std::move(views);
            nb::dict compositions;
            for (const auto& [name, composition] : self.cache().fbo_compositions) {
                nb::dict item;
                item["color"] = composition.color;
                item["depth"] = composition.depth;
                compositions[name.c_str()] = std::move(item);
            }
            result["fbo_compositions"] = std::move(compositions);
            return result;
        })

        .def_static("deserialize", [](nb::dict data, nb::object resource_manager) -> RenderPipeline* {
            nb::module_ pass_module = nb::module_::import_("termin.render_framework.python_pass");
            nb::object deserialize_pass = pass_module.attr("deserialize_pass");
            std::string name = "default";
            if (data.contains("name")) name = nb::cast<std::string>(data["name"]);
            std::unique_ptr<RenderPipeline, RenderPipelineCandidateDeleter> pipeline(
                new RenderPipeline(name));
            if (data.contains("passes")) {
                nb::list passes = nb::cast<nb::list>(data["passes"]);
                for (size_t i = 0; i < nb::len(passes); i++) {
                    try {
                        nb::dict pass_data = nb::cast<nb::dict>(passes[i]);
                        std::string pass_type;
                        if (pass_data.contains("type")) {
                            pass_type = nb::cast<std::string>(pass_data["type"]);
                        }
                        if (!pass_type.empty() &&
                            tc_pass_registry_has(pass_type.c_str()) &&
                            tc_pass_registry_get_kind(pass_type.c_str()) == TC_NATIVE_PASS) {
                            tc_pass* native_pass = tc_pass_registry_create(pass_type.c_str());
                            if (!native_pass) {
                                throw std::runtime_error("failed to create native pass '" + pass_type + "'");
                            }

                            TcPassRef native_ref(native_pass);
                            if (pass_data.contains("pass_name")) {
                                native_ref.set_pass_name(nb::cast<std::string>(pass_data["pass_name"]));
                            }
                            if (pass_data.contains("enabled")) {
                                native_ref.set_enabled(nb::cast<bool>(pass_data["enabled"]));
                            }
                            if (pass_data.contains("passthrough")) {
                                native_ref.set_passthrough(nb::cast<bool>(pass_data["passthrough"]));
                            }
                            if (pass_data.contains("viewport_name") && !pass_data["viewport_name"].is_none()) {
                                native_ref.set_viewport_name(nb::cast<std::string>(pass_data["viewport_name"]));
                            }
                            if (pass_data.contains("data")) {
                                nb::object native_ref_obj = nb::cast(native_ref);
                                native_ref_obj.attr("deserialize_data")(pass_data["data"]);
                            }
                            if (!tc_pipeline_adopt_pass(
                                    pipeline->handle(), native_pass, native_pass->deleter)) {
                                tc_pass_delete_unowned(native_pass);
                                throw std::runtime_error("failed to adopt native pass '" + pass_type + "'");
                            }
                            continue;
                        }

                        if (!pass_type.empty() && !tc_pass_registry_has(pass_type.c_str())) {
                            tc_pass* unknown = make_unknown_pass_from_serialized(pass_type, pass_data);
                            if (!tc_pipeline_adopt_pass(
                                    pipeline->handle(), unknown, unknown->deleter)) {
                                tc_pass_delete_unowned(unknown);
                                throw std::runtime_error("failed to adopt UnknownPass");
                            }
                            continue;
                        }

                        nb::object frame_pass = deserialize_pass(pass_data, resource_manager);
                        if (!frame_pass.is_none()) {
                            adopt_python_pass(
                                pipeline->handle(),
                                tc_pass_ptr_from_python_pass(
                                    frame_pass, "RenderPipeline.deserialize"),
                                frame_pass
                            );
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
            if (data.contains("resource_views")) {
                nb::dict values = nb::cast<nb::dict>(data["resource_views"]);
                for (auto [key, value] : values) {
                    nb::dict item = nb::cast<nb::dict>(value);
                    if (!item.contains("parent") || !item.contains("attachment")) {
                        throw std::runtime_error(
                            "resource view requires 'parent' and 'attachment'");
                    }
                    ResourceView view;
                    view.parent = nb::cast<std::string>(item["parent"]);
                    const std::string attachment =
                        nb::cast<std::string>(item["attachment"]);
                    if (view.parent.empty()) {
                        throw std::runtime_error("resource view parent must not be empty");
                    }
                    if (attachment == "depth") {
                        view.attachment = AttachmentKind::Depth;
                    } else if (attachment != "color") {
                        throw std::runtime_error(
                            "resource view attachment must be 'color' or 'depth'");
                    }
                    pipeline->cache().resource_views[nb::cast<std::string>(key)] = std::move(view);
                }
            }
            if (data.contains("fbo_compositions")) {
                nb::dict values = nb::cast<nb::dict>(data["fbo_compositions"]);
                for (auto [key, value] : values) {
                    nb::dict item = nb::cast<nb::dict>(value);
                    FboComposition composition;
                    if (item.contains("color")) {
                        composition.color = nb::cast<std::string>(item["color"]);
                    }
                    if (item.contains("depth")) {
                        composition.depth = nb::cast<std::string>(item["depth"]);
                    }
                    if (composition.color.empty() && composition.depth.empty()) {
                        throw std::runtime_error(
                            "FBO composition requires a color or depth attachment");
                    }
                    pipeline->cache().fbo_compositions[nb::cast<std::string>(key)] =
                        std::move(composition);
                }
            }
            return pipeline.release();
        }, nb::rv_policy::take_ownership)

        .def("copy", [](RenderPipeline& self, nb::object resource_manager) -> nb::object {
            nb::dict data = nb::cast<nb::dict>(nb::cast(&self).attr("serialize")());
            nb::module_ render_module = nb::module_::import_("termin.render_framework");
            nb::object RenderPipelineClass = render_module.attr("RenderPipeline");
            return RenderPipelineClass.attr("deserialize")(data, resource_manager);
        }, nb::arg("resource_manager"));

    m.attr("RenderPipelineInstance") = m.attr("RenderPipeline");

    m.def("compile_graph_from_json", [](const std::string& json_str,
                                         const std::string& template_uuid,
                                         const std::string& template_name) {
        if (template_uuid.empty()) {
            return tc::compile_graph(json_str);
        }
        const std::string& name = template_name.empty() ? template_uuid : template_name;
        TcPipelineTemplate pipeline_template = TcPipelineTemplate::declare(template_uuid, name);
        if (!pipeline_template.is_valid()) {
            throw std::runtime_error("failed to declare canonical pipeline template");
        }
        return tc::compile_graph(json_str, pipeline_template);
    }, nb::arg("json_str"), nb::arg("template_uuid") = "",
       nb::arg("template_name") = "", nb::rv_policy::take_ownership);

    m.def("publish_pipeline_template", [](
        RenderPipeline& pipeline,
        const TcPipelineTemplate& pipeline_template,
        const std::vector<std::string>& pass_parameters,
        nb::list target_values
    ) {
        std::vector<tc::PipelineTemplateTarget> targets;
        targets.reserve(nb::len(target_values));
        for (nb::handle value : target_values) {
            nb::dict item = nb::cast<nb::dict>(value);
            tc::PipelineTemplateTarget target;
            if (item.contains("viewport_name")) {
                target.viewport_name = nb::cast<std::string>(item["viewport_name"]);
            }
            if (item.contains("export_name")) {
                target.export_name = nb::cast<std::string>(item["export_name"]);
            }
            if (item.contains("width")) target.width = nb::cast<int32_t>(item["width"]);
            if (item.contains("height")) target.height = nb::cast<int32_t>(item["height"]);
            targets.push_back(std::move(target));
        }
        if (!tc::publish_pipeline_template(
                pipeline, pipeline_template, pass_parameters, targets)) {
            throw std::runtime_error("failed to publish canonical pipeline template");
        }
    }, nb::arg("pipeline"), nb::arg("pipeline_template"),
       nb::arg("pass_parameters") = std::vector<std::string>{},
       nb::arg("targets") = nb::list());

}

} // namespace termin
