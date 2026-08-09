#include <termin/glb/native_backend.h>

#include <cgltf.h>
#include <tcbase/tc_log.h>
#include <tgfx/resources/tc_mesh_registry.h>

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <limits>
#include <new>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace {

    void set_error(termin_glb_error* error, termin_glb_error_code code, const char* format, ...) {
        char message[TERMIN_GLB_ERROR_MESSAGE_SIZE] = {};
        va_list args;
        va_start(args, format);
        std::vsnprintf(message, sizeof(message), format, args);
        va_end(args);
        tc_log(TC_LOG_ERROR, "[termin_glb] %s", message);
        if (!error)
            return;
        error->code = code;
        std::snprintf(error->message, sizeof(error->message), "%s", message);
    }

    const char* cgltf_result_name(cgltf_result result) {
        switch (result) {
        case cgltf_result_success:
            return "success";
        case cgltf_result_data_too_short:
            return "data_too_short";
        case cgltf_result_unknown_format:
            return "unknown_format";
        case cgltf_result_invalid_json:
            return "invalid_json";
        case cgltf_result_invalid_gltf:
            return "invalid_gltf";
        case cgltf_result_invalid_options:
            return "invalid_options";
        case cgltf_result_file_not_found:
            return "file_not_found";
        case cgltf_result_io_error:
            return "io_error";
        case cgltf_result_out_of_memory:
            return "out_of_memory";
        case cgltf_result_legacy_gltf:
            return "legacy_gltf";
        case cgltf_result_max_enum:
            break;
        }
        return "unknown";
    }

    termin_glb_error_code error_code_for_result(cgltf_result result) {
        if (result == cgltf_result_out_of_memory)
            return TERMIN_GLB_ERROR_OUT_OF_MEMORY;
        if (result == cgltf_result_file_not_found || result == cgltf_result_io_error)
            return TERMIN_GLB_ERROR_IO;
        return TERMIN_GLB_ERROR_INVALID_FORMAT;
    }

    bool checked_add(size_t left, size_t right, size_t* result) {
        if (left > std::numeric_limits<size_t>::max() - right)
            return false;
        *result = left + right;
        return true;
    }

    bool checked_mul(size_t left, size_t right, size_t* result) {
        if (right != 0 && left > std::numeric_limits<size_t>::max() / right)
            return false;
        *result = left * right;
        return true;
    }

    class MappedFile {
    public:
        MappedFile() = default;
        MappedFile(const MappedFile&) = delete;
        MappedFile& operator=(const MappedFile&) = delete;

        ~MappedFile() {
            close();
        }

        bool open(const char* path, termin_glb_error* error) {
            if (!path || path[0] == '\0') {
                set_error(error, TERMIN_GLB_ERROR_IO, "GLB path is empty");
                return false;
            }
#if defined(_WIN32)
            const int wide_size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, nullptr, 0);
            if (wide_size <= 0) {
                set_error(error, TERMIN_GLB_ERROR_IO, "GLB path is not valid UTF-8: '%s'", path);
                return false;
            }
            std::wstring wide_path(static_cast<size_t>(wide_size), L'\0');
            MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, wide_path.data(), wide_size);
            file_ = CreateFileW(wide_path.c_str(),
                                GENERIC_READ,
                                FILE_SHARE_READ,
                                nullptr,
                                OPEN_EXISTING,
                                FILE_ATTRIBUTE_NORMAL,
                                nullptr);
            if (file_ == INVALID_HANDLE_VALUE) {
                set_error(error, TERMIN_GLB_ERROR_IO, "cannot open GLB '%s' (win32=%lu)", path, GetLastError());
                return false;
            }
            LARGE_INTEGER file_size = {};
            if (!GetFileSizeEx(file_, &file_size) || file_size.QuadPart <= 0 ||
                static_cast<unsigned long long>(file_size.QuadPart) > std::numeric_limits<size_t>::max()) {
                set_error(error, TERMIN_GLB_ERROR_IO, "GLB '%s' has an invalid file size", path);
                close();
                return false;
            }
            size_ = static_cast<size_t>(file_size.QuadPart);
            mapping_ = CreateFileMappingW(file_, nullptr, PAGE_READONLY, 0, 0, nullptr);
            if (!mapping_) {
                set_error(error, TERMIN_GLB_ERROR_IO, "cannot map GLB '%s' (win32=%lu)", path, GetLastError());
                close();
                return false;
            }
            data_ = MapViewOfFile(mapping_, FILE_MAP_READ, 0, 0, 0);
            if (!data_) {
                set_error(error, TERMIN_GLB_ERROR_IO, "cannot view mapped GLB '%s' (win32=%lu)", path, GetLastError());
                close();
                return false;
            }
