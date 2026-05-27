#pragma once

#include <memory>
#include <functional>
#include <cstddef>
#include <cstdint>
#include <set>
#include <string>
#include <vector>

extern "C" {
#include "core/tc_component.h"
#include "core/tc_drawable_protocol.h"
#include "tc_value.h"
}

struct tc_mesh;

#include <termin/entity/component.hpp>
#include <termin/entity/entity.hpp>
#include <termin/geom/mat44.hpp>
#include <tgfx/resources/tc_material.h>
#include <tgfx/resources/tc_material_registry.h>
#include <tgfx/tgfx_shader_handle.hpp>

#include <termin/render/render_context.hpp>
#include <termin/render/render_export.hpp>

namespace tgfx { class RenderContext2; }

namespace termin {

struct GeometryDrawCall {
    tc_material_phase* phase = nullptr;
    tc_material_handle material = tc_material_handle_invalid();
    size_t phase_index = SIZE_MAX;
    int geometry_id = 0;

    GeometryDrawCall() = default;

    GeometryDrawCall(tc_material_phase* p, int gid = 0)
        : phase(p), geometry_id(gid) {
        bind_phase_ref(p);
    }

    void bind_phase_ref(tc_material_phase* p) {
        phase = p;
        material = tc_material_handle_invalid();
        phase_index = SIZE_MAX;
        tc_material_find_phase_ref(p, &material, &phase_index);
    }

    bool has_stable_phase_ref() const {
        return !tc_material_handle_is_invalid(material) && phase_index != SIZE_MAX;
    }

    tc_material_phase* resolve_phase() const {
        if (has_stable_phase_ref()) {
            tc_material* mat = tc_material_get(material);
            if (mat && phase_index < mat->phase_count) {
                return &mat->phases[phase_index];
            }
        }
        return phase;
    }
};

class RENDER_API Drawable {
public:
    mutable std::vector<GeometryDrawCall> _cached_geometry_draws;

    virtual ~Drawable() = default;

    virtual std::set<std::string> get_phase_marks() const = 0;
    virtual void draw_geometry(const RenderContext& context, int geometry_id = 0) = 0;
    virtual std::vector<GeometryDrawCall> get_geometry_draws(const std::string* phase_mark = nullptr) = 0;

    virtual TcShader override_shader(
        const std::string& phase_mark,
        int geometry_id,
        TcShader original_shader
    ) {
        (void)phase_mark;
        (void)geometry_id;
        return original_shader;
    }

    virtual void collect_shader_usages(
        const std::string& phase_mark,
        int geometry_id,
        TcShader original_shader,
        const std::function<void(TcShader)>& emit
    ) {
        (void)phase_mark;
        (void)geometry_id;
        emit(original_shader);
    }

    // Expose the underlying tc_mesh for a given phase + geometry id so
    // tgfx2-migrated passes (ShadowPass, IdPass, ColorPass) can wrap it
    // via wrap_mesh_as_tgfx2() and draw through RenderContext2 instead
    // of going through the old draw_geometry()/tc_mesh_draw_gpu path.
    //
    // Default returns nullptr; drawables that are still on the old
    // path fall back to draw_geometry(). MeshRenderer overrides to
    // return its TcMesh. Returning non-null opts a drawable in to the
    // tgfx2 shadow/id/color rendering paths one type at a time.
    virtual tc_mesh* get_mesh_for_phase(
        const std::string& phase_mark,
        int geometry_id
    ) const {
        (void)phase_mark;
        (void)geometry_id;
        return nullptr;
    }

    // Upload any per-draw uniforms that aren't derivable from the
    // material UBO / push-constant path. Called by tgfx2 pass draw
    // loops (ShadowPass, ColorPass, ...) right before the draw, after
    // the tgfx2 shader has been bound on ctx2 but before ctx2->draw().
    // SkinnedMeshRenderer overrides this to push u_bone_matrices.
    // Default: no-op.
    virtual void upload_per_draw_uniforms_tgfx2(
        tgfx::RenderContext2& ctx2,
        int geometry_id
    ) {
        (void)ctx2;
        (void)geometry_id;
    }

    // Direct tgfx2 draw hook for drawables that are not backed by a
    // tc_mesh in a given render mode. ColorPass calls this when
    // get_mesh_for_phase() returns nullptr.
    virtual bool draw_tgfx2(
        tgfx::RenderContext2& ctx2,
        const RenderContext& context,
        const std::string& phase_mark,
        tc_material_phase* phase,
        int geometry_id
    ) {
        (void)ctx2;
        (void)context;
        (void)phase_mark;
        (void)phase;
        (void)geometry_id;
        return false;
    }

    virtual Mat44f get_model_matrix(const Entity& entity) const;

    bool has_phase(const std::string& phase_mark) const {
        auto marks = get_phase_marks();
        return marks.find(phase_mark) != marks.end();
    }

    static const tc_drawable_vtable& cxx_drawable_vtable();

protected:
    void install_drawable_vtable(tc_component* c) {
        if (c) {
            tc_drawable_capability_attach(c, &cxx_drawable_vtable(), this);
        }
    }

private:
    static bool _cb_has_phase(tc_component* c, const char* phase_mark);
    static void _cb_draw_geometry(tc_component* c, void* render_context, int geometry_id);
    static void* _cb_get_geometry_draws(tc_component* c, const char* phase_mark);
    static tc_shader_handle _cb_override_shader(tc_component* c, const char* phase_mark, int geometry_id, tc_shader_handle original_shader);
    static void _cb_collect_shader_usages(tc_component* c, const char* phase_mark, int geometry_id, tc_shader_handle original_shader, tc_shader_usage_emit_fn emit, void* user_data);
};

struct PhaseDrawCall {
    Entity entity;
    tc_component* component = nullptr;
    tc_material_phase* phase = nullptr;
    tc_shader_handle final_shader;
    int priority = 0;
    int geometry_id = 0;
    tc_material_handle material = tc_material_handle_invalid();
    size_t phase_index = SIZE_MAX;

    tc_material_phase* resolve_phase() const {
        if (!tc_material_handle_is_invalid(material) && phase_index != SIZE_MAX) {
            tc_material* mat = tc_material_get(material);
            if (mat && phase_index < mat->phase_count) {
                return &mat->phases[phase_index];
            }
        }
        return phase;
    }
};

} // namespace termin
