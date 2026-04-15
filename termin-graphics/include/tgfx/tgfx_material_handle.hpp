#pragma once

extern "C" {
#include "tgfx/resources/tc_material.h"
#include "tgfx/resources/tc_material_registry.h"
#include "tgfx/resources/tc_shader_registry.h"
#include "tgfx/resources/tc_texture_registry.h"
#include "tgfx/tgfx_resource_gpu.h"
#include "inspect/tc_inspect.h"
}

#include <algorithm>
#include <cstring>
#include <optional>
#include <string>
#include <vector>

#include <tcbase/tgfx_intern_string.h>
#include <tcbase/tc_log.h>
#include <termin/geom/mat44.hpp>
#include <termin/geom/vec3.hpp>
#include <termin/geom/vec4.hpp>
#include <tgfx/tgfx_shader_handle.hpp>
#include <tgfx/tgfx_texture_handle.hpp>

namespace termin {

class TcMaterial {
public:
    tc_material_handle handle = tc_material_handle_invalid();

public:
    TcMaterial() = default;

    explicit TcMaterial(tc_material_handle h) : handle(h) {
        if (tc_material* m = tc_material_get(handle)) {
            tc_material_add_ref(m);
        }
    }

    explicit TcMaterial(tc_material* m) {
        if (m) {
            handle = tc_material_find(m->header.uuid);
            tc_material_add_ref(m);
        }
    }

    TcMaterial(const TcMaterial& other) : handle(other.handle) {
        if (tc_material* m = tc_material_get(handle)) {
            tc_material_add_ref(m);
        }
    }

    TcMaterial(TcMaterial&& other) noexcept : handle(other.handle) {
        other.handle = tc_material_handle_invalid();
    }

    TcMaterial& operator=(const TcMaterial& other) {
        if (this != &other) {
            if (tc_material* m = tc_material_get(handle)) {
                tc_material_release(m);
            }
            handle = other.handle;
            if (tc_material* m = tc_material_get(handle)) {
                tc_material_add_ref(m);
            }
        }
        return *this;
    }

    TcMaterial& operator=(TcMaterial&& other) noexcept {
        if (this != &other) {
            if (tc_material* m = tc_material_get(handle)) {
                tc_material_release(m);
            }
            handle = other.handle;
            other.handle = tc_material_handle_invalid();
        }
        return *this;
    }

    ~TcMaterial() {
        if (tc_material* m = tc_material_get(handle)) {
            tc_material_release(m);
        }
        handle = tc_material_handle_invalid();
    }

    tc_material* get() const { return tc_material_get(handle); }

    bool is_valid() const { return tc_material_is_valid(handle); }

    const char* uuid() const {
        tc_material* m = get();
        return m ? m->header.uuid : "";
    }

    const char* name() const {
        tc_material* m = get();
        return (m && m->header.name) ? m->header.name : "";
    }

    void set_name(const char* new_name) {
        tc_material* m = get();
        if (m) {
            m->header.name = tgfx_intern_string(new_name);
        }
    }

    uint32_t version() const {
        tc_material* m = get();
        return m ? m->header.version : 0;
    }

    void bump_version() {
        if (tc_material* m = get()) {
            m->header.version++;
        }
    }

    const char* shader_name() const {
        tc_material* m = get();
        return m ? m->shader_name : "";
    }

    void set_shader_name(const char* shader) {
        tc_material* m = get();
        if (m) {
            strncpy(m->shader_name, shader, TC_MATERIAL_NAME_MAX - 1);
            m->shader_name[TC_MATERIAL_NAME_MAX - 1] = '\0';
        }
    }

    const char* source_path() const {
        tc_material* m = get();
        return (m && m->source_path) ? m->source_path : "";
    }

    void set_source_path(const char* path) {
        tc_material* m = get();
        if (m) {
            m->source_path = (path && path[0]) ? tgfx_intern_string(path) : nullptr;
        }
    }

    size_t phase_count() const {
        tc_material* m = get();
        return m ? m->phase_count : 0;
    }

    tc_material_phase* get_phase(size_t index) const {
        tc_material* m = get();
        if (m && index < m->phase_count) {
            return &m->phases[index];
        }
        return nullptr;
    }

    tc_material_phase* default_phase() const {
        return get_phase(0);
    }

    tc_material_phase* find_phase(const char* mark) const {
        tc_material* m = get();
        return m ? tc_material_find_phase(m, mark) : nullptr;
    }

    void clear_phases() {
        tc_material* m = get();
        if (m) {
            m->phase_count = 0;
        }
    }

