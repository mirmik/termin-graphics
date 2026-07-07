#include <termin/render/drawable.hpp>

#include <tcbase/tc_log.hpp>
#include <cstring>

namespace termin {

bool Drawable::_cb_has_phase(tc_component* c, const char* phase_mark) {
    if (!c) return false;

    Drawable* drawable = static_cast<Drawable*>(tc_component_get_drawable_userdata(c));
    if (!drawable) return false;

    return drawable->has_phase(phase_mark ? phase_mark : "");
}

void Drawable::_cb_draw_geometry(tc_component* c, void* render_context, int geometry_id) {
    if (!c) return;

    RenderContext* ctx = static_cast<RenderContext*>(render_context);
    if (!ctx) return;

    Drawable* drawable = static_cast<Drawable*>(tc_component_get_drawable_userdata(c));
    if (!drawable) return;

    drawable->draw_geometry(*ctx, geometry_id);
}

void* Drawable::_cb_get_geometry_draws(tc_component* c, void* render_context, const char* phase_mark) {
    if (!c) return nullptr;

    Drawable* drawable = static_cast<Drawable*>(tc_component_get_drawable_userdata(c));
    if (!drawable) return nullptr;

    std::string phase = phase_mark ? phase_mark : "";
    const std::string* phase_ptr = phase.empty() ? nullptr : &phase;
    auto* ctx = static_cast<RenderContext*>(render_context);
    if (!ctx) return nullptr;

    drawable->_cached_geometry_draws = drawable->get_geometry_draws(*ctx, phase_ptr);
    return &drawable->_cached_geometry_draws;
}

void* Drawable::_cb_get_geometry_ids_for_phase(tc_component* c, void* render_context, const char* phase_mark) {
    if (!c) return nullptr;

    Drawable* drawable = static_cast<Drawable*>(tc_component_get_drawable_userdata(c));
    if (!drawable) return nullptr;

    auto* ctx = static_cast<RenderContext*>(render_context);
    if (!ctx) return nullptr;

    drawable->_cached_geometry_ids = drawable->get_geometry_ids_for_phase(
        *ctx,
        phase_mark ? phase_mark : "");
    return &drawable->_cached_geometry_ids;
}

tc_shader_handle Drawable::_cb_override_shader(
    tc_component* c,
    const char* phase_mark,
    int geometry_id,
    tc_shader_handle original_shader
) {
    if (!c) return original_shader;

    Drawable* drawable = static_cast<Drawable*>(tc_component_get_drawable_userdata(c));
    if (!drawable) return original_shader;

    TcShader result = drawable->override_shader(
        phase_mark ? phase_mark : "",
        geometry_id,
        TcShader(original_shader)
    );

    return result.handle;
}

TcShader override_drawable_shader(
    tc_component* component,
    const ShaderOverrideContext& context)
{
    if (!component) {
        return context.original_shader;
    }

    if (tc_component_get_drawable_vtable(component) == &Drawable::cxx_drawable_vtable()) {
        Drawable* drawable = static_cast<Drawable*>(
            tc_component_get_drawable_userdata(component));
        if (drawable) {
            return drawable->override_shader_with_context(context);
        }
    }

    return TcShader(tc_component_override_shader(
        component,
        context.phase_mark.c_str(),
        context.geometry_id,
        context.original_shader.handle));
}

void collect_drawable_shader_usages_with_context(
    tc_component* component,
    const ShaderOverrideContext& context,
    const std::function<void(TcShader)>& emit)
{
    if (!emit) {
        return;
    }

    if (!component) {
        if (context.original_shader.is_valid()) {
            emit(context.original_shader);
        }
        return;
    }

    if (tc_component_get_drawable_vtable(component) == &Drawable::cxx_drawable_vtable()) {
        Drawable* drawable = static_cast<Drawable*>(
            tc_component_get_drawable_userdata(component));
        if (drawable) {
            drawable->collect_shader_usages_with_context(context, emit);
            return;
        }
    }

    if (context.original_shader.is_valid()) {
        emit(context.original_shader);
    }
    TcShader override_shader(tc_component_override_shader(
        component,
        context.phase_mark.c_str(),
        context.geometry_id,
        context.original_shader.handle));
    if (override_shader.is_valid() &&
        (override_shader.handle.index != context.original_shader.handle.index ||
         override_shader.handle.generation != context.original_shader.handle.generation)) {
        emit(override_shader);
    }
}

bool Drawable::collect_render_items(
    const tc_render_item_collect_context& context,
    tc_render_item_sink& sink
) {
    if (!sink.emit) {
        tc::Log::error("[Drawable] cannot emit render items: sink callback is null");
        return false;
    }
    if (!context.phase_mark || context.phase_mark[0] == '\0') {
        tc::Log::error("[Drawable] cannot emit render items: phase_mark is empty");
        return false;
    }

    auto* component = dynamic_cast<Component*>(this);
    if (!component) {
        return true;
    }

    RenderContext render_context;
    render_context.phase = context.phase_mark;
    render_context.layer_mask = context.layer_mask;
    render_context.render_category_mask = context.render_category_mask;
    if (context.pass_contract) {
        render_context.pass_contract =
            *static_cast<const MaterialPipelinePassContract*>(context.pass_contract);
    }

    const std::string phase_mark(context.phase_mark);
    std::vector<int> geometry_ids = get_geometry_ids_for_phase(render_context, phase_mark);
    if (geometry_ids.empty()) {
        return true;
    }

    std::vector<GeometryDrawCall> geometry_draws = get_geometry_draws(render_context, &phase_mark);
    Entity owner = component->entity();
    Mat44f model = owner.valid() ? get_model_matrix(owner) : Mat44f::identity();

    for (int geometry_id : geometry_ids) {
        MeshDrawGeometry mesh_geometry{};
        if (!resolve_mesh_geometry(phase_mark, geometry_id, mesh_geometry)) {
            continue;
        }
        if (!mesh_geometry.mesh) {
            tc::Log::error(
                "[Drawable] cannot emit mesh RenderItem: geometry %d resolved null mesh",
                geometry_id);
            continue;
        }

        tc_mesh_handle mesh_handle = tc_mesh_find(mesh_geometry.mesh->header.uuid);
        if (tc_mesh_handle_is_invalid(mesh_handle)) {
            tc::Log::error(
                "[Drawable] cannot emit mesh RenderItem: geometry %d mesh has no stable registry handle",
                geometry_id);
            continue;
        }

        const GeometryDrawCall* selected_draw = nullptr;
        for (const GeometryDrawCall& draw : geometry_draws) {
            if (draw.geometry_id == geometry_id) {
                selected_draw = &draw;
                break;
            }
        }

        tc_material_phase* phase = selected_draw ? selected_draw->resolve_phase() : nullptr;
        const bool emit_without_material_phase =
            !phase &&
            ((context.flags & TC_RENDER_ITEM_COLLECT_FLAG_ALLOW_MISSING_MATERIAL_PHASE) != 0u);
        if (!phase && !emit_without_material_phase) {
            continue;
        }

        tc_render_item item{};
        item.kind = TC_RENDER_ITEM_KIND_MESH;
        item.flags = TC_RENDER_ITEM_FLAG_HAS_MODEL_MATRIX;
        item.component = component->tc_component_ptr();
        item.geometry_id = geometry_id;
        item.material_phase = phase;
        item.material = tc_material_handle_invalid();
        item.material_phase_index = SIZE_MAX;
        if (phase) {
            item.flags |= TC_RENDER_ITEM_FLAG_HAS_MATERIAL_PHASE;
            tc_material_find_phase_ref(phase, &item.material, &item.material_phase_index);
        }
        std::memcpy(item.model_matrix, model.data, sizeof(float) * 16);
        item.payload.mesh.mesh = mesh_geometry.mesh;
        item.payload.mesh.mesh_handle = mesh_handle;
        item.payload.mesh.submesh_index = mesh_geometry.submesh_index;

        if (!sink.emit(&item, sink.user_data)) {
            return false;
        }
    }

    return true;
}

namespace {

const char* render_item_kind_name(uint32_t kind) {
    switch (kind) {
        case TC_RENDER_ITEM_KIND_MESH:
            return "Mesh";
        case TC_RENDER_ITEM_KIND_LINE_BATCH:
            return "LineBatch";
        case TC_RENDER_ITEM_KIND_TEXT_BATCH:
            return "TextBatch";
        case TC_RENDER_ITEM_KIND_FOLIAGE_BATCH:
            return "FoliageBatch";
        default:
            return "Invalid";
    }
}

const char* component_debug_name(const tc_component* component) {
    if (!component) {
        return "<unknown>";
    }
    if (component->declared_type_name) {
        return component->declared_type_name;
    }
    if (component->display_name) {
        return component->display_name;
    }
    return "<unknown>";
}

bool validate_render_item(
    const tc_render_item& item,
    const tc_render_item_collect_context& context,
    tc_component* source_component)
{
    const char* phase_mark = context.phase_mark ? context.phase_mark : "";
    const char* pass_name = context.debug_pass_name ? context.debug_pass_name : "<unknown>";
    tc_component* component = item.component ? item.component : source_component;
    const char* component_type = component_debug_name(component);

    if (item.kind == TC_RENDER_ITEM_KIND_INVALID) {
        tc::Log::error(
            "[RenderItemSink] malformed item: pass='%s' phase='%s' component='%s' emitted invalid kind",
            pass_name,
            phase_mark,
            component_type);
        return false;
    }

    if (item.kind == TC_RENDER_ITEM_KIND_MESH && !item.payload.mesh.mesh) {
        tc::Log::error(
            "[RenderItemSink] malformed Mesh item: pass='%s' phase='%s' component='%s' geometry=%d has null mesh",
            pass_name,
            phase_mark,
            component_type,
            item.geometry_id);
        return false;
    }

    if ((item.flags & TC_RENDER_ITEM_FLAG_HAS_MATERIAL_PHASE) && !item.material_phase) {
        tc::Log::error(
            "[RenderItemSink] malformed %s item: pass='%s' phase='%s' component='%s' geometry=%d has material flag without material phase",
            render_item_kind_name(item.kind),
            pass_name,
            phase_mark,
            component_type,
            item.geometry_id);
        return false;
    }

    return true;
}

struct RenderItemVectorSinkData {
    std::vector<tc_render_item>* items = nullptr;
    const tc_render_item_collect_context* context = nullptr;
    tc_component* component = nullptr;
};

bool emit_render_item_to_vector(const tc_render_item* item, void* user_data) {
    auto* data = static_cast<RenderItemVectorSinkData*>(user_data);
    if (!data || !data->items || !data->context || !item) {
        tc::Log::error("[RenderItemSink] invalid vector sink callback state");
        return false;
    }

    if (!validate_render_item(*item, *data->context, data->component)) {
        return true;
    }

    tc_render_item copy = *item;
    if (!copy.component) {
        copy.component = data->component;
    }
    data->items->push_back(copy);
    return true;
}

} // namespace

bool collect_drawable_render_items(
    tc_component* component,
    const tc_render_item_collect_context& context,
    std::vector<tc_render_item>& out_items)
{
    if (!component) {
        tc::Log::error("[RenderItemCollector] cannot collect from null component");
        return false;
    }
    if (!context.phase_mark || context.phase_mark[0] == '\0') {
        tc::Log::error(
            "[RenderItemCollector] component '%s' collection requested with empty phase mark",
            component_debug_name(component));
        return false;
    }

    RenderItemVectorSinkData sink_data;
    sink_data.items = &out_items;
    sink_data.context = &context;
    sink_data.component = component;

    tc_render_item_sink sink;
    sink.emit = emit_render_item_to_vector;
    sink.user_data = &sink_data;
    return tc_component_collect_render_items(component, &context, &sink);
}

void Drawable::_cb_collect_shader_usages(
    tc_component* c,
    const char* phase_mark,
    int geometry_id,
    tc_shader_handle original_shader,
    tc_shader_usage_emit_fn emit,
    void* user_data
) {
    if (!c || !emit) return;

    Drawable* drawable = static_cast<Drawable*>(tc_component_get_drawable_userdata(c));
    if (!drawable) {
        emit(c, original_shader, user_data);
        return;
    }

    drawable->collect_shader_usages(
        phase_mark ? phase_mark : "",
        geometry_id,
        TcShader(original_shader),
        [c, emit, user_data](TcShader shader) {
            emit(c, shader.handle, user_data);
        }
    );
}

bool Drawable::_cb_collect_render_items(
    tc_component* c,
    const tc_render_item_collect_context* context,
    tc_render_item_sink* sink
) {
    if (!c || !context || !sink || !sink->emit) {
        return false;
    }

    Drawable* drawable = static_cast<Drawable*>(tc_component_get_drawable_userdata(c));
    if (!drawable) {
        return false;
    }

    return drawable->collect_render_items(*context, *sink);
}

Mat44f Drawable::get_model_matrix(const Entity& entity) const {
    double m[16];
    entity.transform().world_matrix(m);

    Mat44f result;
    for (int i = 0; i < 16; ++i) {
        result.data[i] = static_cast<float>(m[i]);
    }

    return result;
}

const tc_drawable_vtable& Drawable::cxx_drawable_vtable() {
    static const tc_drawable_vtable vtable = {
        &Drawable::_cb_has_phase,
        &Drawable::_cb_draw_geometry,
        &Drawable::_cb_get_geometry_draws,
        &Drawable::_cb_get_geometry_ids_for_phase,
        &Drawable::_cb_override_shader,
        &Drawable::_cb_collect_shader_usages,
        &Drawable::_cb_collect_render_items
    };
    return vtable;
}

} // namespace termin
