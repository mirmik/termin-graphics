#include <termin/gui_native/tc_ui_serialization.h>

#include <termin/gui_native/tc_ui_snapshot.h>
#include <termin/gui_native/tc_widget_registry.h>

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include <tcbase/tc_log.h>

static tc_value serialize_float_list(const float* values, size_t count) {
    tc_value result = tc_value_list_new();
    size_t index;
    for (index = 0; index < count; ++index) {
        tc_value_list_push(&result, tc_value_double(values[index]));
    }
    return result;
}

static tc_value serialize_rect(tc_ui_rect value) {
    const float values[] = {value.x, value.y, value.width, value.height};
    return serialize_float_list(values, 4);
}

static tc_value serialize_size(tc_ui_size value) {
    const float values[] = {value.width, value.height};
    return serialize_float_list(values, 2);
}

static tc_value serialize_color(tc_ui_color value) {
    const float values[] = {value.r, value.g, value.b, value.a};
    return serialize_float_list(values, 4);
}

static tc_value serialize_style(tc_ui_style value) {
    tc_value result = tc_value_dict_new();
    tc_value_dict_set(&result, "background", serialize_color(value.background));
    tc_value_dict_set(&result, "foreground", serialize_color(value.foreground));
    tc_value_dict_set(&result, "border", serialize_color(value.border));
    tc_value_dict_set(&result, "accent", serialize_color(value.accent));
    tc_value_dict_set(&result, "padding_left", tc_value_double(value.padding_left));
    tc_value_dict_set(&result, "padding_top", tc_value_double(value.padding_top));
    tc_value_dict_set(&result, "padding_right", tc_value_double(value.padding_right));
    tc_value_dict_set(&result, "padding_bottom", tc_value_double(value.padding_bottom));
    tc_value_dict_set(&result, "spacing", tc_value_double(value.spacing));
    tc_value_dict_set(&result, "border_width", tc_value_double(value.border_width));
    tc_value_dict_set(&result, "font_size", tc_value_double(value.font_size));
    tc_value_dict_set(&result, "min_width", tc_value_double(value.min_width));
    tc_value_dict_set(&result, "min_height", tc_value_double(value.min_height));
    tc_value_dict_set(&result, "font_role", tc_value_int(value.font_role));
    return result;
}

static tc_value serialize_common(const tc_ui_widget_snapshot* widget) {
    const uint32_t persisted_flags =
        TC_WIDGET_FOCUSABLE | TC_WIDGET_VISIBLE | TC_WIDGET_ENABLED | TC_WIDGET_MOUSE_TRANSPARENT;
    tc_value result = tc_value_dict_new();
    tc_value style_override = tc_value_dict_new();
    tc_value_dict_set(&result, "stable_id",
                      tc_value_string(widget->stable_id ? widget->stable_id : ""));
    tc_value_dict_set(&result, "name", tc_value_string(widget->name ? widget->name : ""));
    tc_value_dict_set(&result, "debug_name",
                      tc_value_string(widget->debug_name ? widget->debug_name : ""));
    tc_value_dict_set(&result, "bounds", serialize_rect(widget->bounds));
    tc_value_dict_set(&result, "min_size", serialize_size(widget->min_size));
    tc_value_dict_set(&result, "preferred_size", serialize_size(widget->preferred_size));
    tc_value_dict_set(&result, "max_size", serialize_size(widget->max_size));
    tc_value_dict_set(&result, "flags", tc_value_int(widget->flags & persisted_flags));
    tc_value_dict_set(&result, "style_role", tc_value_int(widget->style_role));
    tc_value_dict_set(&style_override, "fields",
                      tc_value_int((int64_t)widget->style_override.fields));
    tc_value_dict_set(&style_override, "flags", tc_value_int(widget->style_override.flags));
    tc_value_dict_set(&style_override, "value", serialize_style(widget->style_override.value));
    tc_value_dict_set(&result, "style_override", style_override);
    return result;
}

static bool find_widget_index(const tc_ui_document_inspect_snapshot* snapshot,
                              tc_widget_handle handle, size_t* out_index) {
    size_t index;
    for (index = 0; index < snapshot->widget_count; ++index) {
        if (tc_widget_handle_eq(snapshot->widgets[index].handle, handle)) {
            *out_index = index;
            return true;
        }
    }
    return false;
}