#else
            fd_ = ::open(path, O_RDONLY);
            if (fd_ < 0) {
                set_error(error, TERMIN_GLB_ERROR_IO, "cannot open GLB '%s'", path);
                return false;
            }
            struct stat stat_buffer = {};
            if (fstat(fd_, &stat_buffer) != 0 || stat_buffer.st_size <= 0 ||
                static_cast<uintmax_t>(stat_buffer.st_size) > std::numeric_limits<size_t>::max()) {
                set_error(error, TERMIN_GLB_ERROR_IO, "GLB '%s' has an invalid file size", path);
                close();
                return false;
            }
            size_ = static_cast<size_t>(stat_buffer.st_size);
            data_ = mmap(nullptr, size_, PROT_READ, MAP_PRIVATE, fd_, 0);
            if (data_ == MAP_FAILED) {
                data_ = nullptr;
                set_error(error, TERMIN_GLB_ERROR_IO, "cannot mmap GLB '%s'", path);
                close();
                return false;
            }
#endif
            return true;
        }

        void close() {
#if defined(_WIN32)
            if (data_)
                UnmapViewOfFile(data_);
            if (mapping_)
                CloseHandle(mapping_);
            if (file_ != INVALID_HANDLE_VALUE)
                CloseHandle(file_);
            mapping_ = nullptr;
            file_ = INVALID_HANDLE_VALUE;
#else
            if (data_)
                munmap(data_, size_);
            if (fd_ >= 0)
                ::close(fd_);
            fd_ = -1;
#endif
            data_ = nullptr;
            size_ = 0;
        }

        const void* data() const {
            return data_;
        }
        size_t size() const {
            return size_;
        }

    private:
        void* data_ = nullptr;
        size_t size_ = 0;
#if defined(_WIN32)
        HANDLE file_ = INVALID_HANDLE_VALUE;
        HANDLE mapping_ = nullptr;
#else
        int fd_ = -1;