    tc_material_phase* add_phase(TcShader& shader, const char* mark = "opaque", int priority = 0) {
        tc_material* m = get();
        if (!m) return nullptr;
        return tc_material_add_phase(m, shader.handle, mark, priority);
    }

    tc_material_phase* add_phase(tc_shader_handle shader_handle, const char* mark = "opaque", int priority = 0) {
        tc_material* m = get();
        if (!m) return nullptr;
        return tc_material_add_phase(m, shader_handle, mark, priority);
    }

    tc_material_phase* add_phase_from_sources(
        const char* vertex_source,
        const char* fragment_source,
        const char* geometry_source,
        const char* shader_name,
        const char* phase_mark,
        int priority,
        const tc_render_state& state,
        const char* shader_uuid = nullptr
    ) {
        tc_material* m = get();
        if (!m) return nullptr;

        tc_shader_handle sh = tc_shader_from_sources(
            vertex_source, fragment_source, geometry_source, shader_name, nullptr, shader_uuid
        );
        if (tc_shader_handle_is_invalid(sh)) return nullptr;

        tc_material_phase* phase = tc_material_add_phase(m, sh, phase_mark, priority);
        if (phase) {
            phase->state = state;
        }
        return phase;
    }

    std::optional<Vec4> color() const {
        tc_material* m = get();
        if (!m) return std::nullopt;
        float r, g, b, a;
        if (tc_material_get_color(m, &r, &g, &b, &a)) {
            return Vec4{r, g, b, a};
        }
        return std::nullopt;
    }

    void set_color(const Vec4& rgba) {
        tc_material* m = get();
        if (m) {
            tc_material_set_color(m, rgba.x, rgba.y, rgba.z, rgba.w);
        }
    }

    void set_color(float r, float g, float b, float a = 1.0f) {
        tc_material* m = get();
        if (m) {
            tc_material_set_color(m, r, g, b, a);
        }
    }

    void set_uniform_float(const char* name, float value) {
        tc_material* m = get();
        if (m) {
            tc_material_set_uniform(m, name, TC_UNIFORM_FLOAT, &value);
        }
    }

    void set_uniform_int(const char* name, int value) {
        tc_material* m = get();
        if (m) {
            tc_material_set_uniform(m, name, TC_UNIFORM_INT, &value);
        }
    }

    void set_uniform_vec3(const char* name, const Vec3& v) {
        tc_material* m = get();
        if (m) {
            float arr[3] = {static_cast<float>(v.x), static_cast<float>(v.y), static_cast<float>(v.z)};
            tc_material_set_uniform(m, name, TC_UNIFORM_VEC3, arr);
        }
    }

    void set_uniform_vec4(const char* name, const Vec4& v) {
        tc_material* m = get();
        if (m) {
            float arr[4] = {static_cast<float>(v.x), static_cast<float>(v.y), static_cast<float>(v.z), static_cast<float>(v.w)};
            tc_material_set_uniform(m, name, TC_UNIFORM_VEC4, arr);
        }
    }

    void set_uniform_mat4(const char* name, const Mat44f& mat) {
        tc_material* m = get();
        if (m) {
            tc_material_set_uniform(m, name, TC_UNIFORM_MAT4, mat.data);
        }
    }

    void set_texture(const char* name, TcTexture& tex) {
        tc_material* m = get();
        if (m) {
            tc_material_set_texture(m, name, tex.handle);
        }
    }

    void set_texture(const char* name, tc_texture_handle tex_handle) {
        tc_material* m = get();
        if (m) {
            tc_material_set_texture(m, name, tex_handle);
        }
    }

    const char* active_phase_mark() const {
        tc_material* m = get();
        return m ? m->active_phase_mark : "";
    }

    void set_active_phase_mark(const char* mark) {
        tc_material* m = get();
        if (m) {
            strncpy(m->active_phase_mark, mark, TC_PHASE_MARK_MAX - 1);
            m->active_phase_mark[TC_PHASE_MARK_MAX - 1] = '\0';
        }
    }

    std::vector<tc_material_phase*> get_phases_for_mark(const std::string& mark) const {
        std::vector<tc_material_phase*> result;
        tc_material* m = get();
        if (!m) return result;

        tc_material_phase* phases[TC_MATERIAL_MAX_PHASES];
        size_t count = tc_material_get_phases_for_mark(m, mark.c_str(), phases, TC_MATERIAL_MAX_PHASES);
        result.reserve(count);
        for (size_t i = 0; i < count; i++) {
            result.push_back(phases[i]);
        }
        return result;
    }

    std::vector<std::string> get_phase_marks() const {
        std::vector<std::string> marks;
        tc_material* m = get();
        if (!m) return marks;

        for (size_t i = 0; i < m->phase_count; i++) {
            std::string mark = m->phases[i].phase_mark;
            if (std::find(marks.begin(), marks.end(), mark) == marks.end()) {
                marks.push_back(mark);
            }
        }
        return marks;
    }

