#pragma once

// TcShader - RAII wrapper with handle-based access to tc_shader
// Uses tc_shader_handle with generation checking for safety
// Includes GL methods via tc_gpu: ensure_ready, use, set_uniform_*

extern "C" {
#include <tgfx/resources/tc_shader.h>
#include <tgfx/resources/tc_shader_registry.h>
#include <tgfx/tc_gpu_context.h>
#include <tgfx/tgfx_resource_gpu.h>
#include <tcbase/tc_log.h>
}

#include <string>
#include <cstring>

namespace termin {

// TcShader - shader wrapper with registry integration
// Stores handle (index + generation) instead of raw pointer
class TcShader {
public:
    tc_shader_handle handle = tc_shader_handle_invalid();

    TcShader() = default;

    explicit TcShader(tc_shader_handle h) : handle(h) {
        if (tc_shader* s = tc_shader_get(handle)) {
            tc_shader_add_ref(s);
        }
    }

    // Construct from raw pointer (finds handle for it)
    explicit TcShader(tc_shader* s) {
        if (s) {
            handle = tc_shader_find(s->uuid);
            tc_shader_add_ref(s);
        }
    }

    TcShader(const TcShader& other) : handle(other.handle) {
        if (tc_shader* s = tc_shader_get(handle)) {
            tc_shader_add_ref(s);
        }
    }

    TcShader(TcShader&& other) noexcept : handle(other.handle) {
        other.handle = tc_shader_handle_invalid();
    }

    TcShader& operator=(const TcShader& other) {
        if (this != &other) {
            if (tc_shader* s = tc_shader_get(handle)) {
                tc_shader_release(s);
            }
            handle = other.handle;
            if (tc_shader* s = tc_shader_get(handle)) {
                tc_shader_add_ref(s);
            }
        }
        return *this;
    }

    TcShader& operator=(TcShader&& other) noexcept {
        if (this != &other) {
            if (tc_shader* s = tc_shader_get(handle)) {
                tc_shader_release(s);
            }
            handle = other.handle;
            other.handle = tc_shader_handle_invalid();
        }
        return *this;
    }

    ~TcShader() {
        if (tc_shader* s = tc_shader_get(handle)) {
            tc_shader_release(s);
        }
        handle = tc_shader_handle_invalid();
    }

    // Get raw pointer (may return nullptr if handle is stale)
    tc_shader* get() const { return tc_shader_get(handle); }

    // For backwards compatibility
    tc_shader* shader_ptr() const { return get(); }

    // Query (safe - returns defaults if handle is stale)
    bool is_valid() const { return tc_shader_is_valid(handle); }

    const char* uuid() const {
        tc_shader* s = get();
        return s ? s->uuid : "";
    }

    const char* name() const {
        tc_shader* s = get();
        return (s && s->name) ? s->name : "";
    }

    const char* source_path() const {
        tc_shader* s = get();
        return (s && s->source_path) ? s->source_path : "";
    }

    uint32_t version() const {
        tc_shader* s = get();
        return s ? s->version : 0;
    }

    const char* source_hash() const {
        tc_shader* s = get();
        return s ? s->source_hash : "";
    }

    // Source accessors
    const char* vertex_source() const {
        tc_shader* s = get();
        return (s && s->vertex_source) ? s->vertex_source : "";
    }

    const char* fragment_source() const {
        tc_shader* s = get();
        return (s && s->fragment_source) ? s->fragment_source : "";
    }

    const char* geometry_source() const {
        tc_shader* s = get();
        return (s && s->geometry_source) ? s->geometry_source : "";
    }

    bool has_geometry() const {
        tc_shader* s = get();
        return s && tc_shader_has_geometry(s);
    }

    // Features
    uint32_t features() const {
        tc_shader* s = get();
        return s ? s->features : 0;
    }

    bool has_feature(tc_shader_feature feature) const {
        tc_shader* s = get();
        return s && tc_shader_has_feature(s, feature);
    }

    void set_feature(tc_shader_feature feature) {
        tc_shader* s = get();
        if (s) tc_shader_set_feature(s, feature);
    }

    void clear_feature(tc_shader_feature feature) {
        tc_shader* s = get();
        if (s) tc_shader_clear_feature(s, feature);
    }

    void set_features(uint32_t features) {
        tc_shader* s = get();
        if (s) s->features = features;
    }

    // Variant info
    bool is_variant() const {
        tc_shader* s = get();
        return s && s->is_variant;
    }

    tc_shader_variant_op variant_op() const {
        tc_shader* s = get();
        return s ? static_cast<tc_shader_variant_op>(s->variant_op) : TC_SHADER_VARIANT_NONE;
    }

    TcShader original() const {
        tc_shader* s = get();
        if (!s || !s->is_variant) return TcShader();
        return TcShader(s->original_handle);
    }

    bool variant_is_stale() const {
        return tc_shader_variant_is_stale(handle);
    }

    void bump_version() {
        if (tc_shader* s = get()) {
            s->version++;
        }
    }

