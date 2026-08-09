#include <termin/glb/native_backend.h>

#include <tgfx/resources/tc_mesh_registry.h>

#include <nanobind/nanobind.h>
#include <nanobind/stl/string.h>

#include <stdexcept>
#include <string>
#include <vector>

namespace nb = nanobind;

namespace {

    uint64_t fnv1a(const void* data, size_t size, uint64_t hash = 14695981039346656037ull) {
        const auto* bytes = static_cast<const uint8_t*>(data);
        for (size_t i = 0; i < size; ++i) {
            hash ^= bytes[i];
            hash *= 1099511628211ull;
        }
        return hash;
    }

    std::runtime_error native_error(const termin_glb_error& error) {
        return std::runtime_error(std::string(termin_glb_error_code_name(error.code)) + ": " + error.message);
    }

    nb::dict texture_view_dict(const termin_glb_texture_view_info& info) {
        nb::dict result;
        result["present"] = info.present;
        if (info.present) {
            result["texture_index"] = info.texture_index;
            result["texcoord"] = info.texcoord;
            result["scale"] = info.scale;
            result["has_transform"] = info.has_transform;
        }
        return result;
    }

    class NativeDocument {
    public:
        explicit NativeDocument(const std::string& path) {
            termin_glb_error error = {};
            document_ = termin_glb_document_open(path.c_str(), &error);
            if (!document_)
                throw native_error(error);
        }

        NativeDocument(const NativeDocument&) = delete;
        NativeDocument& operator=(const NativeDocument&) = delete;

        ~NativeDocument() {
            termin_glb_document_close(document_);
        }

        size_t mesh_count() const {
            return termin_glb_document_mesh_count(document_);
        }

        nb::dict mesh_info(size_t mesh_index) const {
            termin_glb_mesh_info info = {};
            termin_glb_error error = {};
            if (!termin_glb_document_mesh_info(document_, mesh_index, &info, &error))
                throw native_error(error);
            nb::dict result;
            result["name"] = info.name;
            result["primitive_count"] = info.primitive_count;
            result["vertex_count"] = info.vertex_count;
            result["index_count"] = info.index_count;
            result["skinned"] = info.skinned;
            return result;
        }

        nb::list meshes() const {
            nb::list result;
            for (size_t i = 0; i < mesh_count(); ++i)
                result.append(mesh_info(i));
            return result;
        }

        nb::dict image_info(size_t image_index) const {
            termin_glb_image_info info = {};
            termin_glb_error error = {};
            if (!termin_glb_document_image_info(document_, image_index, &info, &error))
                throw native_error(error);
            nb::dict result;
            result["name"] = info.name;
            result["has_name"] = info.has_name;
            result["mime_type"] = info.mime_type;
            result["uri"] = info.uri;
            result["embedded"] = info.embedded;
            result["encoded_size"] = info.encoded_size;
            return result;
        }

        nb::list images() const {
            nb::list result;
            for (size_t i = 0; i < termin_glb_document_image_count(document_); ++i)
                result.append(image_info(i));
            return result;
        }

        nb::bytes image_payload(size_t image_index) const {
            const unsigned char* data = nullptr;
            size_t size = 0;
            termin_glb_error error = {};
            if (!termin_glb_document_image_payload(document_, image_index, &data, &size, &error))
                throw native_error(error);
            return nb::bytes(reinterpret_cast<const char*>(data), size);
        }

        nb::dict texture_info(size_t texture_index) const {
            termin_glb_texture_info info = {};
            termin_glb_error error = {};
            if (!termin_glb_document_texture_info(document_, texture_index, &info, &error))
                throw native_error(error);
            nb::dict result;
            result["name"] = info.name;
            result["has_name"] = info.has_name;
            result["has_image"] = info.has_image;
            result["image_index"] = info.image_index;
            result["has_sampler"] = info.has_sampler;
            result["sampler_index"] = info.sampler_index;
            result["selected_webp"] = info.selected_webp;
            result["selected_basisu"] = info.selected_basisu;
            result["mag_filter"] = info.mag_filter;
            result["min_filter"] = info.min_filter;
            result["wrap_s"] = info.wrap_s;
            result["wrap_t"] = info.wrap_t;
            return result;
        }

        nb::list textures() const {
            nb::list result;
            for (size_t i = 0; i < termin_glb_document_texture_count(document_); ++i)
                result.append(texture_info(i));
            return result;
        }