    TcShader get_phase_shader(size_t phase_index) const {
        tc_material* m = get();
        if (!m || phase_index >= m->phase_count) return TcShader();
        return TcShader(m->phases[phase_index].shader);
    }

    tc_render_state get_phase_render_state(size_t phase_index) const {
        tc_material* m = get();
        if (!m || phase_index >= m->phase_count) return tc_render_state_opaque();
        return m->phases[phase_index].state;
    }

    tc_value serialize_to_value() const {
        tc_value d = tc_value_dict_new();
        if (!is_valid()) {
            tc_value_dict_set(&d, "type", tc_value_string("none"));
            return d;
        }
        tc_value_dict_set(&d, "uuid", tc_value_string(uuid()));
        tc_value_dict_set(&d, "name", tc_value_string(name()));
        tc_value_dict_set(&d, "type", tc_value_string("uuid"));
        return d;
    }

    void deserialize_from(const tc_value* data, void* = nullptr) {
        if (tc_material* m = tc_material_get(handle)) {
            tc_material_release(m);
        }
        handle = tc_material_handle_invalid();

        if (!data) return;

        if (data->type == TC_VALUE_STRING && data->data.s && data->data.s[0]) {
            const char* mat_name = data->data.s;
            if (strcmp(mat_name, "(None)") == 0) return;

            tc_material_handle h = tc_material_find_by_name(mat_name);
            if (!tc_material_handle_is_invalid(h)) {
                handle = h;
                if (tc_material* m = tc_material_get(handle)) {
                    tc_material_add_ref(m);
                }
            } else {
                tc_log_error("[TcMaterial] Material '%s' not found", mat_name);
            }
            return;
        }

        if (data->type != TC_VALUE_DICT) return;

        tc_value* uuid_val = tc_value_dict_get(const_cast<tc_value*>(data), "uuid");
        if (uuid_val && uuid_val->type == TC_VALUE_STRING && uuid_val->data.s) {
            tc_material_handle h = tc_material_find(uuid_val->data.s);
            if (!tc_material_handle_is_invalid(h)) {
                handle = h;
                if (tc_material* m = tc_material_get(handle)) {
                    tc_material_add_ref(m);
                }
                return;
            }
        }

        tc_value* name_val = tc_value_dict_get(const_cast<tc_value*>(data), "name");
        if (name_val && name_val->type == TC_VALUE_STRING && name_val->data.s) {
            const char* mat_name = name_val->data.s;
            tc_material_handle h = tc_material_find_by_name(mat_name);
            if (!tc_material_handle_is_invalid(h)) {
                handle = h;
                if (tc_material* m = tc_material_get(handle)) {
                    tc_material_add_ref(m);
                }
            } else {
                tc_log_error("[TcMaterial] Material '%s' not found", mat_name);
            }
        }
    }

    static TcMaterial from_uuid(const std::string& uuid) {
        tc_material_handle h = tc_material_find(uuid.c_str());
        if (tc_material_handle_is_invalid(h)) {
            return TcMaterial();
        }
        return TcMaterial(h);
    }

    static TcMaterial from_name(const std::string& name) {
        tc_material_handle h = tc_material_find_by_name(name.c_str());
        if (tc_material_handle_is_invalid(h)) {
            return TcMaterial();
        }
        return TcMaterial(h);
    }

    static TcMaterial get_or_create(const std::string& uuid, const std::string& name) {
        tc_material_handle h = tc_material_get_or_create(uuid.c_str(), name.c_str());
        if (tc_material_handle_is_invalid(h)) {
            return TcMaterial();
        }
        return TcMaterial(h);
    }

    static TcMaterial create(const std::string& name, const std::string& uuid_hint = "") {
        if (name.empty()) {
            tc_log_error("[TcMaterial::create] name is required");
            return TcMaterial();
        }
        const char* uuid = uuid_hint.empty() ? nullptr : uuid_hint.c_str();
        tc_material_handle h = tc_material_create(uuid, name.c_str());
        if (tc_material_handle_is_invalid(h)) {
            return TcMaterial();
        }
        return TcMaterial(h);
    }

    static TcMaterial copy(const TcMaterial& src, const std::string& new_uuid = "") {
        const char* uuid = new_uuid.empty() ? nullptr : new_uuid.c_str();
        tc_material_handle h = tc_material_copy(src.handle, uuid);
        if (tc_material_handle_is_invalid(h)) {
            return TcMaterial();
        }
        return TcMaterial(h);
    }
};

} // namespace termin
