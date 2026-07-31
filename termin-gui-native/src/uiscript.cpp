#include <termin/gui_native/uiscript.hpp>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>
#include <unordered_set>

#include <tcbase/tc_log.h>
#include <tcbase/tc_trent_yaml.hpp>
#include <termin/gui_native/builtin_widget_registration.hpp>
#include <termin/gui_native/tc_uiscript.h>
#include <termin/gui_native/tc_widget_registry.h>

namespace termin::gui_native {
namespace {

[[noreturn]] void fail(const std::string& path, const std::string& message) {
    throw UiScriptError(path + ": " + message);
}

bool is_number(tc::trent_view value) {
    return value.is_numer() && !value.is_bool();
}

void validate_number(
    tc::trent_view value,
    const std::string& path,
    bool nonnegative = false
) {
    if (!is_number(value)) {
        fail(path, "expected a number");
    }
    if (nonnegative && value.as_numer() < 0.0) {
        fail(path, "expected a value >= 0");
    }
}

void validate_color(tc::trent_view value, const std::string& path) {
    if (!value.is_list() || (value.size() != 3 && value.size() != 4)) {
        fail(path, "expected an RGB or RGBA list");
    }
    for (size_t index = 0; index < value.size(); ++index) {
        tc::trent_view channel = value[index];
        validate_number(channel, path + "[" + std::to_string(index) + "]");
        if (channel.as_numer() < 0.0 || channel.as_numer() > 1.0) {
            fail(path, "color channels must be in the [0, 1] range");
        }
    }
}

tc_ui_length parse_layout_length(tc::trent_view value, const std::string& path) {
    if (is_number(value)) {
        validate_number(value, path, true);
        return tc_ui_length{
            TC_UI_LENGTH_FIXED, static_cast<float>(value.as_numer())};
    }
    if (!value.is_string()) {
        fail(path, "expected 'auto', 'fill', a non-negative number, or a percentage");
    }
    const std::string text = value.as_string();
    if (text == "auto") {
        return tc_ui_length{TC_UI_LENGTH_AUTO, 0.0f};
    }
    if (text == "fill") {
        return tc_ui_length{TC_UI_LENGTH_FILL, 0.0f};
    }
    if (text.size() > 1 && text.back() == '%') {
        try {
            size_t consumed = 0;
            const double percent = std::stod(text.substr(0, text.size() - 1), &consumed);
            if (consumed != text.size() - 1 || !std::isfinite(percent) ||
                percent < 0.0 || percent > 100.0) {
                fail(path, "percentage must be in the [0%, 100%] range");
            }
            return tc_ui_length{
                TC_UI_LENGTH_PERCENT, static_cast<float>(percent / 100.0)};
        } catch (const std::invalid_argument&) {
            fail(path, "invalid percentage length '" + text + "'");
        } catch (const std::out_of_range&) {
            fail(path, "percentage length is out of range");
        }
    }
    fail(path, "unsupported layout length '" + text + "'");
}

tc_ui_widget_layout_spec parse_layout_spec(
    tc::trent_view value,
    const std::string& path
) {
    if (!value.is_dict()) {
        fail(path, "expected a layout mapping");
    }
    tc_ui_widget_layout_spec spec = tc_ui_widget_layout_spec_default();
    static const std::unordered_set<std::string> supported{
        "width", "height", "min_width", "min_height", "max_width",
        "max_height", "margin", "aspect_ratio", "minimum_touch_target"};
    for (const auto entry : value.as_dict()) {
        const std::string key = entry.key ? entry.key : "";
        if (!supported.contains(key)) {
            fail(path, "unsupported layout property '" + key + "'");
        }
    }
    if (tc::trent_view field = value["width"]; field) {
        spec.width = parse_layout_length(field, path + ".width");
    }
    if (tc::trent_view field = value["height"]; field) {
        spec.height = parse_layout_length(field, path + ".height");
    }
#define READ_NONNEGATIVE_LAYOUT_FIELD(field)                                                   \
    if (tc::trent_view item = value[#field]; item) {                                           \
        validate_number(item, path + "." #field, true);                                        \
        spec.field = static_cast<float>(item.as_numer());                                      \
    }
    READ_NONNEGATIVE_LAYOUT_FIELD(min_width)
    READ_NONNEGATIVE_LAYOUT_FIELD(min_height)
    READ_NONNEGATIVE_LAYOUT_FIELD(max_width)
    READ_NONNEGATIVE_LAYOUT_FIELD(max_height)
#undef READ_NONNEGATIVE_LAYOUT_FIELD
    if (tc::trent_view margin = value["margin"]; margin) {
        if (is_number(margin)) {
            validate_number(margin, path + ".margin", true);
            const float all = static_cast<float>(margin.as_numer());
            spec.margin = tc_ui_insets{all, all, all, all};
        } else if (margin.is_list() && margin.size() == 4) {
            validate_number(margin[0], path + ".margin[0]", true);
            validate_number(margin[1], path + ".margin[1]", true);
            validate_number(margin[2], path + ".margin[2]", true);
            validate_number(margin[3], path + ".margin[3]", true);
            spec.margin = tc_ui_insets{
                static_cast<float>(margin[0].as_numer()),
                static_cast<float>(margin[1].as_numer()),
                static_cast<float>(margin[2].as_numer()),
                static_cast<float>(margin[3].as_numer())};
        } else {
            fail(path + ".margin", "expected a number or [left, top, right, bottom]");
        }
    }
    if (tc::trent_view ratio = value["aspect_ratio"]; ratio) {
        validate_number(ratio, path + ".aspect_ratio");
        if (ratio.as_numer() <= 0.0) {
            fail(path + ".aspect_ratio", "expected a value > 0");
        }
        spec.aspect_ratio = static_cast<float>(ratio.as_numer());
    }
    if (tc::trent_view target = value["minimum_touch_target"]; target) {
        if (target.is_bool()) {
            if (target.as_bool()) {
                fail(path + ".minimum_touch_target",
                     "true is ambiguous; provide a size or use false");
            }
        } else if (is_number(target)) {
            validate_number(target, path + ".minimum_touch_target", true);
            const float both = static_cast<float>(target.as_numer());
            spec.touch_target_policy = TC_UI_TOUCH_TARGET_LAYOUT_MINIMUM;
            spec.minimum_touch_target = tc_ui_size{both, both};
        } else if (target.is_list() && target.size() == 2) {
            validate_number(target[0], path + ".minimum_touch_target[0]", true);
            validate_number(target[1], path + ".minimum_touch_target[1]", true);
            spec.touch_target_policy = TC_UI_TOUCH_TARGET_LAYOUT_MINIMUM;
            spec.minimum_touch_target = tc_ui_size{
                static_cast<float>(target[0].as_numer()),
                static_cast<float>(target[1].as_numer())};
        } else {
            fail(path + ".minimum_touch_target",
                 "expected false, a size, or [width, height]");
        }
    }
    tc_ui_widget_layout_spec normalized;
    if (!tc_ui_widget_layout_spec_normalize(&spec, &normalized)) {
        fail(path, "invalid or over-constrained widget layout spec");
    }
    return normalized;
}

void validate_property(
    const std::string& name,
    tc::trent_view value,
    const std::string& path
) {
    if (name == "layout") {
        (void)parse_layout_spec(value, path);
        return;
    }
    if (name == "visible" || name == "enabled" || name == "active") {
        if (!value.is_bool()) fail(path, "expected a boolean");
        return;
    }
    if (name == "spacing" || name == "size" || name == "font_size" ||
        name == "border_radius") {
        validate_number(value, path, true);
        return;
    }
    if (name == "background_color" || name == "hover_color" ||
        name == "pressed_color" || name == "active_color" ||
        name == "icon_color" || name == "color") {
        validate_color(value, path);
        return;
    }
    if (name == "icon" || name == "tooltip" || name == "text") {
        if (!value.is_string()) fail(path, "expected a string");
        return;
    }
    if (name == "anchor") {
        if (!value.is_string()) fail(path, "expected an anchor string");
        const std::string anchor = value.as_string();
        if (anchor != "fill" && anchor != "top-left" &&
            anchor != "top-right" && anchor != "bottom-left" &&
            anchor != "bottom-right") {
            fail(path, "unsupported anchor '" + anchor + "'");
        }
        return;
    }
    if (name == "offset") {
        if (!value.is_list() || value.size() != 2) {
            fail(path, "expected a two-item list");
        }
        validate_number(value[0], path + "[0]");
        validate_number(value[1], path + "[1]");
        return;
    }
    fail(path, "property has no native UiScript validator");
}

bool contains_property(
    const tc_uiscript_type_descriptor& descriptor,
    const std::string& name
) {
    for (size_t index = 0; index < descriptor.property_count; ++index) {
        if (descriptor.properties[index] &&
            name == descriptor.properties[index]) {
            return true;
        }
    }
    return false;
}

UiScriptNode parse_node(
    tc::trent_view source,
    const std::string& path,
    std::unordered_set<std::string>& names,
    std::vector<std::string>& dependencies,
    std::unordered_set<std::string>& dependency_set
) {
    if (!source.is_dict()) {
        fail(path, "expected a widget mapping");
    }
    const tc::trent_view type_value = source["type"];
    if (!type_value.is_string() || type_value.as_string().empty()) {
        fail(path, "missing non-empty string property 'type'");
    }
    const std::string type_name = type_value.as_string();
    if (!tc_widget_registry_has(type_name.c_str())) {
        fail(path + ".type", "unknown registered widget type '" + type_name + "'");
    }
    const tc_uiscript_type_descriptor* descriptor =
        tc_uiscript_type_descriptor_get(type_name.c_str());
    if (!descriptor) {
        fail(path + ".type", "widget type '" + type_name +
            "' has no native UiScript contract");
    }
    if (dependency_set.insert(type_name).second) {
        dependencies.push_back(type_name);
    }

    UiScriptNode node;
    node.type_name = type_name;
    node.source_path = path;
    const tc::trent_view name_value = source["name"];
    if (name_value) {
        if (!name_value.is_string() || name_value.as_string().empty()) {
            fail(path + ".name", "expected a non-empty string");
        }
        node.name = name_value.as_string();
        if (!names.insert(node.name).second) {
            fail(path + ".name", "duplicate widget name '" + node.name + "'");
        }
    }

    static const std::unordered_set<std::string> structural{
        "type", "name", "children"};
    static const std::unordered_set<std::string> common{
        "visible", "enabled", "anchor", "offset", "layout"};
    for (const auto entry : source.as_dict()) {
        const std::string key = entry.key ? entry.key : "";
        if (structural.contains(key)) {
            continue;
        }
        if (!common.contains(key) && !contains_property(*descriptor, key)) {
            fail(path, "unsupported " + type_name + " property '" + key + "'");
        }
        validate_property(key, entry.view(), path + "." + key);
        node.properties.set(key, tc::trent::copy_of(*entry.value));
    }

    const tc::trent_view children = source["children"];
    if (children) {
        if (!children.is_list()) {
            fail(path + ".children", "expected a list");
        }
        if (!descriptor->attach_child && !children.as_list().empty()) {
            fail(path + ".children", type_name + " does not accept children");
        }
        node.children.reserve(children.size());
        for (size_t index = 0; index < children.size(); ++index) {
            node.children.push_back(parse_node(
                children[index],
                path + ".children[" + std::to_string(index) + "]",
                names,
                dependencies,
                dependency_set
            ));
        }
    }
    return node;
}

MaterializedWidget materialize_node(
    tc_ui_document_handle document,
    const UiScriptNode& node,
    std::vector<tc_widget_handle>& created,
    std::unordered_map<std::string, MaterializedWidget>& named
) {
    const tc_widget_handle handle =
        tc_ui_document_create_registered_widget(document, node.type_name.c_str());
    if (tc_widget_handle_is_invalid(handle)) {
        fail(node.source_path, "registered widget factory failed");
    }
    created.push_back(handle);
    tc_widget* widget = tc_ui_document_resolve_widget(document, handle);
    if (!widget) {
        fail(node.source_path, "created widget could not be resolved");
    }
    if (widget->native_language != TC_LANGUAGE_C &&
        widget->native_language != TC_LANGUAGE_CXX &&
        widget->native_language != TC_LANGUAGE_RUST) {
        fail(node.source_path + ".type", "widget type '" + node.type_name +
            "' is not native");
    }
    if (!node.name.empty() &&
        (!tc_widget_set_name(widget, node.name.c_str()) ||
         !tc_widget_set_stable_id(widget, node.name.c_str()))) {
        fail(node.source_path + ".name", "failed to assign stable widget identity");
    }
    if (tc::trent_view visible = node.properties["visible"]; visible) {
        tc_widget_set_visible(widget, visible.as_bool());
    }
    if (tc::trent_view enabled = node.properties["enabled"]; enabled) {
        tc_widget_set_enabled(widget, enabled.as_bool());
    }
    if (tc::trent_view layout = node.properties["layout"]; layout) {
        const tc_ui_widget_layout_spec spec =
            parse_layout_spec(layout, node.source_path + ".layout");
        if (!tc_widget_set_layout_spec(widget, &spec)) {
            fail(node.source_path + ".layout", "widget rejected normalized layout spec");
        }
    }
    const tc_uiscript_type_descriptor* descriptor =
        tc_uiscript_type_descriptor_get(node.type_name.c_str());
    if (!descriptor) {
        fail(node.source_path + ".type", "UiScript contract disappeared during materialization");
    }
    if (descriptor->apply_properties &&
        !descriptor->apply_properties(widget, node.properties.raw())) {
        fail(node.source_path, "widget rejected validated properties");
    }

    MaterializedWidget result{handle, node.type_name, node.properties};
    if (!node.name.empty()) {
        named.emplace(node.name, result);
    }
    for (const UiScriptNode& child_node : node.children) {
        MaterializedWidget child = materialize_node(
            document, child_node, created, named);
        tc_widget* child_widget =
            tc_ui_document_resolve_widget(document, child.handle);
        if (!child_widget || !descriptor->attach_child ||
            !descriptor->attach_child(
                widget, child_widget, child_node.properties.raw())) {
            fail(child_node.source_path, "native parent rejected child");
        }
    }
    return result;
}

void rollback(
    tc_ui_document_handle document,
    const std::vector<tc_widget_handle>& created
) {
    for (auto it = created.rbegin(); it != created.rend(); ++it) {
        if (tc_ui_document_resolve_widget(document, *it)) {
            tc_ui_document_destroy_widget_recursive(document, *it);
        }
    }
}

std::string read_file(const std::string& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw UiScriptError("failed to open UiScript file '" + path + "'");
    }
    std::ostringstream output;
    output << input.rdbuf();
    if (!input.good() && !input.eof()) {
        throw UiScriptError("failed to read UiScript file '" + path + "'");
    }
    return output.str();
}

} // namespace

UiScriptDescription UiScriptParser::parse(const std::string& source) const {
    tc::trent document;
    try {
        document = tc::yaml::parse(source);
    } catch (const std::exception& error) {
        throw UiScriptError(std::string("invalid native UiScript YAML: ") + error.what());
    }
    return parse_document(document);
}

UiScriptDescription UiScriptParser::parse_document(
    tc::trent_view document
) const {
    if (!register_builtin_widget_types()) {
        throw UiScriptError("failed to register built-in native UI widget types");
    }
    if (!document.is_dict()) {
        fail("document", "expected a mapping");
    }
    for (const auto entry : document.as_dict()) {
        const std::string key = entry.key ? entry.key : "";
        if (key != "uiscript" && key != "root") {
            fail("document", "unsupported key '" + key + "'");
        }
    }
    const tc::trent_view version = document["uiscript"];
    if (!version.is_integer() ||
        version.as_integer() != static_cast<int64_t>(UISCRIPT_VERSION)) {
        fail("document.uiscript", "expected dialect version " +
            std::to_string(UISCRIPT_VERSION));
    }
    if (!document.contains("root")) {
        fail("document", "missing root");
    }
    UiScriptDescription description;
    std::unordered_set<std::string> names;
    std::unordered_set<std::string> dependency_set;
    description.root = parse_node(
        document["root"], "root", names,
        description.type_dependencies, dependency_set);
    return description;
}

LoadedUiScript::LoadedUiScript(LoadedUiScript&& other) noexcept {
    *this = std::move(other);
}

LoadedUiScript& LoadedUiScript::operator=(LoadedUiScript&& other) noexcept {
    if (this == &other) {
        return *this;
    }
    close();
    document_ = other.document_;
    description_ = std::move(other.description_);
    root_ = std::move(other.root_);
    widgets_ = std::move(other.widgets_);
    owns_document_ = other.owns_document_;
    closed_ = other.closed_;
    other.document_ = tc_ui_document_handle_invalid();
    other.root_ = {};
    other.widgets_.clear();
    other.owns_document_ = false;
    other.closed_ = true;
    return *this;
}

LoadedUiScript::~LoadedUiScript() {
    close();
}

const MaterializedWidget& LoadedUiScript::named(const std::string& name) const {
    const auto found = widgets_.find(name);
    if (found == widgets_.end()) {
        throw std::out_of_range("native UiScript has no named widget '" + name + "'");
    }
    return found->second;
}

void LoadedUiScript::close() {
    if (closed_) {
        return;
    }
    closed_ = true;
    if (owns_document_) {
        if (tc_ui_document_is_valid(document_)) {
            tc_ui_document_destroy(document_);
        }
    } else if (tc_ui_document_is_valid(document_) &&
               tc_ui_document_resolve_widget(document_, root_.handle)) {
        tc_ui_document_destroy_widget_recursive(document_, root_.handle);
    }
    document_ = tc_ui_document_handle_invalid();
    root_ = {};
    widgets_.clear();
}

LoadedUiScript UiScriptLoader::load(
    const std::string& path,
    TcDocument document
) const {
    return load_string(read_file(path), document, path);
}

LoadedUiScript UiScriptLoader::load_string(
    const std::string& source,
    TcDocument document,
    const std::string& source_name
) const {
    try {
        return materialize(parser.parse(source), document);
    } catch (const std::exception& error) {
        tc_log_error(
            "[termin-gui-native] failed to load native UiScript '%s': %s",
            source_name.c_str(), error.what());
        throw;
    }
}

LoadedUiScript UiScriptLoader::materialize(
    const UiScriptDescription& description,
    TcDocument supplied_document
) const {
    const bool owns_document = !supplied_document.valid();
    const tc_ui_document_handle document = owns_document
        ? tc_ui_document_create()
        : supplied_document.handle();
    if (!tc_ui_document_is_valid(document)) {
        throw UiScriptError("failed to create target UI document");
    }
    std::vector<tc_widget_handle> created;
    LoadedUiScript loaded;
    loaded.document_ = document;
    loaded.description_ = description;
    loaded.owns_document_ = owns_document;
    loaded.closed_ = false;
    try {
        loaded.root_ = materialize_node(
            document, description.root, created, loaded.widgets_);
        if (!tc_ui_document_add_root(document, loaded.root_.handle)) {
            fail(description.root.source_path, "native document rejected root");
        }
        return loaded;
    } catch (...) {
        if (owns_document) {
            tc_ui_document_destroy(document);
        } else {
            rollback(document, created);
        }
        loaded.document_ = tc_ui_document_handle_invalid();
        loaded.closed_ = true;
        throw;
    }
}

LoadedUiScript UiScriptLoader::reload(
    LoadedUiScript& loaded,
    const std::string& source,
    const std::string& source_name
) const {
    if (loaded.closed_ || !tc_ui_document_is_valid(loaded.document_)) {
        throw UiScriptError("cannot reload a closed native UiScript");
    }
    UiScriptDescription description;
    try {
        description = parser.parse(source);
    } catch (const std::exception& error) {
        tc_log_error(
            "[termin-gui-native] failed to reload native UiScript '%s': %s",
            source_name.c_str(), error.what());
        throw;
    }
    return reload(loaded, description);
}

LoadedUiScript UiScriptLoader::reload(
    LoadedUiScript& loaded,
    const UiScriptDescription& description
) const {
    if (loaded.closed_ || !tc_ui_document_is_valid(loaded.document_)) {
        throw UiScriptError("cannot reload a closed native UiScript");
    }
    LoadedUiScript replacement =
        materialize(description, TcDocument(loaded.document_));
    replacement.owns_document_ = loaded.owns_document_;
    loaded.owns_document_ = false;
    loaded.close();
    return replacement;
}

} // namespace termin::gui_native
