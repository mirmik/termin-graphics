#pragma once

#include <algorithm>
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

#include <termin/render/material_pipeline_shader_assembler.hpp>
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

struct MeshDrawGeometry {
    tc_mesh* mesh = nullptr;
    size_t submesh_index = 0;
};

enum class DirectTgfx2DrawKind {
    MaterialPhase,
    OverrideColor,
};

struct ShaderOverrideContext {
    // Drawable-facing representation/material routing label. This selects
    // geometry/material participation only; render passes must not rely on it
    // to imply vertex layout, skinning template, pass resources, or output
    // semantics.
    std::string phase_mark;
    int geometry_id = 0;
    TcShader original_shader;

    // Pass-owned shader/layout intent. Context-aware override paths must use
    // this contract for material-pipeline variants instead of interpreting
    // phase_mark strings such as "shadow", "depth", "pick", or "normal".
    MaterialPipelinePassContract pass_contract;
};

class RENDER_API Drawable {
public:
    mutable std::vector<GeometryDrawCall> _cached_geometry_draws;
    mutable std::vector<int> _cached_geometry_ids;

    virtual ~Drawable() = default;

    virtual std::set<std::string> get_phase_marks() const = 0;
    virtual void draw_geometry(const RenderContext& context, int geometry_id = 0) = 0;
    virtual std::vector<GeometryDrawCall> get_geometry_draws(
        const RenderContext& context,
        const std::string* phase_mark = nullptr
    ) = 0;

    virtual TcShader override_shader(
        const std::string& phase_mark,
        int geometry_id,
        TcShader original_shader
    ) {
        (void)phase_mark;
        (void)geometry_id;
        return original_shader;
    }

    virtual TcShader override_shader_with_context(
        const ShaderOverrideContext& context
    ) {
        return override_shader(
            context.phase_mark,
            context.geometry_id,
            context.original_shader);
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

    virtual void collect_shader_usages_with_context(
        const ShaderOverrideContext& context,
        const std::function<void(TcShader)>& emit
    ) {
        if (context.original_shader.is_valid()) {
            emit(context.original_shader);
        }
        TcShader override_shader = override_shader_with_context(context);
        if (override_shader.is_valid() &&
            (override_shader.handle.index != context.original_shader.handle.index ||
             override_shader.handle.generation != context.original_shader.handle.generation)) {
            emit(override_shader);
        }
    }

    virtual bool collect_render_items(
        const tc_render_item_collect_context& context,
        tc_render_item_sink& sink
    );

    // Expose the underlying tc_mesh for a given phase + geometry id so
    // tgfx2-migrated passes (ShadowPass, IdPass, ColorPass) can wrap it
    // via wrap_mesh_as_tgfx2() and draw through RenderContext2.
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

    virtual bool resolve_mesh_geometry(
        const std::string& phase_mark,
        int geometry_id,
        MeshDrawGeometry& out
    ) const {
        if (geometry_id != 0) {
            return false;
        }
        tc_mesh* mesh = get_mesh_for_phase(phase_mark, geometry_id);
        if (!mesh) {
            return false;
        }
        out.mesh = mesh;
        out.submesh_index = 0;
        return true;
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

    // Direct tgfx2 drawables can bind their own shaders instead of the
    // material shader pair. Passes still need to know whether to prepare and
    // bind the engine lighting block for those shaders.
    virtual bool needs_lighting_ubo_tgfx2(
        const std::string& phase_mark,
        int geometry_id
    ) const {
        (void)phase_mark;
        (void)geometry_id;
        return false;
    }

    // Direct tgfx2 drawing is intentionally opt-in per pass contract.
    // MaterialPhase means the caller provides a live material phase and
    // expects material/lighting/shadow bindings to be honored. OverrideColor
    // means the pass owns the shader/color contract (for example IdPass).
    virtual bool supports_direct_tgfx2_draw(
        const std::string& phase_mark,
        int geometry_id,
        DirectTgfx2DrawKind kind
    ) const {
        (void)phase_mark;
        (void)geometry_id;
        (void)kind;
        return false;
    }

    virtual std::vector<int> get_geometry_ids_for_phase(
        const RenderContext& context,
        const std::string& phase_mark
    ) {
        std::vector<int> ids;
        std::vector<GeometryDrawCall> draws = get_geometry_draws(context, &phase_mark);
        for (const GeometryDrawCall& draw : draws) {
            if (std::find(ids.begin(), ids.end(), draw.geometry_id) == ids.end()) {
                ids.push_back(draw.geometry_id);
            }
        }
        if (!ids.empty()) {
            return ids;
        }

        MeshDrawGeometry mesh_geometry{};
        if (resolve_mesh_geometry(phase_mark, 0, mesh_geometry) ||
            supports_direct_tgfx2_draw(phase_mark, 0, DirectTgfx2DrawKind::OverrideColor)) {
            ids.push_back(0);
        }
        return ids;
    }

    // Direct tgfx2 draw hook for drawables that are not backed by a
    // tc_mesh in a given render mode. Passes call this only after
    // supports_direct_tgfx2_draw() confirms the drawable understands that
    // pass contract.
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
    static void* _cb_get_geometry_draws(tc_component* c, void* render_context, const char* phase_mark);
    static void* _cb_get_geometry_ids_for_phase(tc_component* c, void* render_context, const char* phase_mark);
    static tc_shader_handle _cb_override_shader(tc_component* c, const char* phase_mark, int geometry_id, tc_shader_handle original_shader);
    static void _cb_collect_shader_usages(tc_component* c, const char* phase_mark, int geometry_id, tc_shader_handle original_shader, tc_shader_usage_emit_fn emit, void* user_data);
    static bool _cb_collect_render_items(tc_component* c, const tc_render_item_collect_context* context, tc_render_item_sink* sink);
};

RENDER_API TcShader override_drawable_shader(
    tc_component* component,
    const ShaderOverrideContext& context);

RENDER_API void collect_drawable_shader_usages_with_context(
    tc_component* component,
    const ShaderOverrideContext& context,
    const std::function<void(TcShader)>& emit);

RENDER_API bool collect_drawable_render_items(
    tc_component* component,
    const tc_render_item_collect_context& context,
    std::vector<tc_render_item>& out_items);

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
