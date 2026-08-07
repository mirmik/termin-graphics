#include <nanobind/nanobind.h>
#include <nanobind/ndarray.h>
#include <nanobind/stl/optional.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/string_view.h>
#include <nanobind/stl/unordered_map.h>
#include <nanobind/stl/vector.h>
#include <tcbase/tc_log.hpp>
#include <termin/materials/shader_parser.hpp>
#include <termin/materials/surface_contract_registry.hpp>

namespace termin {

    namespace nb = nanobind;

    void bind_tc_material(nb::module_& m);

    void bind_shader_parser(nb::module_& m) {
        // --- MaterialProperty (UniformProperty) ---
        nb::class_<MaterialProperty>(m, "MaterialProperty")
            .def(nb::init<>())
            .def(nb::init<std::string, std::string>(), nb::arg("name"), nb::arg("property_type"))
            // Full constructor with default value and range
            .def(
                "__init__",
                [](MaterialProperty* self,
                   const std::string& name,
                   const std::string& property_type,
                   nb::object default_val,
                   std::optional<double> range_min,
                   std::optional<double> range_max,
                   nb::object expected_encoding) {
                    new (self) MaterialProperty();
                    self->name = name;
                    self->property_type = property_type;
                    self->range_min = range_min;
                    self->range_max = range_max;
                    if (!expected_encoding.is_none()) {
                        const std::string value = nb::cast<std::string>(expected_encoding);
                        if (value == "srgb") {
                            self->expected_texture_encoding = tgfx::TextureEncoding::SRGB;
                        } else if (value == "linear") {
                            self->expected_texture_encoding = tgfx::TextureEncoding::Linear;
                        } else {
                            throw std::runtime_error("expected_encoding must be 'srgb', 'linear', or None");
                        }
                    }

                    // Convert Python default value to C++ variant
                    if (default_val.is_none()) {
                        self->default_value = std::monostate{};
                    } else if (nb::isinstance<nb::bool_>(default_val)) {
                        self->default_value = nb::cast<bool>(default_val);
                    } else if (nb::isinstance<nb::int_>(default_val)) {
                        self->default_value = nb::cast<int>(default_val);
                    } else if (nb::isinstance<nb::float_>(default_val)) {
                        self->default_value = nb::cast<double>(default_val);
                    } else if (nb::isinstance<nb::str>(default_val)) {
                        self->default_value = nb::cast<std::string>(default_val);
                    } else if (nb::isinstance<nb::tuple>(default_val) || nb::isinstance<nb::list>(default_val)) {
                        std::vector<double> vec;
                        for (auto item : default_val) {
                            vec.push_back(nb::cast<double>(item));
                        }
                        self->default_value = vec;
                    }
                },
                nb::arg("name"),
                nb::arg("property_type"),
                nb::arg("default") = nb::none(),
                nb::arg("range_min") = std::nullopt,
                nb::arg("range_max") = std::nullopt,
                nb::arg("expected_encoding") = nb::none())
            .def_rw("name", &MaterialProperty::name)
            .def_rw("property_type", &MaterialProperty::property_type)
            .def_rw("range_min", &MaterialProperty::range_min)
            .def_rw("range_max", &MaterialProperty::range_max)
            .def_rw("label", &MaterialProperty::label)
            .def_prop_rw(
                "expected_encoding",
                [](const MaterialProperty& self) -> nb::object {
                    if (!self.expected_texture_encoding.has_value()) {
                        return nb::none();
                    }
                    return nb::cast(std::string(
                        *self.expected_texture_encoding == tgfx::TextureEncoding::SRGB ? "srgb" : "linear"));
                },
                [](MaterialProperty& self, nb::object value) {
                    if (value.is_none()) {
                        self.expected_texture_encoding = std::nullopt;
                        return;
                    }
                    const std::string encoding = nb::cast<std::string>(value);
                    if (encoding == "srgb") {
                        self.expected_texture_encoding = tgfx::TextureEncoding::SRGB;
                    } else if (encoding == "linear") {
                        self.expected_texture_encoding = tgfx::TextureEncoding::Linear;
                    } else {
                        throw std::runtime_error("expected_encoding must be 'srgb', 'linear', or None");
                    }
                })
            .def_prop_rw(
                "default",
                [](const MaterialProperty& self) -> nb::object {
                    return std::visit(
                        [](auto&& arg) -> nb::object {
                            using T = std::decay_t<decltype(arg)>;
                            if constexpr (std::is_same_v<T, std::monostate>) {
                                return nb::none();
                            } else if constexpr (std::is_same_v<T, std::vector<double>>) {
                                nb::tuple t = nb::steal<nb::tuple>(PyTuple_New(arg.size()));
                                for (size_t i = 0; i < arg.size(); ++i) {
                                    PyTuple_SET_ITEM(t.ptr(), i, nb::cast(arg[i]).release().ptr());
                                }
                                return t;
                            } else {
                                return nb::cast(arg);
                            }
                        },
                        self.default_value);
                },
                [](MaterialProperty& self, nb::object val) {
                    if (val.is_none()) {
                        self.default_value = std::monostate{};
                    } else if (nb::isinstance<nb::bool_>(val)) {
                        self.default_value = nb::cast<bool>(val);
                    } else if (nb::isinstance<nb::int_>(val)) {
                        self.default_value = nb::cast<int>(val);
                    } else if (nb::isinstance<nb::float_>(val)) {
                        self.default_value = nb::cast<double>(val);
                    } else if (nb::isinstance<nb::str>(val)) {
                        self.default_value = nb::cast<std::string>(val);
                    } else if (nb::isinstance<nb::tuple>(val) || nb::isinstance<nb::list>(val)) {
                        std::vector<double> vec;
                        for (auto item : val) {
                            vec.push_back(nb::cast<double>(item));
                        }
                        self.default_value = vec;
                    }
                });

        // Alias for backward compatibility
        m.attr("UniformProperty") = m.attr("MaterialProperty");

        // --- ShaderStage ---
        nb::class_<ShaderStage>(m, "ShaderStage")
            .def(nb::init<>())
            .def(nb::init<std::string, std::string>(), nb::arg("name"), nb::arg("source"))
            .def(
                nb::init<std::string, std::string, std::string>(), nb::arg("name"), nb::arg("source"), nb::arg("entry"))
            .def_rw("name", &ShaderStage::name)
            .def_rw("source", &ShaderStage::source)
            .def_rw("entry", &ShaderStage::entry);

        // Alias for typo compatibility
        m.attr("ShasderStage") = m.attr("ShaderStage");

        // --- PhaseRenderSettings ---
        nb::class_<PhaseRenderSettings>(m, "PhaseRenderSettings")
            .def(nb::init<>())
            .def_rw("gl_depth_mask", &PhaseRenderSettings::gl_depth_mask)
            .def_rw("gl_depth_test", &PhaseRenderSettings::gl_depth_test)
            .def_rw("gl_blend", &PhaseRenderSettings::gl_blend)
            .def_rw("gl_cull", &PhaseRenderSettings::gl_cull)
            .def_rw("priority", &PhaseRenderSettings::priority);

        // --- Material UBO layout (std140 block metadata from shader parser) ---
        nb::class_<MaterialUboEntry>(m, "MaterialUboEntry")
            .def(nb::init<>())
            .def_rw("name", &MaterialUboEntry::name)
            .def_rw("property_type", &MaterialUboEntry::property_type)
            .def_rw("offset", &MaterialUboEntry::offset)
            .def_rw("size", &MaterialUboEntry::size)
            .def("__repr__", [](const MaterialUboEntry& e) {
                return "<MaterialUboEntry " + e.name + ":" + e.property_type + " @" + std::to_string(e.offset) + " +" +
                       std::to_string(e.size) + ">";
            });

        nb::class_<MaterialUboLayout>(m, "MaterialUboLayout")
            .def(nb::init<>())
            .def_rw("entries", &MaterialUboLayout::entries)
            .def_rw("block_size", &MaterialUboLayout::block_size)
            .def("empty", &MaterialUboLayout::empty)
            .def("__repr__", [](const MaterialUboLayout& l) {
                return "<MaterialUboLayout " + std::to_string(l.entries.size()) + " entries, " +
                       std::to_string(l.block_size) + " bytes>";
            });

        nb::class_<SurfaceFragmentInput>(m, "SurfaceFragmentInput")
            .def(nb::init<>())
            .def(nb::init<std::string, std::string>(), nb::arg("semantic"), nb::arg("value_type"))
            .def_rw("semantic", &SurfaceFragmentInput::semantic)
            .def_rw("value_type", &SurfaceFragmentInput::value_type);

        nb::class_<SurfaceProducerResourceDecl>(m, "SurfaceProducerResourceDecl")
            .def(nb::init<>())
            .def_rw("name", &SurfaceProducerResourceDecl::name)
            .def_rw("kind", &SurfaceProducerResourceDecl::kind)
            .def_rw("scope", &SurfaceProducerResourceDecl::scope)
            .def_rw("stage_mask", &SurfaceProducerResourceDecl::stage_mask)
            .def_rw("size", &SurfaceProducerResourceDecl::size);

        nb::class_<MaterialSurfaceProducer>(m, "MaterialSurfaceProducer")
            .def(nb::init<>())
            .def_rw("contract_id", &MaterialSurfaceProducer::contract_id)
            .def_rw("contract_version", &MaterialSurfaceProducer::contract_version)
            .def_rw("surface_type_name", &MaterialSurfaceProducer::surface_type_name)
            .def_rw("evaluator_entry", &MaterialSurfaceProducer::evaluator_entry)
            .def_rw("evaluator_source", &MaterialSurfaceProducer::evaluator_source)
            .def_rw("source_identity", &MaterialSurfaceProducer::source_identity)
            .def_rw("required_fragment_inputs", &MaterialSurfaceProducer::required_fragment_inputs)
            .def_rw("resources", &MaterialSurfaceProducer::resources);

        nb::class_<SurfaceContractKey>(m, "SurfaceContractKey")
            .def(nb::init<>())
            .def(nb::init<std::string, uint32_t>(), nb::arg("id"), nb::arg("version"))
            .def_rw("id", &SurfaceContractKey::id)
            .def_rw("version", &SurfaceContractKey::version);

        nb::class_<SurfaceContractDescriptor>(m, "SurfaceContractDescriptor")
            .def(nb::init<>())
            .def_rw("key", &SurfaceContractDescriptor::key)
            .def_rw("debug_name", &SurfaceContractDescriptor::debug_name)
            .def_rw("surface_type_name", &SurfaceContractDescriptor::surface_type_name)
            .def_rw("interface_source", &SurfaceContractDescriptor::interface_source)
            .def_rw("source_identity", &SurfaceContractDescriptor::source_identity);

        nb::class_<SurfaceContractRegistry>(m, "SurfaceContractRegistry")
            .def_static("register_contract",
                        &SurfaceContractRegistry::register_contract,
                        nb::arg("descriptor"),
                        nb::arg("owner"))
            .def_static("find", &SurfaceContractRegistry::find, nb::arg("key"))
            .def_static("owner_of", &SurfaceContractRegistry::owner_of, nb::arg("key"))
            .def_static(
                "unregister_contract", &SurfaceContractRegistry::unregister_contract, nb::arg("key"), nb::arg("owner"))
            .def_static("unregister_owner", &SurfaceContractRegistry::unregister_owner, nb::arg("owner"))
            .def_static("register_builtins", &SurfaceContractRegistry::register_builtins);

        // --- ShaderPhase ---
        nb::class_<ShaderPhase>(m, "ShaderPhase")
            .def(nb::init<>())
            .def(nb::init<std::string>(), nb::arg("phase_mark"))
            // Full constructor with all parameters
            .def(
                "__init__",
                [](ShaderPhase* self,
                   const std::string& phase_mark,
                   int priority,
                   std::optional<bool> gl_depth_mask,
                   std::optional<bool> gl_depth_test,
                   std::optional<bool> gl_blend,
                   std::optional<bool> gl_cull,
                   const std::unordered_map<std::string, ShaderStage>& stages,
                   const std::vector<MaterialProperty>& uniforms,
                   const std::vector<MaterialProperty>& material_uniforms) {
                    new (self) ShaderPhase();
                    self->phase_mark = phase_mark;
                    self->priority = priority;
                    self->gl_depth_mask = gl_depth_mask;
                    self->gl_depth_test = gl_depth_test;
                    self->gl_blend = gl_blend;
                    self->gl_cull = gl_cull;
                    self->stages = stages;
                    self->uniforms = uniforms;
                    self->material_uniforms = material_uniforms;
                },
                nb::arg("phase_mark"),
                nb::arg("priority") = 0,
                nb::arg("gl_depth_mask") = std::nullopt,
                nb::arg("gl_depth_test") = std::nullopt,
                nb::arg("gl_blend") = std::nullopt,
                nb::arg("gl_cull") = std::nullopt,
                nb::arg("stages") = std::unordered_map<std::string, ShaderStage>{},
                nb::arg("uniforms") = std::vector<MaterialProperty>{},
                nb::arg("material_uniforms") = std::vector<MaterialProperty>{})
            .def_rw("phase_mark", &ShaderPhase::phase_mark)
            .def_rw("available_marks", &ShaderPhase::available_marks)
            .def_rw("priority", &ShaderPhase::priority)
            .def_rw("gl_depth_mask", &ShaderPhase::gl_depth_mask)
            .def_rw("gl_depth_test", &ShaderPhase::gl_depth_test)
            .def_rw("gl_blend", &ShaderPhase::gl_blend)
            .def_rw("gl_cull", &ShaderPhase::gl_cull)
            .def_rw("stages", &ShaderPhase::stages)
            .def_rw("uniforms", &ShaderPhase::uniforms)
            .def_rw("material_uniforms", &ShaderPhase::material_uniforms)
            // std140 material UBO layout computed by the parser. Populated
            // when the phase has @property declarations (and the parser
            // synthesized a MaterialParams block for the phase). Empty
            // layout on raw shaders or system shaders without properties.
            .def_rw("material_ubo_layout", &ShaderPhase::material_ubo_layout)
            .def_rw("material_texture_resources", &ShaderPhase::material_texture_resources)
            .def_rw("uses_engine_per_frame", &ShaderPhase::uses_engine_per_frame)
            .def_rw("uses_engine_draw_data", &ShaderPhase::uses_engine_draw_data)
            .def_rw("surface_producer", &ShaderPhase::surface_producer)
            .def_rw("mark_settings", &ShaderPhase::mark_settings)
            // Backward compatibility: identity transform
            .def_static(
                "from_tree",
                [](const ShaderPhase& phase) { return phase; },
                nb::arg("tree"),
                "Backward compatibility: returns the object as-is");

        // --- ShaderMultyPhaseProgramm ---
        nb::class_<ShaderMultyPhaseProgramm>(m, "ShaderMultyPhaseProgramm")
            .def(nb::init<>())
            .def(nb::init<std::string,
                          std::vector<ShaderPhase>,
                          std::string,
                          std::vector<std::string>,
                          std::vector<MaterialProperty>>(),
                 nb::arg("program"),
                 nb::arg("phases"),
                 nb::arg("source_path") = "",
                 nb::arg("features") = std::vector<std::string>{},
                 nb::arg("material_properties") = std::vector<MaterialProperty>{})
            .def_rw("program", &ShaderMultyPhaseProgramm::program)
            .def_rw("language", &ShaderMultyPhaseProgramm::language)
            .def_rw("phases", &ShaderMultyPhaseProgramm::phases)
            .def_rw("source_path", &ShaderMultyPhaseProgramm::source_path)
            .def_rw("features", &ShaderMultyPhaseProgramm::features)
            .def_rw("material_properties", &ShaderMultyPhaseProgramm::material_properties)
            .def("has_feature",
                 &ShaderMultyPhaseProgramm::has_feature,
                 nb::arg("feature"),
                 "Check if shader has a specific feature")
            .def("get_phase", &ShaderMultyPhaseProgramm::get_phase, nb::arg("mark"), nb::rv_policy::reference)
            // Backward compatibility: parse_shader_text now returns ShaderMultyPhaseProgramm directly
            .def_static(
                "from_tree",
                [](const ShaderMultyPhaseProgramm& prog) {
                    return prog; // Identity - already parsed
                },
                nb::arg("tree"),
                "Backward compatibility: returns the object as-is");

        // Parser functions
        m.def("parse_shader_text", &parse_shader_text, nb::arg("text"), "Parse shader text in custom format");

        m.def("parse_property_directive", &parse_property_directive, nb::arg("line"), "Parse @property directive line");
    }

} // namespace termin

NB_MODULE(_materials_native, m) {
    termin::bind_shader_parser(m);
    termin::bind_tc_material(m);
}
