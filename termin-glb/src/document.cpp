#include <termin/glb/native_backend.h>

#include <cgltf.h>
#include <tcbase/tc_log.h>
#include <tgfx/resources/tc_mesh_registry.h>

#include <cstdarg>
#include <cmath>
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

    void add_cross(float* destination, const float* origin, const float* left, const float* right) {
        const float ax = left[0] - origin[0];
        const float ay = left[1] - origin[1];
        const float az = left[2] - origin[2];
        const float bx = right[0] - origin[0];
        const float by = right[1] - origin[1];
        const float bz = right[2] - origin[2];
        destination[0] += ay * bz - az * by;
        destination[1] += az * bx - ax * bz;
        destination[2] += ax * by - ay * bx;
    }

    void normalize3(float* vector) {
        const float length = std::sqrt(vector[0] * vector[0] + vector[1] * vector[1] + vector[2] * vector[2]);
        if (length <= 1e-8f)
            return;
        vector[0] /= length;
        vector[1] /= length;
        vector[2] /= length;
    }

    float dot3(const float* left, const float* right) {
        return left[0] * right[0] + left[1] * right[1] + left[2] * right[2];
    }

    void cross3(const float* left, const float* right, float* destination) {
        destination[0] = left[1] * right[2] - left[2] * right[1];
        destination[1] = left[2] * right[0] - left[0] * right[2];
        destination[2] = left[0] * right[1] - left[1] * right[0];
    }

    bool is_supported_required_extension(const char* extension) {
        static const char* supported[] = {
            "EXT_texture_webp",
            "KHR_mesh_quantization",
            "KHR_texture_basisu",
        };
        for (const char* candidate : supported) {
            if (std::strcmp(extension, candidate) == 0)
                return true;
        }
        return false;
    }

    bool validate_required_extensions(const cgltf_data* data, const char* path, termin_glb_error* error) {
        for (cgltf_size index = 0; index < data->extensions_required_count; ++index) {
            const char* extension = data->extensions_required[index];
            if (!extension || !is_supported_required_extension(extension)) {
                set_error(error,
                          TERMIN_GLB_ERROR_UNSUPPORTED,
                          "%s: required glTF extension '%s' is not supported by the native adapter",
                          path,
                          extension ? extension : "<null>");
                return false;
            }
        }
        return true;
    }

    template <typename T>
    bool pointer_index(const T* pointer, const T* base, size_t count, size_t* index) {
        if (!pointer || !base || pointer < base || pointer >= base + count)
            return false;
        *index = static_cast<size_t>(pointer - base);
        return true;
    }

    const cgltf_image* selected_image(const cgltf_texture& texture) {
        if (texture.has_webp)
            return texture.webp_image;
        if (texture.has_basisu)
            return texture.basisu_image;
        return texture.image;
    }

    const unsigned char* buffer_view_data(const cgltf_buffer_view* view) {
        if (!view)
            return nullptr;
        if (view->data)
            return static_cast<const unsigned char*>(view->data);
        if (!view->buffer || !view->buffer->data)
            return nullptr;
        return static_cast<const unsigned char*>(view->buffer->data) + view->offset;
    }

    int resolved_filter(cgltf_filter_type value) {
        return value == cgltf_filter_type_undefined ? 9729 : static_cast<int>(value);
    }

    int resolved_wrap(cgltf_wrap_mode value) {
        return value == static_cast<cgltf_wrap_mode>(0) ? 10497 : static_cast<int>(value);
    }

    termin_glb_texture_view_info texture_view_info(const cgltf_data* data, const cgltf_texture_view& view) {
        termin_glb_texture_view_info info = {};
        info.present = pointer_index(view.texture, data->textures, data->textures_count, &info.texture_index);
        info.texcoord = view.texcoord;
        info.scale = view.scale;
        info.has_transform = view.has_transform;
        return info;
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

    bool validate_rig_accessors(cgltf_data* data, const char* path, termin_glb_error* error) {
        for (cgltf_size skin_index = 0; skin_index < data->skins_count; ++skin_index) {
            const cgltf_skin& skin = data->skins[skin_index];
            if (skin.inverse_bind_matrices &&
                (!accessor_storage_is_valid(skin.inverse_bind_matrices) ||
                 skin.inverse_bind_matrices->type != cgltf_type_mat4 ||
                 skin.inverse_bind_matrices->count != skin.joints_count)) {
                set_error(error,
                          TERMIN_GLB_ERROR_INVALID_FORMAT,
                          "%s: skin[%zu] has invalid inverse bind matrices",
                          path,
                          static_cast<size_t>(skin_index));
                return false;
            }
        }
        for (cgltf_size animation_index = 0; animation_index < data->animations_count; ++animation_index) {
            const cgltf_animation& animation = data->animations[animation_index];
            for (cgltf_size sampler_index = 0; sampler_index < animation.samplers_count; ++sampler_index) {
                const cgltf_animation_sampler& sampler = animation.samplers[sampler_index];
                if (!sampler.input || !sampler.output || !accessor_storage_is_valid(sampler.input) ||
                    !accessor_storage_is_valid(sampler.output) || sampler.input->type != cgltf_type_scalar) {
                    set_error(error,
                              TERMIN_GLB_ERROR_INVALID_FORMAT,
                              "%s: animation[%zu] sampler[%zu] has invalid input/output accessors",
                              path,
                              static_cast<size_t>(animation_index),
                              static_cast<size_t>(sampler_index));
                    return false;
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
    std::vector<std::string> fallback_image_names;
    std::vector<std::string> fallback_texture_names;
    std::vector<std::string> fallback_material_names;
    std::vector<std::string> fallback_node_names;
    std::vector<std::string> fallback_skin_names;
    std::vector<std::string> fallback_animation_names;

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
    if (!validate_required_extensions(document->data, document->path.c_str(), error)) {
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
    if (!validate_rig_accessors(document->data, document->path.c_str(), error)) {
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
        document->fallback_image_names.reserve(document->data->images_count);
        document->fallback_texture_names.reserve(document->data->textures_count);
        document->fallback_material_names.reserve(document->data->materials_count);
        document->fallback_node_names.reserve(document->data->nodes_count);
        document->fallback_skin_names.reserve(document->data->skins_count);
        document->fallback_animation_names.reserve(document->data->animations_count);
        for (cgltf_size i = 0; i < document->data->meshes_count; ++i)
            document->fallback_mesh_names.emplace_back("Mesh_" + std::to_string(i));
        for (cgltf_size i = 0; i < document->data->images_count; ++i)
            document->fallback_image_names.emplace_back("Image_" + std::to_string(i));
        for (cgltf_size i = 0; i < document->data->textures_count; ++i)
            document->fallback_texture_names.emplace_back("Texture_" + std::to_string(i));
        for (cgltf_size i = 0; i < document->data->materials_count; ++i)
            document->fallback_material_names.emplace_back("Material_" + std::to_string(i));
        for (cgltf_size i = 0; i < document->data->nodes_count; ++i)
            document->fallback_node_names.emplace_back("Node_" + std::to_string(i));
        for (cgltf_size i = 0; i < document->data->skins_count; ++i)
            document->fallback_skin_names.emplace_back("Skin_" + std::to_string(i));
        for (cgltf_size i = 0; i < document->data->animations_count; ++i)
            document->fallback_animation_names.emplace_back("Animation_" + std::to_string(i));
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
    bool any_skinned = false;
    bool any_static = false;
    for (cgltf_size primitive_index = 0; primitive_index < mesh.primitives_count; ++primitive_index) {
        const cgltf_primitive& primitive = mesh.primitives[primitive_index];
        const bool has_joints = find_attribute(primitive, cgltf_attribute_type_joints) != nullptr;
        const bool has_weights = find_attribute(primitive, cgltf_attribute_type_weights) != nullptr;
        if (has_joints != has_weights) {
            set_error(error,
                      TERMIN_GLB_ERROR_INVALID_FORMAT,
                      "%s: mesh[%zu] primitive[%zu] must provide JOINTS_0 and WEIGHTS_0 together",
                      document->path.c_str(),
                      mesh_index,
                      static_cast<size_t>(primitive_index));
            return false;
        }
        any_skinned = any_skinned || has_joints;
        any_static = any_static || !has_joints;
        size_t primitive_vertices = 0;
        size_t primitive_indices = 0;
        if (!primitive_counts(primitive, &primitive_vertices, &primitive_indices)) {
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

    if (any_skinned && any_static) {
        set_error(error,
                  TERMIN_GLB_ERROR_UNSUPPORTED,
                  "%s: mesh[%zu] mixes skinned and static primitives",
                  document->path.c_str(),
                  mesh_index);
        return false;
    }

    info->name = mesh.name ? mesh.name : document->fallback_mesh_names[mesh_index].c_str();
    info->primitive_count = mesh.primitives_count;
    info->vertex_count = vertex_count;
    info->index_count = index_count;
    info->skinned = any_skinned;
    return true;
}

size_t termin_glb_document_image_count(const termin_glb_document* document) {
    return document && document->data ? document->data->images_count : 0;
}

bool termin_glb_document_image_info(const termin_glb_document* document,
                                    size_t image_index,
                                    termin_glb_image_info* info,
                                    termin_glb_error* error) {
    termin_glb_error_clear(error);
    if (!document || !document->data || !info) {
        set_error(error, TERMIN_GLB_ERROR_INTERNAL, "image discovery requires a document and output pointer");
        return false;
    }
    if (image_index >= document->data->images_count) {
        set_error(error,
                  TERMIN_GLB_ERROR_INVALID_FORMAT,
                  "%s: image index %zu is out of range (count=%zu)",
                  document->path.c_str(),
                  image_index,
                  static_cast<size_t>(document->data->images_count));
        return false;
    }
    const cgltf_image& image = document->data->images[image_index];
    info->name = image.name ? image.name : document->fallback_image_names[image_index].c_str();
    info->has_name = image.name != nullptr;
    info->mime_type = image.mime_type ? image.mime_type : "";
    info->uri = image.uri ? image.uri : "";
    info->embedded = image.buffer_view != nullptr;
    info->encoded_size = image.buffer_view ? image.buffer_view->size : 0;
    return true;
}

bool termin_glb_document_image_payload(const termin_glb_document* document,
                                       size_t image_index,
                                       const unsigned char** data,
                                       size_t* size,
                                       termin_glb_error* error) {
    termin_glb_error_clear(error);
    if (!document || !document->data || !data || !size) {
        set_error(error, TERMIN_GLB_ERROR_INTERNAL, "image payload requires a document and output pointers");
        return false;
    }
    if (image_index >= document->data->images_count) {
        set_error(error,
                  TERMIN_GLB_ERROR_INVALID_FORMAT,
                  "%s: image index %zu is out of range (count=%zu)",
                  document->path.c_str(),
                  image_index,
                  static_cast<size_t>(document->data->images_count));
        return false;
    }
    const cgltf_image& image = document->data->images[image_index];
    if (!image.buffer_view) {
        set_error(error,
                  TERMIN_GLB_ERROR_UNSUPPORTED,
                  "%s: image[%zu] '%s' uses URI '%s'; native GLB image payloads currently require an embedded bufferView",
                  document->path.c_str(),
                  image_index,
                  image.name ? image.name : document->fallback_image_names[image_index].c_str(),
                  image.uri ? image.uri : "");
        return false;
    }
    const unsigned char* payload = buffer_view_data(image.buffer_view);
    if (!payload || image.buffer_view->size == 0 ||
        !buffer_view_range_is_valid(image.buffer_view, 0, image.buffer_view->size)) {
        set_error(error,
                  TERMIN_GLB_ERROR_INVALID_FORMAT,
                  "%s: image[%zu] '%s' embedded bufferView is empty or invalid",
                  document->path.c_str(),
                  image_index,
                  image.name ? image.name : document->fallback_image_names[image_index].c_str());
        return false;
    }
    *data = payload;
    *size = image.buffer_view->size;
    return true;
}

size_t termin_glb_document_texture_count(const termin_glb_document* document) {
    return document && document->data ? document->data->textures_count : 0;
}

bool termin_glb_document_texture_info(const termin_glb_document* document,
                                      size_t texture_index,
                                      termin_glb_texture_info* info,
                                      termin_glb_error* error) {
    termin_glb_error_clear(error);
    if (!document || !document->data || !info) {
        set_error(error, TERMIN_GLB_ERROR_INTERNAL, "texture discovery requires a document and output pointer");
        return false;
    }
    if (texture_index >= document->data->textures_count) {
        set_error(error,
                  TERMIN_GLB_ERROR_INVALID_FORMAT,
                  "%s: texture index %zu is out of range (count=%zu)",
                  document->path.c_str(),
                  texture_index,
                  static_cast<size_t>(document->data->textures_count));
        return false;
    }
    const cgltf_texture& texture = document->data->textures[texture_index];
    const cgltf_image* image = selected_image(texture);
    *info = {};
    info->name = texture.name ? texture.name : document->fallback_texture_names[texture_index].c_str();
    info->has_name = texture.name != nullptr;
    info->has_image = pointer_index(image, document->data->images, document->data->images_count, &info->image_index);
    if (!info->has_image) {
        set_error(error,
                  TERMIN_GLB_ERROR_INVALID_FORMAT,
                  "%s: texture[%zu] '%s' has no valid selected image",
                  document->path.c_str(),
                  texture_index,
                  info->name);
        return false;
    }
    info->has_sampler = pointer_index(
        texture.sampler, document->data->samplers, document->data->samplers_count, &info->sampler_index);
    info->selected_webp = texture.has_webp;
    info->selected_basisu = !texture.has_webp && texture.has_basisu;
    info->mag_filter = resolved_filter(texture.sampler ? texture.sampler->mag_filter : cgltf_filter_type_undefined);
    info->min_filter = resolved_filter(texture.sampler ? texture.sampler->min_filter : cgltf_filter_type_undefined);
    info->wrap_s = resolved_wrap(texture.sampler ? texture.sampler->wrap_s : static_cast<cgltf_wrap_mode>(0));
    info->wrap_t = resolved_wrap(texture.sampler ? texture.sampler->wrap_t : static_cast<cgltf_wrap_mode>(0));
    return true;
}

size_t termin_glb_document_material_count(const termin_glb_document* document) {
    return document && document->data ? document->data->materials_count : 0;
}

bool termin_glb_document_material_info(const termin_glb_document* document,
                                       size_t material_index,
                                       termin_glb_material_info* info,
                                       termin_glb_error* error) {
    termin_glb_error_clear(error);
    if (!document || !document->data || !info) {
        set_error(error, TERMIN_GLB_ERROR_INTERNAL, "material discovery requires a document and output pointer");
        return false;
    }
    if (material_index >= document->data->materials_count) {
        set_error(error,
                  TERMIN_GLB_ERROR_INVALID_FORMAT,
                  "%s: material index %zu is out of range (count=%zu)",
                  document->path.c_str(),
                  material_index,
                  static_cast<size_t>(document->data->materials_count));
        return false;
    }
    const cgltf_material& material = document->data->materials[material_index];
    const cgltf_pbr_metallic_roughness& pbr = material.pbr_metallic_roughness;
    *info = {};
    info->name = material.name ? material.name : document->fallback_material_names[material_index].c_str();
    std::memcpy(info->base_color_factor, pbr.base_color_factor, sizeof(info->base_color_factor));
    info->metallic_factor = pbr.metallic_factor;
    info->roughness_factor = pbr.roughness_factor;
    info->base_color_texture = texture_view_info(document->data, pbr.base_color_texture);
    info->metallic_roughness_texture = texture_view_info(document->data, pbr.metallic_roughness_texture);
    info->normal_texture = texture_view_info(document->data, material.normal_texture);
    info->occlusion_texture = texture_view_info(document->data, material.occlusion_texture);
    info->emissive_texture = texture_view_info(document->data, material.emissive_texture);
    std::memcpy(info->emissive_factor, material.emissive_factor, sizeof(info->emissive_factor));
    info->alpha_mode = static_cast<int>(material.alpha_mode);
    info->alpha_cutoff = material.alpha_cutoff;
    info->double_sided = material.double_sided;
    info->unlit = material.unlit;
    info->ior = material.has_ior ? material.ior.ior : 1.5f;
    info->specular_factor = material.has_specular ? material.specular.specular_factor : 1.0f;
    const float default_specular_color[3] = {1.0f, 1.0f, 1.0f};
    std::memcpy(info->specular_color_factor,
                material.has_specular ? material.specular.specular_color_factor : default_specular_color,
                sizeof(info->specular_color_factor));
    return true;
}

size_t termin_glb_document_node_count(const termin_glb_document* document) {
    return document && document->data ? document->data->nodes_count : 0;
}

bool termin_glb_document_node_info(const termin_glb_document* document,
                                   size_t node_index,
                                   termin_glb_node_info* info,
                                   termin_glb_error* error) {
    termin_glb_error_clear(error);
    if (!document || !document->data || !info) {
        set_error(error, TERMIN_GLB_ERROR_INTERNAL, "node discovery requires a document and output pointer");
        return false;
    }
    if (node_index >= document->data->nodes_count) {
        set_error(error,
                  TERMIN_GLB_ERROR_INVALID_FORMAT,
                  "%s: node index %zu is out of range (count=%zu)",
                  document->path.c_str(),
                  node_index,
                  static_cast<size_t>(document->data->nodes_count));
        return false;
    }
    const cgltf_node& node = document->data->nodes[node_index];
    *info = {};
    info->name = node.name ? node.name : document->fallback_node_names[node_index].c_str();
    info->has_parent = pointer_index(node.parent, document->data->nodes, document->data->nodes_count, &info->parent_index);
    info->has_mesh = pointer_index(node.mesh, document->data->meshes, document->data->meshes_count, &info->mesh_index);
    info->has_skin = pointer_index(node.skin, document->data->skins, document->data->skins_count, &info->skin_index);
    const cgltf_scene* scene = document->data->scene;
    if (!scene && document->data->scenes_count > 0)
        scene = &document->data->scenes[0];
    if (scene) {
        for (cgltf_size root_index = 0; root_index < scene->nodes_count; ++root_index)
            info->default_scene_root = info->default_scene_root || scene->nodes[root_index] == &node;
    }
    info->has_matrix = node.has_matrix;
    std::memcpy(info->translation, node.translation, sizeof(info->translation));
    std::memcpy(info->rotation, node.rotation, sizeof(info->rotation));
    std::memcpy(info->scale, node.scale, sizeof(info->scale));
    std::memcpy(info->matrix, node.matrix, sizeof(info->matrix));
    return true;
}

size_t termin_glb_document_skin_count(const termin_glb_document* document) {
    return document && document->data ? document->data->skins_count : 0;
}

bool termin_glb_document_skin_info(const termin_glb_document* document,
                                   size_t skin_index,
                                   termin_glb_skin_info* info,
                                   termin_glb_error* error) {
    termin_glb_error_clear(error);
    if (!document || !document->data || !info) {
        set_error(error, TERMIN_GLB_ERROR_INTERNAL, "skin discovery requires a document and output pointer");
        return false;
    }
    if (skin_index >= document->data->skins_count) {
        set_error(error,
                  TERMIN_GLB_ERROR_INVALID_FORMAT,
                  "%s: skin index %zu is out of range (count=%zu)",
                  document->path.c_str(),
                  skin_index,
                  static_cast<size_t>(document->data->skins_count));
        return false;
    }
    const cgltf_skin& skin = document->data->skins[skin_index];
    *info = {};
    info->name = skin.name ? skin.name : document->fallback_skin_names[skin_index].c_str();
    info->joint_count = skin.joints_count;
    info->has_skeleton = pointer_index(
        skin.skeleton, document->data->nodes, document->data->nodes_count, &info->skeleton_node_index);
    info->has_inverse_bind_matrices = skin.inverse_bind_matrices != nullptr;
    return true;
}

bool termin_glb_document_skin_joints(const termin_glb_document* document,
                                     size_t skin_index,
                                     size_t* node_indices,
                                     size_t node_index_count,
                                     termin_glb_error* error) {
    termin_glb_skin_info info = {};
    if (!termin_glb_document_skin_info(document, skin_index, &info, error))
        return false;
    if (node_index_count != info.joint_count || (node_index_count > 0 && !node_indices)) {
        set_error(error,
                  TERMIN_GLB_ERROR_INTERNAL,
                  "%s: skin[%zu] joint output size mismatch (expected=%zu actual=%zu)",
                  document->path.c_str(),
                  skin_index,
                  info.joint_count,
                  node_index_count);
        return false;
    }
    const cgltf_skin& skin = document->data->skins[skin_index];
    for (size_t joint_index = 0; joint_index < node_index_count; ++joint_index) {
        if (!pointer_index(skin.joints[joint_index],
                           document->data->nodes,
                           document->data->nodes_count,
                           &node_indices[joint_index])) {
            set_error(error,
                      TERMIN_GLB_ERROR_INVALID_FORMAT,
                      "%s: skin[%zu] joint[%zu] has an invalid node reference",
                      document->path.c_str(),
                      skin_index,
                      joint_index);
            return false;
        }
    }
    return true;
}

bool termin_glb_document_skin_inverse_bind_matrices(const termin_glb_document* document,
                                                    size_t skin_index,
                                                    float* matrices,
                                                    size_t float_count,
                                                    termin_glb_error* error) {
    termin_glb_skin_info info = {};
    if (!termin_glb_document_skin_info(document, skin_index, &info, error))
        return false;
    size_t expected = 0;
    if (!checked_mul(info.joint_count, size_t{16}, &expected) || float_count != expected ||
        (float_count > 0 && !matrices)) {
        set_error(error,
                  TERMIN_GLB_ERROR_INTERNAL,
                  "%s: skin[%zu] inverse-bind output size mismatch (expected=%zu actual=%zu)",
                  document->path.c_str(),
                  skin_index,
                  expected,
                  float_count);
        return false;
    }
    const cgltf_accessor* accessor = document->data->skins[skin_index].inverse_bind_matrices;
    for (size_t joint_index = 0; joint_index < info.joint_count; ++joint_index) {
        float source[16] = {};
        if (accessor && !cgltf_accessor_read_float(accessor, joint_index, source, 16)) {
            set_error(error,
                      TERMIN_GLB_ERROR_INVALID_FORMAT,
                      "%s: skin[%zu] cannot read inverse bind matrix[%zu]",
                      document->path.c_str(),
                      skin_index,
                      joint_index);
            return false;
        }
        float* destination = matrices + joint_index * 16;
        if (!accessor) {
            destination[0] = destination[5] = destination[10] = destination[15] = 1.0f;
            continue;
        }
        std::memcpy(destination, source, sizeof(source));
    }
    return true;
}

size_t termin_glb_document_animation_count(const termin_glb_document* document) {
    return document && document->data ? document->data->animations_count : 0;
}

bool termin_glb_document_animation_info(const termin_glb_document* document,
                                        size_t animation_index,
                                        termin_glb_animation_info* info,
                                        termin_glb_error* error) {
    termin_glb_error_clear(error);
    if (!document || !document->data || !info) {
        set_error(error, TERMIN_GLB_ERROR_INTERNAL, "animation discovery requires a document and output pointer");
        return false;
    }
    if (animation_index >= document->data->animations_count) {
        set_error(error,
                  TERMIN_GLB_ERROR_INVALID_FORMAT,
                  "%s: animation index %zu is out of range (count=%zu)",
                  document->path.c_str(),
                  animation_index,
                  static_cast<size_t>(document->data->animations_count));
        return false;
    }
    const cgltf_animation& animation = document->data->animations[animation_index];
    *info = {};
    info->name = animation.name ? animation.name : document->fallback_animation_names[animation_index].c_str();
    info->sampler_count = animation.samplers_count;
    info->channel_count = animation.channels_count;
    return true;
}

bool termin_glb_document_animation_sampler_info(const termin_glb_document* document,
                                                size_t animation_index,
                                                size_t sampler_index,
                                                termin_glb_animation_sampler_info* info,
                                                termin_glb_error* error) {
    termin_glb_animation_info animation_info = {};
    if (!termin_glb_document_animation_info(document, animation_index, &animation_info, error))
        return false;
    if (!info || sampler_index >= animation_info.sampler_count) {
        set_error(error,
                  TERMIN_GLB_ERROR_INVALID_FORMAT,
                  "%s: animation[%zu] sampler index %zu is out of range (count=%zu)",
                  document->path.c_str(),
                  animation_index,
                  sampler_index,
                  animation_info.sampler_count);
        return false;
    }
    const cgltf_animation_sampler& sampler = document->data->animations[animation_index].samplers[sampler_index];
    const size_t components = cgltf_num_components(sampler.output->type);
    size_t output_float_count = 0;
    if (components == 0 || !checked_mul(sampler.output->count, components, &output_float_count)) {
        set_error(error,
                  TERMIN_GLB_ERROR_INVALID_FORMAT,
                  "%s: animation[%zu] sampler[%zu] output size overflows",
                  document->path.c_str(),
                  animation_index,
                  sampler_index);
        return false;
    }
    *info = {};
    info->input_count = sampler.input->count;
    info->output_float_count = output_float_count;
    info->output_components = components;
    info->interpolation = static_cast<int>(sampler.interpolation);
    return true;
}

bool termin_glb_document_animation_sampler_payload(const termin_glb_document* document,
                                                   size_t animation_index,
                                                   size_t sampler_index,
                                                   float* input,
                                                   size_t input_count,
                                                   float* output,
                                                   size_t output_float_count,
                                                   termin_glb_error* error) {
    termin_glb_animation_sampler_info info = {};
    if (!termin_glb_document_animation_sampler_info(document, animation_index, sampler_index, &info, error))
        return false;
    if (input_count != info.input_count || output_float_count != info.output_float_count ||
        (input_count > 0 && !input) || (output_float_count > 0 && !output)) {
        set_error(error,
                  TERMIN_GLB_ERROR_INTERNAL,
                  "%s: animation[%zu] sampler[%zu] payload size mismatch",
                  document->path.c_str(),
                  animation_index,
                  sampler_index);
        return false;
    }
    const cgltf_animation_sampler& sampler = document->data->animations[animation_index].samplers[sampler_index];
    for (size_t key_index = 0; key_index < input_count; ++key_index) {
        if (!cgltf_accessor_read_float(sampler.input, key_index, input + key_index, 1)) {
            set_error(error,
                      TERMIN_GLB_ERROR_INVALID_FORMAT,
                      "%s: animation[%zu] sampler[%zu] cannot read input[%zu]",
                      document->path.c_str(),
                      animation_index,
                      sampler_index,
                      key_index);
            return false;
        }
    }
    const size_t output_count = sampler.output->count;
    for (size_t value_index = 0; value_index < output_count; ++value_index) {
        if (!cgltf_accessor_read_float(sampler.output,
                                       value_index,
                                       output + value_index * info.output_components,
                                       info.output_components)) {
            set_error(error,
                      TERMIN_GLB_ERROR_INVALID_FORMAT,
                      "%s: animation[%zu] sampler[%zu] cannot read output[%zu]",
                      document->path.c_str(),
                      animation_index,
                      sampler_index,
                      value_index);
            return false;
        }
    }
    return true;
}

bool termin_glb_document_animation_channel_info(const termin_glb_document* document,
                                                size_t animation_index,
                                                size_t channel_index,
                                                termin_glb_animation_channel_info* info,
                                                termin_glb_error* error) {
    termin_glb_animation_info animation_info = {};
    if (!termin_glb_document_animation_info(document, animation_index, &animation_info, error))
        return false;
    if (!info || channel_index >= animation_info.channel_count) {
        set_error(error,
                  TERMIN_GLB_ERROR_INVALID_FORMAT,
                  "%s: animation[%zu] channel index %zu is out of range (count=%zu)",
                  document->path.c_str(),
                  animation_index,
                  channel_index,
                  animation_info.channel_count);
        return false;
    }
    const cgltf_animation& animation = document->data->animations[animation_index];
    const cgltf_animation_channel& channel = animation.channels[channel_index];
    *info = {};
    if (!pointer_index(channel.sampler, animation.samplers, animation.samplers_count, &info->sampler_index)) {
        set_error(error,
                  TERMIN_GLB_ERROR_INVALID_FORMAT,
                  "%s: animation[%zu] channel[%zu] has an invalid sampler reference",
                  document->path.c_str(),
                  animation_index,
                  channel_index);
        return false;
    }
    info->has_target_node = pointer_index(
        channel.target_node, document->data->nodes, document->data->nodes_count, &info->target_node_index);
    info->target_path = static_cast<int>(channel.target_path);
    return true;
}

bool termin_glb_document_build_mesh(termin_glb_document* document,
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
    bool is_skinned = false;
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
        const cgltf_accessor* joints = find_attribute(primitive, cgltf_attribute_type_joints);
        const cgltf_accessor* weights = find_attribute(primitive, cgltf_attribute_type_weights);
        if ((joints != nullptr) != (weights != nullptr)) {
            set_error(error,
                      TERMIN_GLB_ERROR_INVALID_FORMAT,
                      "%s: mesh[%zu] '%s' primitive[%zu] must provide JOINTS_0 and WEIGHTS_0 together",
                      document->path.c_str(),
                      mesh_index,
                      info.name,
                      static_cast<size_t>(primitive_index));
            return false;
        }
        if ((joints != nullptr) != info.skinned) {
            set_error(error,
                      TERMIN_GLB_ERROR_UNSUPPORTED,
                      "%s: mesh[%zu] '%s' mixes skinned and static primitives",
                      document->path.c_str(),
                      mesh_index,
                      info.name);
            return false;
        }
        is_skinned = is_skinned || joints != nullptr;
        has_normals = has_normals || find_attribute(primitive, cgltf_attribute_type_normal);
        has_texcoords = has_texcoords || find_attribute(primitive, cgltf_attribute_type_texcoord);
        has_tangents = has_tangents || find_attribute(primitive, cgltf_attribute_type_tangent);
    }

    tc_vertex_layout layout = {};
    // The production PBR path declares a tangent input. Match the legacy GLB
    // adapter by deriving tangents whenever UVs are available, even when the
    // source file omits the optional TANGENT accessor.
    if (is_skinned)
        layout = tc_vertex_layout_skinned();
    else if (has_tangents || has_texcoords)
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
            const cgltf_accessor* joints = find_attribute(primitive, cgltf_attribute_type_joints);
            const cgltf_accessor* weights = find_attribute(primitive, cgltf_attribute_type_weights);

            if (positions->type != cgltf_type_vec3 ||
                (normals && !accessor_shape_is(normals, cgltf_type_vec3, vertex_count)) ||
                (texcoords && !accessor_shape_is(texcoords, cgltf_type_vec2, vertex_count)) ||
                (tangents && !accessor_shape_is(tangents, cgltf_type_vec4, vertex_count)) ||
                (joints && !accessor_shape_is(joints, cgltf_type_vec4, vertex_count)) ||
                (weights && !accessor_shape_is(weights, cgltf_type_vec4, vertex_count))) {
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
                if (joints && !read_attribute(joints,
                                               vertex_index,
                                               destination + 12,
                                               4,
                                               document->path.c_str(),
                                               mesh_index,
                                               primitive_index,
                                               "JOINTS_0",
                                               error))
                    goto cleanup;
                if (weights && !read_attribute(weights,
                                                vertex_index,
                                                destination + 16,
                                                4,
                                                document->path.c_str(),
                                                mesh_index,
                                                primitive_index,
                                                "WEIGHTS_0",
                                                error))
                    goto cleanup;
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

            const bool tangent_layout = is_skinned || has_tangents || has_texcoords;
            if (!is_skinned && tangent_layout && !normals) {
                for (size_t local_index = 0; local_index + 2 < primitive_index_count; local_index += 3) {
                    const uint32_t i0 = builder.indices[index_base + local_index];
                    const uint32_t i1 = builder.indices[index_base + local_index + 1];
                    const uint32_t i2 = builder.indices[index_base + local_index + 2];
                    float* v0 = reinterpret_cast<float*>(static_cast<unsigned char*>(builder.vertices) +
                                                         static_cast<size_t>(i0) * layout.stride);
                    float* v1 = reinterpret_cast<float*>(static_cast<unsigned char*>(builder.vertices) +
                                                         static_cast<size_t>(i1) * layout.stride);
                    float* v2 = reinterpret_cast<float*>(static_cast<unsigned char*>(builder.vertices) +
                                                         static_cast<size_t>(i2) * layout.stride);
                    add_cross(v0 + 3, v0, v1, v2);
                    add_cross(v1 + 3, v0, v1, v2);
                    add_cross(v2 + 3, v0, v1, v2);
                }
                for (size_t vertex_index = 0; vertex_index < vertex_count; ++vertex_index) {
                    float* destination = reinterpret_cast<float*>(
                        static_cast<unsigned char*>(builder.vertices) +
                        (vertex_base + vertex_index) * layout.stride);
                    normalize3(destination + 3);
                }
            }

            if (!is_skinned && tangent_layout && !tangents) {
                std::vector<float> tangent_1(vertex_count * 3, 0.0f);
                std::vector<float> tangent_2(vertex_count * 3, 0.0f);
                for (size_t local_index = 0; local_index + 2 < primitive_index_count; local_index += 3) {
                    const size_t i0 = builder.indices[index_base + local_index] - vertex_base;
                    const size_t i1 = builder.indices[index_base + local_index + 1] - vertex_base;
                    const size_t i2 = builder.indices[index_base + local_index + 2] - vertex_base;
                    const float* v0 = reinterpret_cast<const float*>(
                        static_cast<const unsigned char*>(builder.vertices) + (vertex_base + i0) * layout.stride);
                    const float* v1 = reinterpret_cast<const float*>(
                        static_cast<const unsigned char*>(builder.vertices) + (vertex_base + i1) * layout.stride);
                    const float* v2 = reinterpret_cast<const float*>(
                        static_cast<const unsigned char*>(builder.vertices) + (vertex_base + i2) * layout.stride);
                    const float x1 = v1[0] - v0[0];
                    const float x2 = v2[0] - v0[0];
                    const float y1 = v1[1] - v0[1];
                    const float y2 = v2[1] - v0[1];
                    const float z1 = v1[2] - v0[2];
                    const float z2 = v2[2] - v0[2];
                    const float s1 = v1[6] - v0[6];
                    const float s2 = v2[6] - v0[6];
                    const float t1 = v1[7] - v0[7];
                    const float t2 = v2[7] - v0[7];
                    const float determinant = s1 * t2 - s2 * t1;
                    if (std::fabs(determinant) <= 1e-8f)
                        continue;
                    const float reciprocal = 1.0f / determinant;
                    const float sdir[3] = {
                        (t2 * x1 - t1 * x2) * reciprocal,
                        (t2 * y1 - t1 * y2) * reciprocal,
                        (t2 * z1 - t1 * z2) * reciprocal,
                    };
                    const float tdir[3] = {
                        (s1 * x2 - s2 * x1) * reciprocal,
                        (s1 * y2 - s2 * y1) * reciprocal,
                        (s1 * z2 - s2 * z1) * reciprocal,
                    };
                    for (const size_t index : {i0, i1, i2}) {
                        for (size_t component = 0; component < 3; ++component) {
                            tangent_1[index * 3 + component] += sdir[component];
                            tangent_2[index * 3 + component] += tdir[component];
                        }
                    }
                }
                for (size_t vertex_index = 0; vertex_index < vertex_count; ++vertex_index) {
                    float* destination = reinterpret_cast<float*>(
                        static_cast<unsigned char*>(builder.vertices) +
                        (vertex_base + vertex_index) * layout.stride);
                    const float* normal = destination + 3;
                    const float* accumulated = tangent_1.data() + vertex_index * 3;
                    const float projection = dot3(normal, accumulated);
                    float tangent[3] = {
                        accumulated[0] - normal[0] * projection,
                        accumulated[1] - normal[1] * projection,
                        accumulated[2] - normal[2] * projection,
                    };
                    normalize3(tangent);
                    if (dot3(tangent, tangent) <= 1e-8f) {
                        const float reference[3] = {
                            0.0f,
                            std::fabs(normal[2]) > 0.9f ? 1.0f : 0.0f,
                            std::fabs(normal[2]) > 0.9f ? 0.0f : 1.0f,
                        };
                        cross3(reference, normal, tangent);
                        normalize3(tangent);
                    }
                    destination[8] = tangent[0];
                    destination[9] = tangent[1];
                    destination[10] = tangent[2];
                    float bitangent[3] = {};
                    cross3(normal, tangent, bitangent);
                    destination[11] = dot3(bitangent, tangent_2.data() + vertex_index * 3) < 0.0f ? -1.0f : 1.0f;
                }
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

bool termin_glb_document_build_static_mesh(termin_glb_document* document,
                                           size_t mesh_index,
                                           const char* mesh_uuid,
                                           const char* mesh_name,
                                           bool convert_to_z_up,
                                           termin_glb_error* error) {
    return termin_glb_document_build_mesh(
        document, mesh_index, mesh_uuid, mesh_name, convert_to_z_up, error);
}
