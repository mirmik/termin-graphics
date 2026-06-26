#pragma once

// TcMesh - RAII wrapper with handle-based access to tc_mesh
// Uses tc_mesh_handle with generation checking for safety

extern "C" {
#include <tgfx/resources/tc_mesh.h>
#include <tgfx/resources/tc_mesh_registry.h>
#include <tcbase/tc_value.h>
#include <tcbase/tc_log.h>
}

#include <tgfx/tgfx_api.h>
#include <string>
#include <cstring>
#include <vector>

namespace termin {

// Forward declaration
class Mesh3;

// TcMesh - GPU-ready mesh wrapper
// Stores handle (index + generation) instead of raw pointer
class TGFX_API TcMesh {
public:
    tc_mesh_handle handle = tc_mesh_handle_invalid();

    TcMesh() = default;

    explicit TcMesh(tc_mesh_handle h) : handle(h) {
        if (tc_mesh* m = tc_mesh_get(handle)) {
            tc_mesh_add_ref(m);
        }
    }

    // Construct from raw pointer (finds handle for it)
    explicit TcMesh(tc_mesh* m) {
        if (m) {
            handle = tc_mesh_find(m->header.uuid);
            tc_mesh_add_ref(m);
        }
    }

    TcMesh(const TcMesh& other) : handle(other.handle) {
        if (tc_mesh* m = tc_mesh_get(handle)) {
            tc_mesh_add_ref(m);
        }
    }

    TcMesh(TcMesh&& other) noexcept : handle(other.handle) {
        other.handle = tc_mesh_handle_invalid();
    }

    TcMesh& operator=(const TcMesh& other) {
        if (this != &other) {
            if (tc_mesh* m = tc_mesh_get(handle)) {
                tc_mesh_release(m);
            }
            handle = other.handle;
            if (tc_mesh* m = tc_mesh_get(handle)) {
                tc_mesh_add_ref(m);
            }
        }
        return *this;
    }

    TcMesh& operator=(TcMesh&& other) noexcept {
        if (this != &other) {
            if (tc_mesh* m = tc_mesh_get(handle)) {
                tc_mesh_release(m);
            }
            handle = other.handle;
            other.handle = tc_mesh_handle_invalid();
        }
        return *this;
    }

    ~TcMesh() {
        if (tc_mesh* m = tc_mesh_get(handle)) {
            tc_mesh_release(m);
        }
        handle = tc_mesh_handle_invalid();
    }

    // Get raw pointer (may return nullptr if handle is stale)
    tc_mesh* get() const { return tc_mesh_get(handle); }

    // For backwards compatibility
    tc_mesh* mesh_ptr() const { return get(); }

    // Query (safe - returns defaults if handle is stale)
    bool is_valid() const { return tc_mesh_is_valid(handle); }

    const char* uuid() const {
        tc_mesh* m = get();
        return m ? m->header.uuid : "";
    }

    const char* name() const {
        tc_mesh* m = get();
        return (m && m->header.name) ? m->header.name : "";
    }

    uint32_t version() const {
        tc_mesh* m = get();
        return m ? m->header.version : 0;
    }

    size_t vertex_count() const {
        tc_mesh* m = get();
        return m ? m->vertex_count : 0;
    }

    size_t index_count() const {
        tc_mesh* m = get();
        return m ? m->index_count : 0;
    }

    size_t triangle_count() const {
        tc_mesh* m = get();
        return m ? m->index_count / 3 : 0;
    }

    uint16_t stride() const {
        tc_mesh* m = get();
        return m ? m->layout.stride : 0;
    }

    const tc_vertex_layout* layout() const {
        tc_mesh* m = get();
        return m ? &m->layout : nullptr;
    }

    tc_draw_mode draw_mode() const {
        tc_mesh* m = get();
        return m ? static_cast<tc_draw_mode>(m->draw_mode) : TC_DRAW_TRIANGLES;
    }