    // Set sources
    bool set_sources(
        const std::string& vertex,
        const std::string& fragment,
        const std::string& geometry = "",
        const std::string& shader_name = "",
        const std::string& src_path = ""
    ) {
        tc_shader* s = get();
        if (!s) return false;
        return tc_shader_set_sources(
            s,
            vertex.c_str(),
            fragment.c_str(),
            geometry.empty() ? nullptr : geometry.c_str(),
            shader_name.empty() ? nullptr : shader_name.c_str(),
            src_path.empty() ? nullptr : src_path.c_str()
        );
    }

    // Set variant info (called after creating variant shader)
    void set_variant_info(const TcShader& original, tc_shader_variant_op op) {
        tc_shader* s = get();
        if (s) {
            tc_shader_set_variant_info(s, original.handle, op);
        }
    }

    // Static factory methods

    // Create from sources (finds existing by hash or creates new)
    static TcShader from_sources(
        const std::string& vertex,
        const std::string& fragment,
        const std::string& geometry = "",
        const std::string& name = "",
        const std::string& source_path = ""
    ) {
        tc_shader_handle h = tc_shader_from_sources(
            vertex.c_str(),
            fragment.c_str(),
            geometry.empty() ? nullptr : geometry.c_str(),
            name.empty() ? nullptr : name.c_str(),
            source_path.empty() ? nullptr : source_path.c_str(),
            nullptr
        );
        if (tc_shader_handle_is_invalid(h)) {
            return TcShader();
        }
        return TcShader(h);
    }

    // Get by UUID from registry
    static TcShader from_uuid(const std::string& uuid) {
        tc_shader_handle h = tc_shader_find(uuid.c_str());
        if (tc_shader_handle_is_invalid(h)) {
            return TcShader();
        }
        return TcShader(h);
    }

    // Get by source hash
    static TcShader from_hash(const std::string& hash) {
        tc_shader_handle h = tc_shader_find_by_hash(hash.c_str());
        if (tc_shader_handle_is_invalid(h)) {
            return TcShader();
        }
        return TcShader(h);
    }

    // Get by name
    static TcShader from_name(const std::string& name) {
        tc_shader_handle h = tc_shader_find_by_name(name.c_str());
        if (tc_shader_handle_is_invalid(h)) {
            return TcShader();
        }
        return TcShader(h);
    }

    // Get or create by UUID
    static TcShader get_or_create(const std::string& uuid) {
        tc_shader_handle h = tc_shader_get_or_create(uuid.c_str());
        if (tc_shader_handle_is_invalid(h)) {
            return TcShader();
        }
        return TcShader(h);
    }

    // ========================================================================
    // GL operations (via tc_gpu)
    // ========================================================================

    // Compile shader if needed, returns GPU program ID (0 on failure)
    uint32_t compile() {
        tc_shader* s = get();
        return s ? tc_shader_compile_gpu(s) : 0;
    }

    // Use this shader (compiles if needed)
    void use() {
        tc_shader* s = get();
        if (!s) {
            tc_log(TC_LOG_ERROR, "TcShader::use(): get() returned NULL for handle %d:%d",
                   handle.index, handle.generation);
            return;
        }
        tc_shader_use_gpu(s);
    }

    // Ensure shader is ready (compiled with current sources)
    bool ensure_ready() {
        tc_shader* s = get();
        if (!s) return false;
        return tc_shader_compile_gpu(s) != 0;
    }

    // Get GPU program ID for current context (0 if not compiled)
    uint32_t gpu_program() const {
        tc_shader* s = get();
        if (!s) return 0;
        tc_gpu_context* ctx = tc_gpu_get_context();
        if (!ctx) return 0;
        tc_gpu_slot* slot = tc_gpu_context_shader_slot(ctx, s->pool_index);
        return slot ? slot->gl_id : 0;
    }

    // Uniform setters (shader must be in use)
    void set_uniform_int(const char* name, int value) {
        tc_shader* s = get();
        if (s) tc_shader_set_int(s, name, value);
    }

    void set_uniform_float(const char* name, float value) {
        tc_shader* s = get();
        if (s) tc_shader_set_float(s, name, value);
    }

    void set_uniform_vec2(const char* name, float x, float y) {
        tc_shader* s = get();
        if (s) tc_shader_set_vec2(s, name, x, y);
    }

    void set_uniform_vec3(const char* name, float x, float y, float z) {
        tc_shader* s = get();
        if (s) tc_shader_set_vec3(s, name, x, y, z);
    }

    void set_uniform_vec4(const char* name, float x, float y, float z, float w) {
        tc_shader* s = get();
        if (s) tc_shader_set_vec4(s, name, x, y, z, w);
    }

    void set_uniform_mat4(const char* name, const float* data, bool transpose = false) {
        tc_shader* s = get();
        if (s) tc_shader_set_mat4(s, name, data, transpose);
    }

    void set_uniform_mat4_array(const char* name, const float* data, int count, bool transpose = false) {
        tc_shader* s = get();
        if (s) tc_shader_set_mat4_array(s, name, data, count, transpose);
    }

    void set_block_binding(const char* block_name, int binding_point) {
        tc_shader* s = get();
        if (s) tc_shader_set_block_binding(s, block_name, binding_point);
    }
};

} // namespace termin
