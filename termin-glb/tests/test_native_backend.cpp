#include <termin/glb/native_backend.h>

#include <tgfx/resources/tc_mesh_registry.h>

#include <cstring>
#include <string>

int main() {
    if (std::strcmp(termin_glb_backend_name(), "cgltf") != 0)
        return 1;
    if (std::strcmp(termin_glb_cgltf_version(), TERMIN_GLB_CGLTF_VERSION) != 0)
        return 2;
    if (std::strcmp(termin_glb_cgltf_revision(), TERMIN_GLB_CGLTF_REVISION) != 0)
        return 3;
    if (std::strcmp(termin_glb_error_code_name(TERMIN_GLB_ERROR_UNSUPPORTED), "unsupported") != 0)
        return 4;

    termin_glb_error error{TERMIN_GLB_ERROR_INTERNAL, "failure"};
    termin_glb_error_clear(&error);
    if (error.code != TERMIN_GLB_ERROR_NONE || error.message[0] != '\0')
        return 5;

    const std::string fixture_dir = TERMIN_GLB_TEST_FIXTURE_DIR;
    const std::string box_path = fixture_dir + "/Box.glb";
    termin_glb_document* document = termin_glb_document_open(box_path.c_str(), &error);
    if (!document)
        return 6;
    if (termin_glb_document_mesh_count(document) != 1)
        return 7;

    termin_glb_mesh_info info = {};
    if (!termin_glb_document_mesh_info(document, 0, &info, &error))
        return 8;
    if (std::strcmp(info.name, "Mesh") != 0 || info.primitive_count != 1 || info.vertex_count != 24 ||
        info.index_count != 36 || info.skinned)
        return 9;
    if (termin_glb_document_material_count(document) != 1 || termin_glb_document_image_count(document) != 0 ||
        termin_glb_document_texture_count(document) != 0)
        return 16;
    termin_glb_material_info material = {};
    if (!termin_glb_document_material_info(document, 0, &material, &error) ||
        std::strcmp(material.name, "Red") != 0 || material.base_color_texture.present)
        return 17;
    if (termin_glb_document_node_count(document) != 2 || termin_glb_document_skin_count(document) != 0 ||
        termin_glb_document_animation_count(document) != 0)
        return 18;
    termin_glb_node_info node = {};
    if (!termin_glb_document_node_info(document, 0, &node, &error) || !node.default_scene_root)
        return 19;

    tc_mesh_init();
    if (!termin_glb_document_build_static_mesh(document, 0, "native-box", "Native Box", true, &error))
        return 10;
    tc_mesh_handle handle = tc_mesh_find("native-box");
    tc_mesh* mesh = tc_mesh_get(handle);
    if (!mesh || mesh->vertex_count != 24 || mesh->index_count != 36 || mesh->submesh_count != 1)
        return 11;
    if (mesh->layout.stride != 24 || mesh->submeshes[0].index_count != 36 ||
        std::strcmp(mesh->submeshes[0].name, "Mesh/Red") != 0)
        return 12;
    for (size_t i = 0; i < mesh->index_count; ++i) {
        if (mesh->indices[i] >= mesh->vertex_count)
            return 13;
    }
    termin_glb_document_close(document);

    const std::string gltf_path = fixture_dir + "/TriangleWithoutIndices.gltf";
    document = termin_glb_document_open(gltf_path.c_str(), &error);
    if (document || error.code != TERMIN_GLB_ERROR_UNSUPPORTED ||
        std::strstr(error.message, gltf_path.c_str()) == nullptr)
        return 14;

    for (int i = 0; i < 64; ++i) {
        document = termin_glb_document_open(box_path.c_str(), &error);
        if (!document)
            return 15;
        termin_glb_document_close(document);
    }
    tc_mesh_shutdown();
    return 0;
}