tc_value tc_ui_document_serialize(const tc_ui_document* document) {
    tc_ui_document_inspect_snapshot snapshot = {0};
    tc_value result = tc_value_nil();
    tc_value widgets;
    tc_value roots;
    tc_value overlays;
    size_t index;
    if (!tc_ui_document_capture_snapshot(document, &snapshot)) {
        return result;
    }

    result = tc_value_dict_new();
    widgets = tc_value_list_new();
    roots = tc_value_list_new();
    overlays = tc_value_list_new();
    tc_value_dict_set(&result, "$schema", tc_value_string(TC_UI_DOCUMENT_SCHEMA));
    tc_value_dict_set(&result, "version", tc_value_int(TC_UI_DOCUMENT_SCHEMA_VERSION));

    for (index = 0; index < snapshot.widget_count; ++index) {
        const tc_ui_widget_snapshot* widget_snapshot = &snapshot.widgets[index];
        const tc_widget* widget =
            tc_ui_document_resolve_widget_const(document, widget_snapshot->handle);
        tc_value record = tc_value_dict_new();
        tc_value children = tc_value_list_new();
        tc_value state = tc_value_nil();
        size_t child;
        if (!widget || !widget->runtime_type_link.type_name) {
            tc_log_error("[termin-gui-native] cannot serialize unregistered widget at record %zu",
                         index);
            tc_value_free(&record);
            tc_value_free(&children);
            tc_value_free(&widgets);
            tc_value_free(&roots);
            tc_value_free(&overlays);
            tc_value_free(&result);
            tc_ui_document_snapshot_destroy(&snapshot);
            return tc_value_nil();
        }
        if (!tc_widget_registry_serialize_state(widget, &state)) {
            tc_log_error("[termin-gui-native] cannot serialize state for widget record %zu", index);
            tc_value_free(&record);
            tc_value_free(&children);
            tc_value_free(&widgets);
            tc_value_free(&roots);
            tc_value_free(&overlays);
            tc_value_free(&result);
            tc_ui_document_snapshot_destroy(&snapshot);
            return tc_value_nil();
        }
        tc_value_dict_set(&record, "type", tc_value_string(widget->runtime_type_link.type_name));
        tc_value_dict_set(&record, "common", serialize_common(widget_snapshot));
        tc_value_dict_set(&record, "state", state);
        for (child = 0; child < widget_snapshot->child_count; ++child) {
            size_t child_record;
            tc_widget_handle child_handle =
                snapshot.children[widget_snapshot->child_offset + child];
            if (!find_widget_index(&snapshot, child_handle, &child_record)) {
                tc_log_error("[termin-gui-native] snapshot child is missing from widget records");
                tc_value_free(&record);
                tc_value_free(&children);
                tc_value_free(&widgets);
                tc_value_free(&roots);
                tc_value_free(&overlays);
                tc_value_free(&result);
                tc_ui_document_snapshot_destroy(&snapshot);
                return tc_value_nil();
            }
            tc_value_list_push(&children, tc_value_int((int64_t)child_record));
        }
        tc_value_dict_set(&record, "children", children);
        tc_value_list_push(&widgets, record);
    }

    for (index = 0; index < snapshot.root_count; ++index) {
        size_t record;
        if (!find_widget_index(&snapshot, snapshot.roots[index], &record)) {
            tc_log_error("[termin-gui-native] snapshot root is missing from widget records");
            goto serialization_failure;
        }
        tc_value_list_push(&roots, tc_value_int((int64_t)record));
    }
    for (index = 0; index < snapshot.overlay_count; ++index) {
        size_t record;
        tc_value overlay = tc_value_dict_new();
        if (!find_widget_index(&snapshot, snapshot.overlays[index].handle, &record)) {
            tc_log_error("[termin-gui-native] snapshot overlay is missing from widget records");
            tc_value_free(&overlay);
            goto serialization_failure;
        }
        tc_value_dict_set(&overlay, "widget", tc_value_int((int64_t)record));
        tc_value_dict_set(&overlay, "flags", tc_value_int(snapshot.overlays[index].flags));
        tc_value_list_push(&overlays, overlay);
    }
    tc_value_dict_set(&result, "widgets", widgets);
    tc_value_dict_set(&result, "roots", roots);
    tc_value_dict_set(&result, "overlays", overlays);
    tc_ui_document_snapshot_destroy(&snapshot);
    return result;

serialization_failure:
    tc_value_free(&widgets);
    tc_value_free(&roots);
    tc_value_free(&overlays);
    tc_value_free(&result);
    tc_ui_document_snapshot_destroy(&snapshot);
    return tc_value_nil();
}