#endif
    };

    const cgltf_accessor* find_attribute(const cgltf_primitive& primitive, cgltf_attribute_type type, int index = 0) {
        for (cgltf_size i = 0; i < primitive.attributes_count; ++i) {
            const cgltf_attribute& attribute = primitive.attributes[i];
            if (attribute.type == type && attribute.index == index)
                return attribute.data;
        }
        return nullptr;
    }

    bool buffer_view_range_is_valid(const cgltf_buffer_view* view, size_t offset, size_t size) {
        if (!view || (!view->data && (!view->buffer || !view->buffer->data)))
            return false;
        size_t end = 0;
        if (!checked_add(offset, size, &end) || end > view->size)
            return false;
        if (view->buffer) {
            size_t view_end = 0;
            if (!checked_add(view->offset, view->size, &view_end) || view_end > view->buffer->size)
                return false;
        }
        return true;
    }

    bool accessor_storage_is_valid(const cgltf_accessor* accessor) {
        if (!accessor || accessor->count == 0)
            return false;
        const size_t element_size = cgltf_calc_size(accessor->type, accessor->component_type);
        if (element_size == 0)
            return false;
        if (accessor->buffer_view) {
            size_t tail_size = 0;
            size_t required_size = 0;
            if (!checked_mul(accessor->stride, accessor->count - 1, &tail_size) ||
                !checked_add(accessor->offset, tail_size, &required_size) ||
                !checked_add(required_size, element_size, &required_size) ||
                !buffer_view_range_is_valid(accessor->buffer_view, 0, required_size)) {
                return false;
            }
        } else if (!accessor->is_sparse) {
            return false;
        }
        if (accessor->is_sparse) {
            const cgltf_accessor_sparse& sparse = accessor->sparse;
            size_t indices_size = 0;
            size_t values_size = 0;
            if (sparse.count > accessor->count ||
                !checked_mul(cgltf_component_size(sparse.indices_component_type), sparse.count, &indices_size) ||
                !checked_mul(element_size, sparse.count, &values_size) ||
                !buffer_view_range_is_valid(sparse.indices_buffer_view, sparse.indices_byte_offset, indices_size) ||
                !buffer_view_range_is_valid(sparse.values_buffer_view, sparse.values_byte_offset, values_size)) {
                return false;
            }
        }
        return true;
    }

    bool validate_mesh_accessors(cgltf_data* data, const char* path, termin_glb_error* error) {
        for (cgltf_size mesh_index = 0; mesh_index < data->meshes_count; ++mesh_index) {
            const cgltf_mesh& mesh = data->meshes[mesh_index];
            for (cgltf_size primitive_index = 0; primitive_index < mesh.primitives_count; ++primitive_index) {
                const cgltf_primitive& primitive = mesh.primitives[primitive_index];
                const cgltf_accessor* positions = find_attribute(primitive, cgltf_attribute_type_position);
                if (!positions) {
                    set_error(error,
                              TERMIN_GLB_ERROR_INVALID_FORMAT,
                              "%s: mesh[%zu] primitive[%zu] POSITION accessor is missing",
                              path,
                              static_cast<size_t>(mesh_index),
                              static_cast<size_t>(primitive_index));
                    return false;
                }
                for (cgltf_size attribute_index = 0; attribute_index < primitive.attributes_count; ++attribute_index) {
                    const cgltf_attribute& attribute = primitive.attributes[attribute_index];
                    if (!accessor_storage_is_valid(attribute.data)) {
                        set_error(error,
                                  TERMIN_GLB_ERROR_INVALID_FORMAT,
                                  "%s: mesh[%zu] primitive[%zu] %s accessor storage is truncated or invalid",
                                  path,
                                  static_cast<size_t>(mesh_index),
                                  static_cast<size_t>(primitive_index),
                                  attribute.name ? attribute.name : "attribute");
                        return false;
                    }
                }
                if (!primitive.indices)
                    continue;
                if (!accessor_storage_is_valid(primitive.indices)) {
                    set_error(error,
                              TERMIN_GLB_ERROR_INVALID_FORMAT,
                              "%s: mesh[%zu] primitive[%zu] INDICES accessor storage is truncated or invalid",
                              path,
                              static_cast<size_t>(mesh_index),
                              static_cast<size_t>(primitive_index));
                    return false;
                }
                for (cgltf_size index = 0; index < primitive.indices->count; ++index) {
                    const cgltf_size value = cgltf_accessor_read_index(primitive.indices, index);
                    if (value >= positions->count) {
                        set_error(
                            error,
                            TERMIN_GLB_ERROR_INVALID_FORMAT,
                            "%s: mesh[%zu] primitive[%zu] INDICES accessor index[%zu]=%zu exceeds POSITION count=%zu",
                            path,
                            static_cast<size_t>(mesh_index),
                            static_cast<size_t>(primitive_index),
                            static_cast<size_t>(index),
                            static_cast<size_t>(value),
                            static_cast<size_t>(positions->count));
                        return false;
                    }
                }
            }
        }
        return true;
    }

    bool primitive_counts(const cgltf_primitive& primitive, size_t* vertices, size_t* indices) {
        const cgltf_accessor* positions = find_attribute(primitive, cgltf_attribute_type_position);
        if (!positions)
            return false;
        *vertices = positions->count;
        *indices = primitive.indices ? primitive.indices->count : positions->count;
        return true;
    }

    bool accessor_shape_is(const cgltf_accessor* accessor, cgltf_type type, size_t count) {
        return accessor && accessor->type == type && accessor->count == count;
    }

    bool read_attribute(const cgltf_accessor* accessor,
                        size_t vertex_index,
                        float* destination,
                        size_t component_count,
                        const char* path,
                        size_t mesh_index,
                        size_t primitive_index,
                        const char* semantic,
                        termin_glb_error* error) {
        if (cgltf_accessor_read_float(accessor, vertex_index, destination, component_count)) {
            // glTF normalization clamps the most-negative signed integer to
            // -1. cgltf performs the division but intentionally does not
            // clamp (-128/127 and -32768/32767), so keep the adapter contract
            // spec-correct here rather than forking the parser for one line.
            if (accessor->normalized && (accessor->component_type == cgltf_component_type_r_8 ||
                                         accessor->component_type == cgltf_component_type_r_16)) {
                for (size_t component = 0; component < component_count; ++component) {
                    if (destination[component] < -1.0f)
                        destination[component] = -1.0f;
                }
            }
            return true;
        }
        set_error(error,
                  TERMIN_GLB_ERROR_INVALID_FORMAT,
                  "%s: mesh[%zu] primitive[%zu] %s accessor read failed at vertex %zu",
                  path,
                  mesh_index,
                  primitive_index,
                  semantic,
                  vertex_index);
        return false;
    }

    void convert_vector_to_z_up(float* value) {
        const float y = value[1];
        value[1] = -value[2];
        value[2] = y;
    }

} // namespace