    void set_draw_mode(tc_draw_mode mode) {
        if (tc_mesh* m = get()) {
            m->draw_mode = static_cast<uint8_t>(mode);
        }
    }

    void bump_version() {
        if (tc_mesh* m = get()) {
            m->header.version++;
        }
    }

    // Trigger lazy load if mesh is declared but not loaded
    bool ensure_loaded() {
        return tc_mesh_ensure_loaded(handle);
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
        if (tc_mesh* m = tc_mesh_get(handle)) {
            tc_mesh_release(m);
        }
        handle = tc_mesh_handle_invalid();

        if (!data) return;

        if (data->type == TC_VALUE_STRING && data->data.s && data->data.s[0]) {
            const char* mesh_name = data->data.s;
            if (strcmp(mesh_name, "(None)") == 0) return;

            tc_mesh_handle h = tc_mesh_find_by_name(mesh_name);
            if (!tc_mesh_handle_is_invalid(h)) {
                handle = h;
                if (tc_mesh* m = tc_mesh_get(handle)) {
                    tc_mesh_add_ref(m);
                }
            } else {
                tc_log_error("[TcMesh] Mesh '%s' not found", mesh_name);
            }
            return;
        }

        if (data->type != TC_VALUE_DICT) return;

        tc_value* uuid_val = tc_value_dict_get(const_cast<tc_value*>(data), "uuid");
        if (uuid_val && uuid_val->type == TC_VALUE_STRING && uuid_val->data.s) {
            tc_mesh_handle h = tc_mesh_find(uuid_val->data.s);
            if (!tc_mesh_handle_is_invalid(h)) {
                handle = h;
                if (tc_mesh* m = tc_mesh_get(handle)) {
                    tc_mesh_add_ref(m);
                }
                ensure_loaded();
                return;
            }
        }

        tc_value* name_val = tc_value_dict_get(const_cast<tc_value*>(data), "name");
        if (name_val && name_val->type == TC_VALUE_STRING && name_val->data.s) {
            const char* mesh_name = name_val->data.s;
            tc_mesh_handle h = tc_mesh_find_by_name(mesh_name);
            if (!tc_mesh_handle_is_invalid(h)) {
                handle = h;
                if (tc_mesh* m = tc_mesh_get(handle)) {
                    tc_mesh_add_ref(m);
                }
                ensure_loaded();
            } else {
                tc_log_error("[TcMesh] Mesh '%s' not found", mesh_name);
            }
        }
    }

    // Populate existing TcMesh with data from Mesh3
    bool set_from_mesh3(const Mesh3& mesh, const tc_vertex_layout* custom_layout = nullptr);

    // Create TcMesh from Mesh3 (CPU mesh)
    static TcMesh from_mesh3(const Mesh3& mesh,
                             const std::string& override_name = "",
                             const std::string& override_uuid = "",
                             const tc_vertex_layout* custom_layout = nullptr);

    // Create TcMesh from raw interleaved vertex data
    static TcMesh from_interleaved(
        const void* vertices, size_t vertex_count,
        const uint32_t* indices, size_t index_count,
        const tc_vertex_layout& layout,
        const std::string& name = "",
        const std::string& uuid_hint = "",
        tc_draw_mode draw_mode = TC_DRAW_TRIANGLES);

    // Get by UUID from registry
    static TcMesh from_uuid(const std::string& uuid) {
        tc_mesh_handle h = tc_mesh_find(uuid.c_str());
        if (tc_mesh_handle_is_invalid(h)) {
            return TcMesh();
        }
        return TcMesh(h);
    }

    // Get or create by UUID
    static TcMesh get_or_create(const std::string& uuid) {
        tc_mesh_handle h = tc_mesh_get_or_create(uuid.c_str());
        if (tc_mesh_handle_is_invalid(h)) {
            return TcMesh();
        }
        return TcMesh(h);
    }
};

} // namespace termin
