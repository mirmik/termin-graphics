#pragma once

#include <algorithm>
#include <memory>
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

class RENDER_API Drawable {
public:
    virtual ~Drawable() = default;

    virtual std::set<std::string> get_phase_marks() const = 0;

    virtual bool collect_render_items(
        const tc_render_item_collect_context& context,
        tc_render_item_sink& sink
    );

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
    static bool _cb_collect_render_items(tc_component* c, const tc_render_item_collect_context* context, tc_render_item_sink* sink);
};

struct RENDER_API RenderItemCollection {
    std::vector<tc_render_item> items;
    std::vector<std::vector<tc_render_item_vec3>> line_batch_points;
    std::vector<std::unique_ptr<std::string>> text_batch_strings;
    std::vector<std::unique_ptr<std::string>> foliage_batch_strings;

    RenderItemCollection() = default;
    ~RenderItemCollection() = default;
    RenderItemCollection(RenderItemCollection&&) noexcept = default;
    RenderItemCollection& operator=(RenderItemCollection&&) noexcept = default;
    RenderItemCollection(const RenderItemCollection&) = delete;
    RenderItemCollection& operator=(const RenderItemCollection&) = delete;

    void clear() {
        items.clear();
        line_batch_points.clear();
        text_batch_strings.clear();
        foliage_batch_strings.clear();
    }
};

RENDER_API bool collect_drawable_render_items(
    tc_component* component,
    const tc_render_item_collect_context& context,
    RenderItemCollection& out_collection);

struct PhaseDrawCall {
    Entity entity;
    tc_component* component = nullptr;
    tc_material_phase* phase = nullptr;
    TcShader final_shader;
    int priority = 0;
    int geometry_id = 0;
    size_t item_index = SIZE_MAX;
    tc_render_item item{};
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
