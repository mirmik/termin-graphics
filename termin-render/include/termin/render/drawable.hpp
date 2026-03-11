#pragma once

#include <memory>
#include <set>
#include <string>
#include <vector>

extern "C" {
#include "core/tc_component.h"
#include "core/tc_drawable_protocol.h"
#include "tc_value.h"
}

#include <termin/entity/component.hpp>
#include <termin/entity/entity.hpp>
#include <termin/geom/mat44.hpp>
#include <tgfx/resources/tc_material.h>
#include <tgfx/tgfx_shader_handle.hpp>

#include <termin/render/render_context.hpp>
#include <termin/render/render_export.hpp>

namespace termin {

class GraphicsBackend;

struct GeometryDrawCall {
    tc_material_phase* phase = nullptr;
    int geometry_id = 0;

    GeometryDrawCall() = default;

    GeometryDrawCall(tc_material_phase* p, int gid = 0)
        : phase(p), geometry_id(gid) {}
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
        return original_shader;
    }

    virtual Mat44f get_model_matrix(const Entity& entity) const;

    bool has_phase(const std::string& phase_mark) const {
        auto marks = get_phase_marks();
        return marks.find(phase_mark) != marks.end();
    }

    static const tc_drawable_vtable cxx_drawable_vtable;

protected:
    void install_drawable_vtable(tc_component* c) {
        if (c) {
            tc_drawable_capability_attach(c, &cxx_drawable_vtable, this);
        }
    }

private:
    static bool _cb_has_phase(tc_component* c, const char* phase_mark);
    static void _cb_draw_geometry(tc_component* c, void* render_context, int geometry_id);
    static void* _cb_get_geometry_draws(tc_component* c, const char* phase_mark);
    static tc_shader_handle _cb_override_shader(tc_component* c, const char* phase_mark, int geometry_id, tc_shader_handle original_shader);
};

struct PhaseDrawCall {
    Entity entity;
    tc_component* component = nullptr;
    tc_material_phase* phase = nullptr;
    tc_shader_handle final_shader;
    int priority = 0;
    int geometry_id = 0;
};

} // namespace termin
