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
    virtual ~Drawable() = default;

    virtual std::set<std::string> get_phase_marks() const = 0;

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
    tc_shader_handle final_shader;
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