struct termin_glb_document {
    MappedFile mapping;
    cgltf_data* data = nullptr;
    std::string path;
    std::vector<std::string> fallback_mesh_names;

    ~termin_glb_document() {
        if (data)
            cgltf_free(data);
    }
};

termin_glb_document* termin_glb_document_open(const char* path, termin_glb_error* error) {
    termin_glb_error_clear(error);
    termin_glb_document* document = new (std::nothrow) termin_glb_document();
    if (!document) {
        set_error(error, TERMIN_GLB_ERROR_OUT_OF_MEMORY, "cannot allocate GLB document");
        return nullptr;
    }
    document->path = path ? path : "";
    if (!document->mapping.open(path, error)) {
        delete document;
        return nullptr;
    }

    cgltf_options options = {};
    cgltf_result result = cgltf_parse(
        &options, document->mapping.data(), static_cast<cgltf_size>(document->mapping.size()), &document->data);
    if (result != cgltf_result_success) {
        set_error(error,
                  error_code_for_result(result),
                  "%s: cgltf parse failed: %s",
                  document->path.c_str(),
                  cgltf_result_name(result));
        delete document;
        return nullptr;
    }
    if (document->data->file_type != cgltf_file_type_glb) {
        set_error(error,
                  TERMIN_GLB_ERROR_UNSUPPORTED,
                  "%s: native static-mesh backend currently accepts binary GLB only",
                  document->path.c_str());
        delete document;
        return nullptr;
    }
    result = cgltf_load_buffers(&options, document->data, path);
    if (result != cgltf_result_success) {
        set_error(error,
                  error_code_for_result(result),
                  "%s: cgltf buffer load failed: %s",
                  document->path.c_str(),
                  cgltf_result_name(result));
        delete document;
        return nullptr;
    }
    if (!validate_mesh_accessors(document->data, document->path.c_str(), error)) {
        delete document;
        return nullptr;
    }
    result = cgltf_validate(document->data);
    if (result != cgltf_result_success) {
        set_error(error,
                  TERMIN_GLB_ERROR_INVALID_FORMAT,
                  "%s: cgltf validation failed: %s",
                  document->path.c_str(),
                  cgltf_result_name(result));
        delete document;
        return nullptr;
    }

    try {
        document->fallback_mesh_names.reserve(document->data->meshes_count);
        for (cgltf_size i = 0; i < document->data->meshes_count; ++i)
            document->fallback_mesh_names.emplace_back("Mesh_" + std::to_string(i));
    } catch (const std::bad_alloc&) {
        set_error(error, TERMIN_GLB_ERROR_OUT_OF_MEMORY, "%s: cannot allocate mesh discovery data", path);
        delete document;
        return nullptr;
    }
    return document;
}

void termin_glb_document_close(termin_glb_document* document) {
    delete document;
}

size_t termin_glb_document_mesh_count(const termin_glb_document* document) {
    return document && document->data ? document->data->meshes_count : 0;
}

