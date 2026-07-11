#include <termin/render/drawable.hpp>

#include <tcbase/tc_log.hpp>
#include <cstring>
#include <vector>

namespace termin {

bool Drawable::_cb_has_phase(tc_component* c, const char* phase_mark) {
    if (!c) return false;

    Drawable* drawable = static_cast<Drawable*>(tc_component_get_drawable_userdata(c));
    if (!drawable) return false;

    return drawable->has_phase(phase_mark ? phase_mark : "");
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
    (void)context;
    if (!sink.emit) {
        tc::Log::error("[Drawable] cannot emit render items: sink callback is null");
        return false;
    }
    auto* component = dynamic_cast<Component*>(this);
    const char* type_name = component ? tc_component_type_name(component->tc_component_ptr()) : "<non-component>";
    tc::Log::error(
        "[Drawable] component '%s' does not implement collect_render_items",
        type_name ? type_name : "<unknown>");
    return false;
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

    if (item.kind == TC_RENDER_ITEM_KIND_LINE_BATCH &&
        (!item.payload.line_batch.points || item.payload.line_batch.point_count < 2)) {
        tc::Log::error(
            "[RenderItemSink] malformed LineBatch item: pass='%s' phase='%s' component='%s' geometry=%d has no drawable points",
            pass_name,
            phase_mark,
            component_type,
            item.geometry_id);
        return false;
    }

    if (item.kind == TC_RENDER_ITEM_KIND_TEXT_BATCH &&
        (!item.payload.text_batch.text || item.payload.text_batch.text[0] == '\0')) {
        tc::Log::error(
            "[RenderItemSink] malformed TextBatch item: pass='%s' phase='%s' component='%s' geometry=%d has empty text",
            pass_name,
            phase_mark,
            component_type,
            item.geometry_id);
        return false;
    }

    if (item.kind == TC_RENDER_ITEM_KIND_FOLIAGE_BATCH &&
        (!item.payload.foliage_batch.prototype_mesh ||
         !item.payload.foliage_batch.foliage_uuid ||
         item.payload.foliage_batch.foliage_uuid[0] == '\0')) {
        tc::Log::error(
            "[RenderItemSink] malformed FoliageBatch item: pass='%s' phase='%s' component='%s' geometry=%d has no prototype mesh or foliage asset",
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

    if ((item.flags & TC_RENDER_ITEM_FLAG_HAS_INLINE_UNIFORM) != 0u &&
        (item.inline_uniform.name[0] == '\0' ||
         std::memchr(
             item.inline_uniform.name,
             '\0',
             TC_RENDER_ITEM_INLINE_UNIFORM_NAME_CAPACITY) == nullptr ||
         item.inline_uniform.size == 0u ||
         item.inline_uniform.size > TC_RENDER_ITEM_INLINE_UNIFORM_DATA_CAPACITY)) {
        tc::Log::error(
            "[RenderItemSink] malformed %s item: pass='%s' phase='%s' component='%s' geometry=%d has invalid inline uniform name/size (%u)",
            render_item_kind_name(item.kind),
            pass_name,
            phase_mark,
            component_type,
            item.geometry_id,
            item.inline_uniform.size);
        return false;
    }

    return true;
}

struct RenderItemVectorSinkData {
    RenderItemCollection* collection = nullptr;
    const tc_render_item_collect_context* context = nullptr;
    tc_component* component = nullptr;
};

bool emit_render_item_to_vector(const tc_render_item* item, void* user_data) {
    auto* data = static_cast<RenderItemVectorSinkData*>(user_data);
    if (!data || !data->collection || !data->context || !item) {
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
    if (copy.kind == TC_RENDER_ITEM_KIND_LINE_BATCH &&
        copy.payload.line_batch.points &&
        copy.payload.line_batch.point_count > 0) {
        const tc_render_item_vec3* begin = copy.payload.line_batch.points;
        const tc_render_item_vec3* end = begin + copy.payload.line_batch.point_count;
        auto& stored_points = data->collection->line_batch_points.emplace_back(begin, end);
        copy.payload.line_batch.points = stored_points.data();
    }
    if (copy.kind == TC_RENDER_ITEM_KIND_TEXT_BATCH) {
        if (copy.payload.text_batch.text) {
            auto stored_text =
                std::make_unique<std::string>(copy.payload.text_batch.text);
            copy.payload.text_batch.text = stored_text->c_str();
            data->collection->text_batch_strings.push_back(std::move(stored_text));
        }
        if (copy.payload.text_batch.font_path) {
            auto stored_font_path =
                std::make_unique<std::string>(copy.payload.text_batch.font_path);
            copy.payload.text_batch.font_path = stored_font_path->c_str();
            data->collection->text_batch_strings.push_back(std::move(stored_font_path));
        }
    }
    if (copy.kind == TC_RENDER_ITEM_KIND_FOLIAGE_BATCH &&
        copy.payload.foliage_batch.foliage_uuid) {
        auto stored_uuid =
            std::make_unique<std::string>(copy.payload.foliage_batch.foliage_uuid);
        copy.payload.foliage_batch.foliage_uuid = stored_uuid->c_str();
        data->collection->foliage_batch_strings.push_back(std::move(stored_uuid));
    }
    data->collection->items.push_back(copy);
    return true;
}

} // namespace

bool collect_drawable_render_items(
    tc_component* component,
    const tc_render_item_collect_context& context,
    RenderItemCollection& out_collection)
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
    sink_data.collection = &out_collection;
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
        &Drawable::_cb_override_shader,
        &Drawable::_cb_collect_shader_usages,
        &Drawable::_cb_collect_render_items
    };
    return vtable;
}

} // namespace termin