static tc_value* required_value(const tc_value* dict, const char* key, tc_value_type type) {
    tc_value* value;
    if (!dict || dict->type != TC_VALUE_DICT ||
        !(value = tc_value_dict_get((tc_value*)dict, key)) || value->type != type) {
        tc_log_error("[termin-gui-native] serialized UI field '%s' is missing or has wrong type",
                     key);
        return NULL;
    }
    return value;
}

static bool read_float_value(const tc_value* value, float* out) {
    double number;
    if (!value || !out) {
        return false;
    }
    switch (value->type) {
    case TC_VALUE_INT:
        number = (double)value->data.i;
        break;
    case TC_VALUE_FLOAT:
        number = value->data.f;
        break;
    case TC_VALUE_DOUBLE:
        number = value->data.d;
        break;
    default:
        return false;
    }
    if (!isfinite(number) || number < -3.402823466e+38 || number > 3.402823466e+38) {
        return false;
    }
    *out = (float)number;
    return true;
}

static bool read_float_list(const tc_value* value, float* out, size_t count) {
    size_t index;
    if (!value || value->type != TC_VALUE_LIST || tc_value_list_size(value) != count) {
        return false;
    }
    for (index = 0; index < count; ++index) {
        if (!read_float_value(tc_value_list_get((tc_value*)value, index), &out[index])) {
            return false;
        }
    }
    return true;
}