bool termin_glb_document_mesh_info(const termin_glb_document* document,
                                   size_t mesh_index,
                                   termin_glb_mesh_info* info,
                                   termin_glb_error* error) {
    termin_glb_error_clear(error);
    if (!document || !document->data || !info) {
        set_error(error, TERMIN_GLB_ERROR_INTERNAL, "mesh discovery requires a document and output pointer");
        return false;
    }
    if (mesh_index >= document->data->meshes_count) {
        set_error(error,
                  TERMIN_GLB_ERROR_INVALID_FORMAT,
                  "%s: mesh index %zu is out of range (count=%zu)",
                  document->path.c_str(),
                  mesh_index,
                  static_cast<size_t>(document->data->meshes_count));
        return false;
    }

    const cgltf_mesh& mesh = document->data->meshes[mesh_index];
    size_t vertex_count = 0;
    size_t index_count = 0;
    for (cgltf_size primitive_index = 0; primitive_index < mesh.primitives_count; ++primitive_index) {
        size_t primitive_vertices = 0;
        size_t primitive_indices = 0;
        if (!primitive_counts(mesh.primitives[primitive_index], &primitive_vertices, &primitive_indices)) {
            set_error(error,
                      TERMIN_GLB_ERROR_INVALID_FORMAT,
                      "%s: mesh[%zu] primitive[%zu] has no POSITION accessor",
                      document->path.c_str(),
                      mesh_index,
                      static_cast<size_t>(primitive_index));
            return false;
        }
        if (!checked_add(vertex_count, primitive_vertices, &vertex_count) ||
            !checked_add(index_count, primitive_indices, &index_count)) {
            set_error(error,
                      TERMIN_GLB_ERROR_INVALID_FORMAT,
                      "%s: mesh[%zu] vertex/index count overflows size_t",
                      document->path.c_str(),
                      mesh_index);
            return false;
        }
    }

    info->name = mesh.name ? mesh.name : document->fallback_mesh_names[mesh_index].c_str();
    info->primitive_count = mesh.primitives_count;
    info->vertex_count = vertex_count;
    info->index_count = index_count;
    return true;
}