        nb::dict material_info(size_t material_index) const {
            termin_glb_material_info info = {};
            termin_glb_error error = {};
            if (!termin_glb_document_material_info(document_, material_index, &info, &error))
                throw native_error(error);
            nb::dict result;
            result["name"] = info.name;
            result["base_color_factor"] = nb::make_tuple(info.base_color_factor[0],
                                                          info.base_color_factor[1],
                                                          info.base_color_factor[2],
                                                          info.base_color_factor[3]);
            result["metallic_factor"] = info.metallic_factor;
            result["roughness_factor"] = info.roughness_factor;
            result["base_color_texture"] = texture_view_dict(info.base_color_texture);
            result["metallic_roughness_texture"] = texture_view_dict(info.metallic_roughness_texture);
            result["normal_texture"] = texture_view_dict(info.normal_texture);
            result["occlusion_texture"] = texture_view_dict(info.occlusion_texture);
            result["emissive_texture"] = texture_view_dict(info.emissive_texture);
            result["emissive_factor"] = nb::make_tuple(
                info.emissive_factor[0], info.emissive_factor[1], info.emissive_factor[2]);
            result["alpha_mode"] = info.alpha_mode;
            result["alpha_cutoff"] = info.alpha_cutoff;
            result["double_sided"] = info.double_sided;
            result["unlit"] = info.unlit;
            result["ior"] = info.ior;
            result["specular_factor"] = info.specular_factor;
            result["specular_color_factor"] = nb::make_tuple(info.specular_color_factor[0],
                                                               info.specular_color_factor[1],
                                                               info.specular_color_factor[2]);
            return result;
        }

        nb::list materials() const {
            nb::list result;
            for (size_t i = 0; i < termin_glb_document_material_count(document_); ++i)
                result.append(material_info(i));
            return result;
        }

