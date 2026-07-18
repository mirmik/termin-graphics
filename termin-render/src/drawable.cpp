#include <termin/render/drawable.hpp>

#include <tcbase/tc_log.hpp>
#include <cstring>
#include <vector>

namespace termin {

tc_phase_mask Drawable::_cb_phase_mask(tc_component* c) {
    if (!c) return TC_PHASE_NONE;

    Drawable* drawable = static_cast<Drawable*>(tc_component_get_drawable_userdata(c));
    if (!drawable) return TC_PHASE_NONE;

    return drawable->get_phase_mask();
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
    const char* phase_mark = tc_phase_name(context.phase);
    if (!phase_mark) {
        phase_mark = context.phase == TC_PHASE_NONE ? "<snapshot>" : "<invalid>";
    }
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
    const tc_render_item_vec3* last_line_source = nullptr;
    size_t last_line_count = 0;
    const tc_render_item_vec3* last_line_stored = nullptr;
    const char* last_text_source = nullptr;
    const char* last_text_stored = nullptr;
    const char* last_font_source = nullptr;
    const char* last_font_stored = nullptr;
    const char* last_foliage_source = nullptr;
    const char* last_foliage_stored = nullptr;
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
        const size_t count = copy.payload.line_batch.point_count;
        if (begin == data->last_line_source && count == data->last_line_count) {
            copy.payload.line_batch.points = data->last_line_stored;
        } else {
            const size_t storage_index = data->collection->active_line_batch_points++;
            if (storage_index == data->collection->line_batch_points.size()) {
                data->collection->line_batch_points.emplace_back();
            }
            auto& stored_points = data->collection->line_batch_points[storage_index];
            stored_points.assign(begin, begin + count);
            copy.payload.line_batch.points = stored_points.data();
            data->last_line_source = begin;
            data->last_line_count = count;
            data->last_line_stored = stored_points.data();
        }
    }
    if (copy.kind == TC_RENDER_ITEM_KIND_TEXT_BATCH) {
        if (copy.payload.text_batch.text) {
            const char* source = copy.payload.text_batch.text;
            if (source == data->last_text_source) {
                copy.payload.text_batch.text = data->last_text_stored;
            } else {
                const size_t index = data->collection->active_text_batch_strings++;
                if (index == data->collection->text_batch_strings.size()) {
                    data->collection->text_batch_strings.push_back(
                        std::make_unique<std::string>());
                }
                std::string& stored = *data->collection->text_batch_strings[index];
                stored.assign(source);
                copy.payload.text_batch.text = stored.c_str();
                data->last_text_source = source;
                data->last_text_stored = stored.c_str();
            }
        }
        if (copy.payload.text_batch.font_path) {
            const char* source = copy.payload.text_batch.font_path;
            if (source == data->last_font_source) {
                copy.payload.text_batch.font_path = data->last_font_stored;
            } else {
                const size_t index = data->collection->active_text_batch_strings++;
                if (index == data->collection->text_batch_strings.size()) {
                    data->collection->text_batch_strings.push_back(
                        std::make_unique<std::string>());
                }
                std::string& stored = *data->collection->text_batch_strings[index];
                stored.assign(source);
                copy.payload.text_batch.font_path = stored.c_str();
                data->last_font_source = source;
                data->last_font_stored = stored.c_str();
            }
        }
    }
    if (copy.kind == TC_RENDER_ITEM_KIND_FOLIAGE_BATCH &&
        copy.payload.foliage_batch.foliage_uuid) {
        const char* source = copy.payload.foliage_batch.foliage_uuid;
        if (source == data->last_foliage_source) {
            copy.payload.foliage_batch.foliage_uuid = data->last_foliage_stored;
        } else {
            const size_t index = data->collection->active_foliage_batch_strings++;
            if (index == data->collection->foliage_batch_strings.size()) {
                data->collection->foliage_batch_strings.push_back(
                    std::make_unique<std::string>());
            }
            std::string& stored = *data->collection->foliage_batch_strings[index];
            stored.assign(source);
            copy.payload.foliage_batch.foliage_uuid = stored.c_str();
            data->last_foliage_source = source;
            data->last_foliage_stored = stored.c_str();
        }
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
    RenderItemVectorSinkData sink_data;
    sink_data.collection = &out_collection;
    sink_data.context = &context;
    sink_data.component = component;

    tc_render_item_sink sink;
    sink.emit = emit_render_item_to_vector;
    sink.user_data = &sink_data;
    return tc_component_collect_render_items(component, &context, &sink);
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
        &Drawable::_cb_phase_mask,
        &Drawable::_cb_collect_render_items
    };
    return vtable;
}

} // namespace termin