bool termin_glb_document_build_static_mesh(termin_glb_document* document,
                                           size_t mesh_index,
                                           const char* mesh_uuid,
                                           const char* mesh_name,
                                           bool convert_to_z_up,
                                           termin_glb_error* error) {
    termin_glb_error_clear(error);
    if (!document || !document->data || !mesh_uuid || mesh_uuid[0] == '\0') {
        set_error(error, TERMIN_GLB_ERROR_INTERNAL, "static mesh build requires a document and non-empty UUID");
        return false;
    }

    termin_glb_mesh_info info = {};
    if (!termin_glb_document_mesh_info(document, mesh_index, &info, error))
        return false;
    if (info.primitive_count == 0) {
        set_error(error,
                  TERMIN_GLB_ERROR_INVALID_FORMAT,
                  "%s: mesh[%zu] '%s' has no primitives",
                  document->path.c_str(),
                  mesh_index,
                  info.name);
        return false;
    }
    if (info.index_count > UINT32_MAX || info.vertex_count > UINT32_MAX) {
        set_error(error,
                  TERMIN_GLB_ERROR_UNSUPPORTED,
                  "%s: mesh[%zu] '%s' exceeds uint32 mesh limits (vertices=%zu indices=%zu)",
                  document->path.c_str(),
                  mesh_index,
                  info.name,
                  info.vertex_count,
                  info.index_count);
        return false;
    }

    const cgltf_mesh& source_mesh = document->data->meshes[mesh_index];
    bool has_normals = false;
    bool has_texcoords = false;
    bool has_tangents = false;
    for (cgltf_size primitive_index = 0; primitive_index < source_mesh.primitives_count; ++primitive_index) {
        const cgltf_primitive& primitive = source_mesh.primitives[primitive_index];
        if (primitive.type != cgltf_primitive_type_triangles) {
            set_error(error,
                      TERMIN_GLB_ERROR_UNSUPPORTED,
                      "%s: mesh[%zu] '%s' primitive[%zu] uses unsupported topology %d",
                      document->path.c_str(),
                      mesh_index,
                      info.name,
                      static_cast<size_t>(primitive_index),
                      static_cast<int>(primitive.type));
            return false;
        }
        if (find_attribute(primitive, cgltf_attribute_type_joints) ||
            find_attribute(primitive, cgltf_attribute_type_weights)) {
            set_error(error,
                      TERMIN_GLB_ERROR_UNSUPPORTED,
                      "%s: mesh[%zu] '%s' primitive[%zu] is skinned; skinning is handled by the rig migration",
                      document->path.c_str(),
                      mesh_index,
                      info.name,
                      static_cast<size_t>(primitive_index));
            return false;
        }
        has_normals = has_normals || find_attribute(primitive, cgltf_attribute_type_normal);
        has_texcoords = has_texcoords || find_attribute(primitive, cgltf_attribute_type_texcoord);
        has_tangents = has_tangents || find_attribute(primitive, cgltf_attribute_type_tangent);
    }

    tc_vertex_layout layout = {};
    if (has_tangents)
        layout = tc_vertex_layout_pos_normal_uv_tangent();
    else if (has_texcoords)
        layout = tc_vertex_layout_pos_normal_uv();
    else if (has_normals)
        layout = tc_vertex_layout_pos_normal();
    else
        layout = tc_vertex_layout_pos();
    tc_mesh_data_builder builder = {};
    if (!tc_mesh_data_builder_allocate(&builder, info.vertex_count, &layout, info.index_count, info.primitive_count)) {
        set_error(error,
                  TERMIN_GLB_ERROR_OUT_OF_MEMORY,
                  "%s: mesh[%zu] '%s' could not allocate destination buffers",
                  document->path.c_str(),
                  mesh_index,
                  info.name);
        return false;
    }

    bool success = false;
    size_t vertex_base = 0;
    size_t index_base = 0;
    std::unordered_map<const cgltf_material*, uint32_t> material_slots;
    try {
        for (cgltf_size primitive_index = 0; primitive_index < source_mesh.primitives_count; ++primitive_index) {
            const cgltf_primitive& primitive = source_mesh.primitives[primitive_index];
            const cgltf_accessor* positions = find_attribute(primitive, cgltf_attribute_type_position);
            const size_t vertex_count = positions->count;
            const cgltf_accessor* normals = find_attribute(primitive, cgltf_attribute_type_normal);
            const cgltf_accessor* texcoords = find_attribute(primitive, cgltf_attribute_type_texcoord);
            const cgltf_accessor* tangents = find_attribute(primitive, cgltf_attribute_type_tangent);

            if (positions->type != cgltf_type_vec3 ||
                (normals && !accessor_shape_is(normals, cgltf_type_vec3, vertex_count)) ||
                (texcoords && !accessor_shape_is(texcoords, cgltf_type_vec2, vertex_count)) ||
                (tangents && !accessor_shape_is(tangents, cgltf_type_vec4, vertex_count))) {
                set_error(error,
                          TERMIN_GLB_ERROR_INVALID_FORMAT,
                          "%s: mesh[%zu] '%s' primitive[%zu] has incompatible static vertex accessor shape/count",
                          document->path.c_str(),
                          mesh_index,
                          info.name,
                          static_cast<size_t>(primitive_index));
                goto cleanup;
            }
            if (primitive.indices && (primitive.indices->type != cgltf_type_scalar ||
                                      (primitive.indices->component_type != cgltf_component_type_r_8u &&
                                       primitive.indices->component_type != cgltf_component_type_r_16u &&
                                       primitive.indices->component_type != cgltf_component_type_r_32u))) {
                set_error(error,
                          TERMIN_GLB_ERROR_INVALID_FORMAT,
                          "%s: mesh[%zu] '%s' primitive[%zu] has an invalid index accessor",
                          document->path.c_str(),
                          mesh_index,
                          info.name,
                          static_cast<size_t>(primitive_index));
                goto cleanup;
            }

            for (size_t vertex_index = 0; vertex_index < vertex_count; ++vertex_index) {
                float* destination = reinterpret_cast<float*>(static_cast<unsigned char*>(builder.vertices) +
                                                              (vertex_base + vertex_index) * layout.stride);
                if (!read_attribute(positions,
                                    vertex_index,
                                    destination,
                                    3,
                                    document->path.c_str(),
                                    mesh_index,
                                    primitive_index,
                                    "POSITION",
                                    error))
                    goto cleanup;
                if (convert_to_z_up)
                    convert_vector_to_z_up(destination);
                if (normals) {
                    if (!read_attribute(normals,
                                        vertex_index,
                                        destination + 3,
                                        3,
                                        document->path.c_str(),
                                        mesh_index,
                                        primitive_index,
                                        "NORMAL",
                                        error))
                        goto cleanup;
                    if (convert_to_z_up)
                        convert_vector_to_z_up(destination + 3);
                }
                if (texcoords && !read_attribute(texcoords,
                                                 vertex_index,
                                                 destination + 6,
                                                 2,
                                                 document->path.c_str(),
                                                 mesh_index,
                                                 primitive_index,
                                                 "TEXCOORD_0",
                                                 error))
                    goto cleanup;
                if (tangents) {
                    if (!read_attribute(tangents,
                                        vertex_index,
                                        destination + 8,
                                        4,
                                        document->path.c_str(),
                                        mesh_index,
                                        primitive_index,
                                        "TANGENT",
                                        error))
                        goto cleanup;
                    if (convert_to_z_up)
                        convert_vector_to_z_up(destination + 8);
                }
            }

            const size_t primitive_index_count = primitive.indices ? primitive.indices->count : vertex_count;
            for (size_t local_index = 0; local_index < primitive_index_count; ++local_index) {
                const size_t source_index =
                    primitive.indices ? cgltf_accessor_read_index(primitive.indices, local_index) : local_index;
                if (source_index >= vertex_count) {
                    set_error(error,
                              TERMIN_GLB_ERROR_INVALID_FORMAT,
                              "%s: mesh[%zu] '%s' primitive[%zu] index[%zu]=%zu exceeds vertex_count=%zu",
                              document->path.c_str(),
                              mesh_index,
                              info.name,
                              static_cast<size_t>(primitive_index),
                              local_index,
                              source_index,
                              vertex_count);
                    goto cleanup;
                }
                builder.indices[index_base + local_index] = static_cast<uint32_t>(vertex_base + source_index);
            }

            uint32_t material_slot = 0;
            const auto slot = material_slots.find(primitive.material);
            if (slot == material_slots.end()) {
                material_slot = static_cast<uint32_t>(material_slots.size());
                material_slots.emplace(primitive.material, material_slot);
            } else {
                material_slot = slot->second;
            }
            tc_submesh& submesh = builder.submeshes[primitive_index];
            submesh.first_index = static_cast<uint32_t>(index_base);
            submesh.index_count = static_cast<uint32_t>(primitive_index_count);
            submesh.material_slot = material_slot;
            submesh.draw_mode = TC_DRAW_TRIANGLES;
            const char* material_name = primitive.material ? primitive.material->name : nullptr;
            if (material_name && material_name[0] != '\0')
                std::snprintf(submesh.name, sizeof(submesh.name), "%s/%s", info.name, material_name);
            else
                std::snprintf(submesh.name,
                              sizeof(submesh.name),
                              "%s/primitive_%zu",
                              info.name,
                              static_cast<size_t>(primitive_index));

            vertex_base += vertex_count;
            index_base += primitive_index_count;
        }
    } catch (const std::bad_alloc&) {
        set_error(error,
                  TERMIN_GLB_ERROR_OUT_OF_MEMORY,
                  "%s: mesh[%zu] '%s' material slot allocation failed",
                  document->path.c_str(),
                  mesh_index,
                  info.name);
        goto cleanup;
    }

    {
        const char* effective_name = mesh_name && mesh_name[0] != '\0' ? mesh_name : info.name;
        tc_mesh_handle handle = tc_mesh_declare(mesh_uuid, effective_name);
        tc_mesh* target = tc_mesh_get(handle);
        if (!target) {
            set_error(error,
                      TERMIN_GLB_ERROR_INTERNAL,
                      "%s: mesh[%zu] '%s' could not resolve target tc_mesh UUID '%s'",
                      document->path.c_str(),
                      mesh_index,
                      info.name,
                      mesh_uuid);
            goto cleanup;
        }
        if (!tc_mesh_data_builder_commit(target, &builder, effective_name)) {
            set_error(error,
                      TERMIN_GLB_ERROR_INTERNAL,
                      "%s: mesh[%zu] '%s' transactional tc_mesh commit failed",
                      document->path.c_str(),
                      mesh_index,
                      info.name);
            goto cleanup;
        }
    }
    success = true;

cleanup:
    tc_mesh_data_builder_discard(&builder);
    return success;
}