        nb::dict rig_data() const {
            nb::dict result;
            nb::list nodes;
            for (size_t node_index = 0; node_index < termin_glb_document_node_count(document_); ++node_index) {
                termin_glb_node_info info = {};
                termin_glb_error error = {};
                if (!termin_glb_document_node_info(document_, node_index, &info, &error))
                    throw native_error(error);
                nb::dict node;
                node["name"] = info.name;
                node["parent_index"] = info.has_parent ? nb::cast(info.parent_index) : nb::none();
                node["mesh_index"] = info.has_mesh ? nb::cast(info.mesh_index) : nb::none();
                node["skin_index"] = info.has_skin ? nb::cast(info.skin_index) : nb::none();
                node["default_scene_root"] = info.default_scene_root;
                node["has_matrix"] = info.has_matrix;
                node["translation"] = nb::make_tuple(
                    info.translation[0], info.translation[1], info.translation[2]);
                node["rotation"] = nb::make_tuple(
                    info.rotation[0], info.rotation[1], info.rotation[2], info.rotation[3]);
                node["scale"] = nb::make_tuple(info.scale[0], info.scale[1], info.scale[2]);
                nb::list matrix;
                for (float value : info.matrix)
                    matrix.append(value);
                node["matrix"] = std::move(matrix);
                nodes.append(std::move(node));
            }
            result["nodes"] = std::move(nodes);

            nb::list skins;
            uint64_t rig_hash = 14695981039346656037ull;
            for (size_t skin_index = 0; skin_index < termin_glb_document_skin_count(document_); ++skin_index) {
                termin_glb_skin_info info = {};
                termin_glb_error error = {};
                if (!termin_glb_document_skin_info(document_, skin_index, &info, &error))
                    throw native_error(error);
                std::vector<size_t> joints(info.joint_count);
                if (!termin_glb_document_skin_joints(
                        document_, skin_index, joints.data(), joints.size(), &error))
                    throw native_error(error);
                std::vector<float> matrices(info.joint_count * 16);
                if (!termin_glb_document_skin_inverse_bind_matrices(
                        document_, skin_index, matrices.data(), matrices.size(), &error))
                    throw native_error(error);
                const size_t component_count = 16;
                rig_hash = fnv1a(&info.joint_count, sizeof(info.joint_count), rig_hash);
                rig_hash = fnv1a(&component_count, sizeof(component_count), rig_hash);
                rig_hash = fnv1a(matrices.data(), matrices.size() * sizeof(float), rig_hash);
                nb::dict skin;
                skin["name"] = info.name;
                skin["skeleton_node_index"] =
                    info.has_skeleton ? nb::cast(info.skeleton_node_index) : nb::none();
                nb::list joint_list;
                for (size_t joint : joints)
                    joint_list.append(joint);
                skin["joints"] = std::move(joint_list);
                skin["inverse_bind_matrices"] = nb::bytes(
                    reinterpret_cast<const char*>(matrices.data()), matrices.size() * sizeof(float));
                skins.append(std::move(skin));
            }
            result["skins"] = std::move(skins);

            nb::list animations;
            std::vector<float> times;
            std::vector<float> values;
            size_t total_sampler_count = 0;
            size_t total_channel_count = 0;
            for (size_t animation_index = 0;
                 animation_index < termin_glb_document_animation_count(document_);
                 ++animation_index) {
                termin_glb_animation_info info = {};
                termin_glb_error error = {};
                if (!termin_glb_document_animation_info(document_, animation_index, &info, &error))
                    throw native_error(error);
                nb::dict animation;
                animation["name"] = info.name;
                nb::list samplers;
                for (size_t sampler_index = 0; sampler_index < info.sampler_count; ++sampler_index) {
                    termin_glb_animation_sampler_info sampler_info = {};
                    if (!termin_glb_document_animation_sampler_info(
                            document_, animation_index, sampler_index, &sampler_info, &error))
                        throw native_error(error);
                    const size_t time_first = times.size();
                    const size_t value_first = values.size();
                    times.resize(time_first + sampler_info.input_count);
                    values.resize(value_first + sampler_info.output_float_count);
                    if (!termin_glb_document_animation_sampler_payload(
                            document_,
                            animation_index,
                            sampler_index,
                            times.data() + time_first,
                            sampler_info.input_count,
                            values.data() + value_first,
                            sampler_info.output_float_count,
                            &error))
                        throw native_error(error);
                    rig_hash = fnv1a(&sampler_info.input_count, sizeof(sampler_info.input_count), rig_hash);
                    const size_t scalar_components = 1;
                    rig_hash = fnv1a(&scalar_components, sizeof(scalar_components), rig_hash);
                    rig_hash = fnv1a(times.data() + time_first,
                                     sampler_info.input_count * sizeof(float),
                                     rig_hash);
                    const size_t output_element_count =
                        sampler_info.output_float_count / sampler_info.output_components;
                    rig_hash = fnv1a(&output_element_count, sizeof(output_element_count), rig_hash);
                    rig_hash = fnv1a(
                        &sampler_info.output_components, sizeof(sampler_info.output_components), rig_hash);
                    rig_hash = fnv1a(values.data() + value_first,
                                     sampler_info.output_float_count * sizeof(float),
                                     rig_hash);
                    nb::dict sampler;
                    sampler["time_first"] = time_first;
                    sampler["time_count"] = sampler_info.input_count;
                    sampler["value_first"] = value_first;
                    sampler["value_count"] = sampler_info.output_float_count;
                    sampler["components"] = sampler_info.output_components;
                    sampler["interpolation"] = sampler_info.interpolation;
                    samplers.append(std::move(sampler));
                }
                animation["samplers"] = std::move(samplers);
                nb::list channels;
                for (size_t channel_index = 0; channel_index < info.channel_count; ++channel_index) {
                    termin_glb_animation_channel_info channel_info = {};
                    if (!termin_glb_document_animation_channel_info(
                            document_, animation_index, channel_index, &channel_info, &error))
                        throw native_error(error);
                    nb::dict channel;
                    channel["sampler_index"] = channel_info.sampler_index;
                    channel["target_node_index"] = channel_info.has_target_node
                                                       ? nb::cast(channel_info.target_node_index)
                                                       : nb::none();
                    channel["target_path"] = channel_info.target_path;
                    channels.append(std::move(channel));
                }
                animation["channels"] = std::move(channels);
                animations.append(std::move(animation));
                total_sampler_count += info.sampler_count;
                total_channel_count += info.channel_count;
            }
            result["animations"] = std::move(animations);
            result["times"] = nb::bytes(
                reinterpret_cast<const char*>(times.data()), times.size() * sizeof(float));
            result["values"] = nb::bytes(
                reinterpret_cast<const char*>(values.data()), values.size() * sizeof(float));
            result["sampler_count"] = total_sampler_count;
            result["channel_count"] = total_channel_count;
            result["rig_hash"] = rig_hash;
            return result;
        }