static bool read_style(const tc_value* data, tc_ui_style* out) {
    tc_value* value;
    float color[4];
#define READ_STYLE_FLOAT(field)                                                                    \
    do {                                                                                           \
        value = tc_value_dict_get((tc_value*)data, #field);                                        \
        if (!read_float_value(value, &out->field))                                                 \
            return false;                                                                          \
    } while (0)
#define READ_STYLE_COLOR(field)                                                                    \
    do {                                                                                           \
        value = tc_value_dict_get((tc_value*)data, #field);                                        \
        if (!read_float_list(value, color, 4))                                                     \
            return false;                                                                          \
        out->field = (tc_ui_color){color[0], color[1], color[2], color[3]};                        \
    } while (0)
    if (!data || data->type != TC_VALUE_DICT || !out) {
        return false;
    }
    READ_STYLE_COLOR(background);
    READ_STYLE_COLOR(foreground);
    READ_STYLE_COLOR(border);
    READ_STYLE_COLOR(accent);
    READ_STYLE_FLOAT(padding_left);
    READ_STYLE_FLOAT(padding_top);
    READ_STYLE_FLOAT(padding_right);
    READ_STYLE_FLOAT(padding_bottom);
    READ_STYLE_FLOAT(spacing);
    READ_STYLE_FLOAT(border_width);
    READ_STYLE_FLOAT(font_size);
    READ_STYLE_FLOAT(min_width);
    READ_STYLE_FLOAT(min_height);
    value = required_value(data, "font_role", TC_VALUE_INT);
    if (!value || value->data.i < TC_UI_FONT_BODY || value->data.i > TC_UI_FONT_MONOSPACE) {
        return false;
    }
    out->font_role = (tc_ui_font_role)value->data.i;
    return true;
#undef READ_STYLE_COLOR
#undef READ_STYLE_FLOAT
}

static bool apply_common_state(tc_widget* widget, const tc_value* common) {
    const uint32_t persisted_flags =
        TC_WIDGET_FOCUSABLE | TC_WIDGET_VISIBLE | TC_WIDGET_ENABLED | TC_WIDGET_MOUSE_TRANSPARENT;
    tc_value* stable_id = required_value(common, "stable_id", TC_VALUE_STRING);
    tc_value* name = required_value(common, "name", TC_VALUE_STRING);
    tc_value* debug_name = required_value(common, "debug_name", TC_VALUE_STRING);
    tc_value* bounds = required_value(common, "bounds", TC_VALUE_LIST);
    tc_value* min_size = required_value(common, "min_size", TC_VALUE_LIST);
    tc_value* preferred_size = required_value(common, "preferred_size", TC_VALUE_LIST);
    tc_value* max_size = required_value(common, "max_size", TC_VALUE_LIST);
    tc_value* flags = required_value(common, "flags", TC_VALUE_INT);
    tc_value* style_role = required_value(common, "style_role", TC_VALUE_INT);
    tc_value* override = required_value(common, "style_override", TC_VALUE_DICT);
    tc_value* override_fields;
    tc_value* override_flags;
    tc_value* override_value;
    tc_ui_rect parsed_bounds;
    tc_ui_size parsed_min;
    tc_ui_size parsed_preferred;
    tc_ui_size parsed_max;
    tc_ui_style_override parsed_override = {0};
    float parsed_rect_values[4];
    float parsed_min_values[2];
    float parsed_preferred_values[2];
    float parsed_max_values[2];
    if (!widget || !stable_id || !name || !debug_name || !bounds || !min_size || !preferred_size ||
        !max_size || !flags || !style_role || !override || flags->data.i < 0 ||
        ((uint64_t)flags->data.i & ~persisted_flags) != 0 || style_role->data.i < 0 ||
        style_role->data.i >= TC_UI_STYLE_ROLE_COUNT) {
        return false;
    }
    if (!read_float_list(bounds, parsed_rect_values, 4) ||
        !read_float_list(min_size, parsed_min_values, 2) ||
        !read_float_list(preferred_size, parsed_preferred_values, 2) ||
        !read_float_list(max_size, parsed_max_values, 2)) {
        return false;
    }
    parsed_bounds = (tc_ui_rect){parsed_rect_values[0], parsed_rect_values[1],
                                 parsed_rect_values[2], parsed_rect_values[3]};
    parsed_min = (tc_ui_size){parsed_min_values[0], parsed_min_values[1]};
    parsed_preferred = (tc_ui_size){parsed_preferred_values[0], parsed_preferred_values[1]};
    parsed_max = (tc_ui_size){parsed_max_values[0], parsed_max_values[1]};
    override_fields = required_value(override, "fields", TC_VALUE_INT);
    override_flags = required_value(override, "flags", TC_VALUE_INT);
    override_value = required_value(override, "value", TC_VALUE_DICT);
    if (!override_fields || !override_flags || !override_value || override_fields->data.i < 0 ||
        ((uint64_t)override_fields->data.i & ~TC_UI_STYLE_ALL_FIELDS) != 0 ||
        override_flags->data.i < 0 ||
        ((uint64_t)override_flags->data.i & ~TC_UI_STYLE_OVERRIDE_INHERIT) != 0 ||
        !read_style(override_value, &parsed_override.value)) {
        return false;
    }
    parsed_override.fields = (tc_ui_style_field_mask)override_fields->data.i;
    parsed_override.flags = (uint32_t)override_flags->data.i;
    if (!tc_widget_set_stable_id(widget, stable_id->data.s) ||
        !tc_widget_set_name(widget, name->data.s) ||
        !tc_widget_set_debug_name(widget, debug_name->data.s)) {
        return false;
    }
    tc_widget_set_bounds(widget, parsed_bounds);
    tc_widget_set_min_size(widget, parsed_min);
    tc_widget_set_preferred_size(widget, parsed_preferred);
    tc_widget_set_max_size(widget, parsed_max);
    tc_widget_set_focusable(widget, (flags->data.i & TC_WIDGET_FOCUSABLE) != 0);
    tc_widget_set_visible(widget, (flags->data.i & TC_WIDGET_VISIBLE) != 0);
    tc_widget_set_enabled(widget, (flags->data.i & TC_WIDGET_ENABLED) != 0);
    tc_widget_set_mouse_transparent(widget, (flags->data.i & TC_WIDGET_MOUSE_TRANSPARENT) != 0);
    tc_widget_set_style_role(widget, (tc_ui_style_role)style_role->data.i);
    return tc_widget_set_style_override(widget, &parsed_override);
}

static bool read_record_index(const tc_value* value, size_t count, size_t* out_index) {
    if (!value || value->type != TC_VALUE_INT || value->data.i < 0 ||
        (uint64_t)value->data.i >= count) {
        return false;
    }
    *out_index = (size_t)value->data.i;
    return true;
}

static void rollback_restored_widgets(tc_ui_document* document, tc_widget_handle* handles,
                                      size_t count) {
    while (count > 0) {
        tc_widget_handle handle = handles[--count];
        if (tc_ui_document_is_alive(document, handle)) {
            tc_ui_document_destroy_widget_recursive(document, handle);
        }
    }
}

bool tc_ui_document_restore(tc_ui_document* document, const tc_value* serialized) {
    tc_value* schema;
    tc_value* version;
    tc_value* widgets;
    tc_value* roots;
    tc_value* overlays;
    tc_widget_handle* handles = NULL;
    size_t widget_count;
    size_t index;
    if (!document || !serialized || tc_ui_document_live_widget_count(document) != 0) {
        tc_log_error(
            "[termin-gui-native] UI restore requires an empty document and serialized data");
        return false;
    }
    schema = required_value(serialized, "$schema", TC_VALUE_STRING);
    version = required_value(serialized, "version", TC_VALUE_INT);
    widgets = required_value(serialized, "widgets", TC_VALUE_LIST);
    roots = required_value(serialized, "roots", TC_VALUE_LIST);
    overlays = required_value(serialized, "overlays", TC_VALUE_LIST);
    if (!schema || !version || !widgets || !roots || !overlays ||
        strcmp(schema->data.s, TC_UI_DOCUMENT_SCHEMA) != 0 ||
        version->data.i != TC_UI_DOCUMENT_SCHEMA_VERSION) {
        tc_log_error("[termin-gui-native] unsupported UI document serialization schema");
        return false;
    }
    widget_count = tc_value_list_size(widgets);
    if (widget_count) {
        handles = (tc_widget_handle*)calloc(widget_count, sizeof(tc_widget_handle));
        if (!handles) {
            tc_log_error("[termin-gui-native] failed to allocate UI restore handle map");
            return false;
        }
    }
    for (index = 0; index < widget_count; ++index) {
        tc_value* record = tc_value_list_get(widgets, index);
        tc_value* type = required_value(record, "type", TC_VALUE_STRING);
        tc_value* common = required_value(record, "common", TC_VALUE_DICT);
        tc_value* state = required_value(record, "state", TC_VALUE_DICT);
        tc_value* children = required_value(record, "children", TC_VALUE_LIST);
        tc_widget* widget;
        if (!type || !common || !state || !children || !tc_widget_registry_has(type->data.s)) {
            tc_log_error("[termin-gui-native] invalid or unavailable widget record %zu", index);
            goto restore_failure;
        }
        handles[index] = tc_ui_document_create_registered_widget(document, type->data.s);
        widget = tc_ui_document_resolve_widget(document, handles[index]);
        if (!widget || !apply_common_state(widget, common) ||
            !tc_widget_registry_deserialize_state(widget, state)) {
            tc_log_error("[termin-gui-native] failed to restore widget record %zu", index);
            goto restore_failure;
        }
    }
    for (index = 0; index < widget_count; ++index) {
        tc_value* record = tc_value_list_get(widgets, index);
        tc_value* children = tc_value_dict_get(record, "children");
        size_t child;
        for (child = 0; child < tc_value_list_size(children); ++child) {
            size_t child_record;
            if (!read_record_index(tc_value_list_get(children, child), widget_count,
                                   &child_record) ||
                !tc_widget_append_child(
                    tc_ui_document_resolve_widget(document, handles[index]),
                    tc_ui_document_resolve_widget(document, handles[child_record]))) {
                tc_log_error("[termin-gui-native] invalid child relation at widget record %zu",
                             index);
                goto restore_failure;
            }
        }
    }
    for (index = 0; index < tc_value_list_size(roots); ++index) {
        size_t record;
        if (!read_record_index(tc_value_list_get(roots, index), widget_count, &record) ||
            !tc_ui_document_add_root(document, handles[record])) {
            tc_log_error("[termin-gui-native] invalid root relation in UI serialization");
            goto restore_failure;
        }
    }
    for (index = 0; index < tc_value_list_size(overlays); ++index) {
        tc_value* overlay = tc_value_list_get(overlays, index);
        tc_value* widget_value = required_value(overlay, "widget", TC_VALUE_INT);
        tc_value* flags = required_value(overlay, "flags", TC_VALUE_INT);
        size_t record;
        if (!widget_value || !flags || flags->data.i < 0 || flags->data.i > UINT32_MAX ||
            !read_record_index(widget_value, widget_count, &record) ||
            !tc_ui_document_show_overlay(document, handles[record], (uint32_t)flags->data.i)) {
            tc_log_error("[termin-gui-native] invalid overlay relation in UI serialization");
            goto restore_failure;
        }
    }
    free(handles);
    return true;

restore_failure:
    rollback_restored_widgets(document, handles, widget_count);
    free(handles);
    return false;
}
