#include <tgfx/tgfx_mesh_handle.hpp>
#include <tgfx/tgfx_mesh3.hpp>

namespace termin {

bool TcMesh::set_from_mesh3(const Mesh3& mesh, const tc_vertex_layout* custom_layout) {
    tc_mesh* m = get();
    if (!m) {
        return false;
    }

    if (mesh.vertices.empty()) {
        return false;
    }

    // Choose layout based on mesh data (tangents require larger layout)
    tc_vertex_layout layout;
    if (custom_layout) {
        layout = *custom_layout;
    } else if (mesh.has_tangents()) {
        layout = tc_vertex_layout_pos_normal_uv_tangent();
    } else {
        layout = tc_vertex_layout_pos_normal_uv();
    }

    // Build interleaved vertex buffer
    size_t num_verts = mesh.vertices.size();
    size_t stride = layout.stride;
    std::vector<uint8_t> buffer(num_verts * stride, 0);

    const tc_vertex_attrib* pos_attr = tc_vertex_layout_find(&layout, "position");
    const tc_vertex_attrib* norm_attr = tc_vertex_layout_find(&layout, "normal");
    const tc_vertex_attrib* uv_attr = tc_vertex_layout_find(&layout, "uv");
    const tc_vertex_attrib* tan_attr = tc_vertex_layout_find(&layout, "tangent");

    for (size_t i = 0; i < num_verts; i++) {
        uint8_t* dst = buffer.data() + i * stride;

        if (pos_attr) {
            float* p = reinterpret_cast<float*>(dst + pos_attr->offset);
            p[0] = mesh.vertices[i].x;
            p[1] = mesh.vertices[i].y;
            p[2] = mesh.vertices[i].z;
        }

        if (norm_attr && i < mesh.normals.size()) {
            float* n = reinterpret_cast<float*>(dst + norm_attr->offset);
            n[0] = mesh.normals[i].x;
            n[1] = mesh.normals[i].y;
            n[2] = mesh.normals[i].z;
        }

        if (uv_attr && i < mesh.uvs.size()) {
            float* u = reinterpret_cast<float*>(dst + uv_attr->offset);
            u[0] = mesh.uvs[i].x;
            u[1] = mesh.uvs[i].y;
        }

        if (tan_attr && i < mesh.tangents.size()) {
            float* t = reinterpret_cast<float*>(dst + tan_attr->offset);
            t[0] = mesh.tangents[i].x;
            t[1] = mesh.tangents[i].y;
            t[2] = mesh.tangents[i].z;
            t[3] = mesh.tangents[i].w;
        }
    }

    return tc_mesh_set_data(m,
                            buffer.data(), num_verts, &layout,
                            mesh.triangles.data(), mesh.triangles.size(),
                            mesh.name.empty() ? nullptr : mesh.name.c_str());
}

TcMesh TcMesh::from_mesh3(const Mesh3& mesh,
                          const std::string& override_name,
                          const std::string& override_uuid,
                          const tc_vertex_layout* custom_layout) {
    if (mesh.vertices.empty()) {
        return TcMesh();
    }

    // Use override_uuid if provided, otherwise use mesh.uuid
    std::string uuid_str = override_uuid.empty() ? mesh.uuid : override_uuid;

    // Check if already in registry
    if (!uuid_str.empty()) {
        tc_mesh_handle h = tc_mesh_find(uuid_str.c_str());
        if (!tc_mesh_handle_is_invalid(h)) {
            return TcMesh(h);
        }
    }

    // Choose layout based on mesh data (tangents require larger layout)
    tc_vertex_layout layout;
    if (custom_layout) {
        layout = *custom_layout;
    } else if (mesh.has_tangents()) {
        layout = tc_vertex_layout_pos_normal_uv_tangent();
    } else {
        layout = tc_vertex_layout_pos_normal_uv();
    }

    // Build interleaved vertex buffer
    size_t num_verts = mesh.vertices.size();
    size_t stride = layout.stride;
    std::vector<uint8_t> buffer(num_verts * stride, 0);

    const tc_vertex_attrib* pos_attr = tc_vertex_layout_find(&layout, "position");
    const tc_vertex_attrib* norm_attr = tc_vertex_layout_find(&layout, "normal");
    const tc_vertex_attrib* uv_attr = tc_vertex_layout_find(&layout, "uv");
    const tc_vertex_attrib* tan_attr = tc_vertex_layout_find(&layout, "tangent");

    for (size_t i = 0; i < num_verts; i++) {
        uint8_t* dst = buffer.data() + i * stride;

        // Position
        if (pos_attr) {
            float* p = reinterpret_cast<float*>(dst + pos_attr->offset);
            p[0] = mesh.vertices[i].x;
            p[1] = mesh.vertices[i].y;
            p[2] = mesh.vertices[i].z;
        }

        // Normal
        if (norm_attr && i < mesh.normals.size()) {
            float* n = reinterpret_cast<float*>(dst + norm_attr->offset);
            n[0] = mesh.normals[i].x;
            n[1] = mesh.normals[i].y;
            n[2] = mesh.normals[i].z;
        }

        // UV
        if (uv_attr && i < mesh.uvs.size()) {
            float* u = reinterpret_cast<float*>(dst + uv_attr->offset);
            u[0] = mesh.uvs[i].x;
            u[1] = mesh.uvs[i].y;
        }

        // Tangent
        if (tan_attr && i < mesh.tangents.size()) {
            float* t = reinterpret_cast<float*>(dst + tan_attr->offset);
            t[0] = mesh.tangents[i].x;
            t[1] = mesh.tangents[i].y;
            t[2] = mesh.tangents[i].z;
            t[3] = mesh.tangents[i].w;
        }
    }

    // Compute UUID from data if not provided
    if (uuid_str.empty()) {
        char computed_uuid[40];
        tc_mesh_compute_uuid(buffer.data(), buffer.size(),
                            mesh.triangles.data(), mesh.triangles.size(),
                            computed_uuid);
        uuid_str = computed_uuid;
    }

    // Get or create mesh in registry
    tc_mesh_handle h = tc_mesh_get_or_create(uuid_str.c_str());
    tc_mesh* m = tc_mesh_get(h);
    if (!m) {
        return TcMesh();
    }

    // Set data if mesh is new (vertex_count == 0)
    if (m->vertex_count == 0) {
        std::string mesh_name = override_name.empty() ? mesh.name : override_name;
        tc_mesh_set_data(m,
                        buffer.data(), num_verts, &layout,
                        mesh.triangles.data(), mesh.triangles.size(),
                        mesh_name.empty() ? nullptr : mesh_name.c_str());
    }

    return TcMesh(h);
}

TcMesh TcMesh::from_interleaved(
    const void* vertices, size_t vertex_count,
    const uint32_t* indices, size_t index_count,
    const tc_vertex_layout& layout,
    const std::string& name,
    const std::string& uuid_hint,
    tc_draw_mode draw_mode) {

    if (vertices == nullptr || vertex_count == 0) {
        return TcMesh();
    }

    // Use uuid_hint if provided, otherwise compute from data
    std::string uuid_str = uuid_hint;

    // Check if already in registry
    if (!uuid_str.empty()) {
        tc_mesh_handle h = tc_mesh_find(uuid_str.c_str());
        if (!tc_mesh_handle_is_invalid(h)) {
            return TcMesh(h);
        }
    }

    // Compute UUID from data if not provided
    if (uuid_str.empty()) {
        size_t vertices_size = vertex_count * layout.stride;
        char computed_uuid[40];
        tc_mesh_compute_uuid(vertices, vertices_size, indices, index_count, computed_uuid);
        uuid_str = computed_uuid;
    }

    // Get or create mesh in registry
    tc_mesh_handle h = tc_mesh_get_or_create(uuid_str.c_str());
    tc_mesh* m = tc_mesh_get(h);
    if (!m) {
        return TcMesh();
    }

    // Set data if mesh is new (vertex_count == 0)
    if (m->vertex_count == 0) {
        tc_mesh_set_data(m,
                        vertices, vertex_count, &layout,
                        indices, index_count,
                        name.empty() ? nullptr : name.c_str());
        m->draw_mode = static_cast<uint8_t>(draw_mode);
    }

    return TcMesh(h);
}

} // namespace termin