        nb::object build_static_mesh(size_t mesh_index,
                                     const std::string& mesh_uuid,
                                     const std::string& mesh_name,
                                     bool convert_to_z_up,
                                     bool compute_diagnostics) {
            termin_glb_error error = {};
            if (!termin_glb_document_build_mesh(document_,
                                                mesh_index,
                                                mesh_uuid.c_str(),
                                                mesh_name.empty() ? nullptr : mesh_name.c_str(),
                                                convert_to_z_up,
                                                &error)) {
                throw native_error(error);
            }
            if (!compute_diagnostics)
                return nb::none();

            const tc_mesh_handle handle = tc_mesh_find(mesh_uuid.c_str());
            const tc_mesh* mesh = tc_mesh_get(handle);
            if (!mesh)
                throw std::runtime_error("native GLB build did not publish the requested tc_mesh UUID");
            const size_t vertex_bytes = tc_mesh_vertices_size(mesh);
            const size_t index_bytes = tc_mesh_indices_size(mesh);
            uint64_t payload_hash = fnv1a(mesh->vertices, vertex_bytes);
            payload_hash = fnv1a(mesh->indices, index_bytes, payload_hash);
            nb::dict diagnostics;
            diagnostics["payload_bytes"] = vertex_bytes + index_bytes;
            diagnostics["payload_hash"] = payload_hash;
            return diagnostics;
        }

    private:
        termin_glb_document* document_ = nullptr;
    };

} // namespace

NB_MODULE(_glb_native, module) {
    module.doc() = "Native cgltf backend for termin-glb";

    nb::enum_<termin_glb_error_code>(module, "NativeErrorCode")
        .value("NONE", TERMIN_GLB_ERROR_NONE)
        .value("IO", TERMIN_GLB_ERROR_IO)
        .value("INVALID_FORMAT", TERMIN_GLB_ERROR_INVALID_FORMAT)
        .value("UNSUPPORTED", TERMIN_GLB_ERROR_UNSUPPORTED)
        .value("OUT_OF_MEMORY", TERMIN_GLB_ERROR_OUT_OF_MEMORY)
        .value("INTERNAL", TERMIN_GLB_ERROR_INTERNAL);

    module.def("error_code_name", &termin_glb_error_code_name, nb::arg("code"));
    module.def("backend_info", []() {
        nb::dict info;
        info["name"] = termin_glb_backend_name();
        info["cgltf_version"] = termin_glb_cgltf_version();
        info["cgltf_revision"] = termin_glb_cgltf_revision();
        return info;
    });

    nb::class_<NativeDocument>(module, "NativeDocument")
        .def(nb::init<const std::string&>(), nb::arg("path"))
        .def_prop_ro("mesh_count", &NativeDocument::mesh_count)
        .def_prop_ro("meshes", &NativeDocument::meshes)
        .def("mesh_info", &NativeDocument::mesh_info, nb::arg("mesh_index"))
        .def_prop_ro("images", &NativeDocument::images)
        .def("image_info", &NativeDocument::image_info, nb::arg("image_index"))
        .def("image_payload", &NativeDocument::image_payload, nb::arg("image_index"))
        .def_prop_ro("textures", &NativeDocument::textures)
        .def("texture_info", &NativeDocument::texture_info, nb::arg("texture_index"))
        .def_prop_ro("materials", &NativeDocument::materials)
        .def("material_info", &NativeDocument::material_info, nb::arg("material_index"))
        .def("rig_data", &NativeDocument::rig_data)
        .def("build_mesh",
             &NativeDocument::build_static_mesh,
             nb::arg("mesh_index"),
             nb::arg("mesh_uuid"),
             nb::arg("mesh_name") = "",
             nb::arg("convert_to_z_up") = true,
             nb::arg("compute_diagnostics") = false)
        .def("build_static_mesh",
             &NativeDocument::build_static_mesh,
             nb::arg("mesh_index"),
             nb::arg("mesh_uuid"),
             nb::arg("mesh_name") = "",
             nb::arg("convert_to_z_up") = true,
             nb::arg("compute_diagnostics") = false);
}
