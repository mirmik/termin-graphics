#include <termin/gui_native/uiscript.hpp>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>
#include <unordered_set>

#include <tcbase/tc_log.h>
#include <tcbase/tc_trent_yaml.hpp>
#include <termin/gui_native/box_layout.hpp>
#include <termin/gui_native/builtin_widget_registration.hpp>
#include <termin/gui_native/grid_layout.hpp>
#include <termin/gui_native/tc_uiscript.h>
#include <termin/gui_native/tc_widget_registry.h>

#include "tc_ui_document_internal.h"

namespace termin::gui_native {
    namespace {

        [[noreturn]] void fail(const std::string& path, const std::string& message) {
            throw UiScriptError(path + ": " + message);
        }

        bool is_number(tc::trent_view value) {
            return value.is_numer() && !value.is_bool();
        }

        void validate_number(tc::trent_view value, const std::string& path, bool nonnegative = false) {
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

        void validate_grid_track(tc::trent_view track, const std::string& path) {
            if (!track.is_dict()) {
                fail(path, "expected a track mapping");
            }
            static const std::unordered_set<std::string> supported{
                "policy", "value", "grow", "shrink", "min_extent", "max_extent"};
            for (const auto entry : track.as_dict()) {
                const std::string key = entry.key ? entry.key : "";
                if (!supported.contains(key)) {
                    fail(path, "unsupported track property '" + key + "'");
                }
            }
            const tc::trent_view policy_value = track["policy"];
            if (!policy_value.is_string()) {
                fail(path + ".policy", "expected 'fixed', 'preferred', 'flex', or 'stretch'");
            }
            const std::string policy = policy_value.as_string();
            if (policy != "fixed" && policy != "preferred" && policy != "flex" && policy != "stretch") {
                fail(path + ".policy", "unsupported track policy '" + policy + "'");
            }
            const tc::trent_view value = track["value"];
            if (policy == "fixed") {
                if (!value) {
                    fail(path + ".value", "fixed track requires a value");
                }
                validate_number(value, path + ".value", true);
            } else if (policy == "flex") {
                if (value) {
                    validate_number(value, path + ".value", true);
                    if (value.as_numer() <= 0.0) {
                        fail(path + ".value", "flex weight must be > 0");
                    }
                }
            } else if (value) {
                fail(path + ".value", policy + " track does not accept a value");
            }
            for (const char* key : {"grow", "shrink", "min_extent", "max_extent"}) {
                if (tc::trent_view item = track[key]; item) {
                    validate_number(item, path + "." + key, true);
                }
            }
            const tc::trent_view grow = track["grow"];
            const tc::trent_view shrink = track["shrink"];
            if (policy == "fixed" && ((grow && grow.as_numer() != 0.0) || (shrink && shrink.as_numer() != 0.0))) {
                fail(path, "fixed track cannot grow or shrink");
            }
            const tc::trent_view minimum = track["min_extent"];
            const tc::trent_view maximum = track["max_extent"];
            if (minimum && maximum && maximum.as_numer() > 0.0 && minimum.as_numer() > maximum.as_numer()) {
                fail(path + ".max_extent", "must be zero (unbounded) or >= min_extent");
            }
            if (policy == "fixed") {
                const double fixed = value.as_numer();
                if ((minimum && minimum.as_numer() != fixed) ||
                    (maximum && maximum.as_numer() != 0.0 && maximum.as_numer() != fixed)) {
                    fail(path, "fixed track extent conflicts with min/max extent");
                }
            }
        }

        void validate_grid_tracks(tc::trent_view value, const std::string& path) {
            if (!value.is_list() || value.size() == 0) {
                fail(path, "expected a non-empty track list");
            }
            for (size_t index = 0; index < value.size(); ++index) {
                validate_grid_track(value[index], path + "[" + std::to_string(index) + "]");
            }
        }

        tc_ui_length parse_layout_length(tc::trent_view value, const std::string& path) {
            if (is_number(value)) {
                validate_number(value, path, true);
                return tc_ui_length{TC_UI_LENGTH_FIXED, static_cast<float>(value.as_numer())};
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
                    if (consumed != text.size() - 1 || !std::isfinite(percent) || percent < 0.0 || percent > 100.0) {
                        fail(path, "percentage must be in the [0%, 100%] range");
                    }
                    return tc_ui_length{TC_UI_LENGTH_PERCENT, static_cast<float>(percent / 100.0)};
                } catch (const std::invalid_argument&) {
                    fail(path, "invalid percentage length '" + text + "'");
                } catch (const std::out_of_range&) {
                    fail(path, "percentage length is out of range");
                }
            }
            fail(path, "unsupported layout length '" + text + "'");
        }

        tc_ui_widget_layout_spec parse_layout_spec(tc::trent_view value, const std::string& path) {
            if (!value.is_dict()) {
                fail(path, "expected a layout mapping");
            }
            tc_ui_widget_layout_spec spec = tc_ui_widget_layout_spec_default();
            static const std::unordered_set<std::string> supported{"width",
                                                                   "height",
                                                                   "min_width",
                                                                   "min_height",
                                                                   "max_width",
                                                                   "max_height",
                                                                   "margin",
                                                                   "aspect_ratio",
                                                                   "minimum_touch_target"};
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
#define READ_NONNEGATIVE_LAYOUT_FIELD(field)                                                                           \
    if (tc::trent_view item = value[#field]; item) {                                                                   \
        validate_number(item, path + "." #field, true);                                                                \
        spec.field = static_cast<float>(item.as_numer());                                                              \
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
                    spec.margin = tc_ui_insets{static_cast<float>(margin[0].as_numer()),
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
                        fail(path + ".minimum_touch_target", "true is ambiguous; provide a size or use false");
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
                    spec.minimum_touch_target =
                        tc_ui_size{static_cast<float>(target[0].as_numer()), static_cast<float>(target[1].as_numer())};
                } else {
                    fail(path + ".minimum_touch_target", "expected false, a size, or [width, height]");
                }
            }
            tc_ui_widget_layout_spec normalized;
            if (!tc_ui_widget_layout_spec_normalize(&spec, &normalized)) {
                fail(path, "invalid or over-constrained widget layout spec");
            }
            return normalized;
        }

        void validate_property(const std::string& name, tc::trent_view value, const std::string& path) {
            if (name == "layout") {
                (void)parse_layout_spec(value, path);
                return;
            }
            if (name == "visible" || name == "enabled" || name == "active") {
                if (!value.is_bool())
                    fail(path, "expected a boolean");
                return;
            }
            if (name == "horizontal_scroll" || name == "vertical_scroll") {
                if (!value.is_bool())
                    fail(path, "expected a boolean");
                return;
            }
            if (name == "spacing" || name == "column_spacing" || name == "row_spacing" || name == "line_spacing" ||
                name == "size" || name == "font_size" || name == "border_radius" || name == "grow" ||
                name == "shrink" || name == "min_extent" || name == "max_extent") {
                validate_number(value, path, true);
                return;
            }
            if (name == "columns" || name == "rows") {
                validate_grid_tracks(value, path);
                return;
            }
            if (name == "row" || name == "column" || name == "row_span" || name == "column_span" ||
                name == "max_lines") {
                if (!value.is_integer() || value.as_integer() < 0 ||
                    ((name == "row_span" || name == "column_span") && value.as_integer() == 0)) {
                    fail(path,
                         name == "row" || name == "column" ? "expected a non-negative integer"
                                                           : "expected a positive integer");
                }
                return;
            }
            if (name == "horizontal_scrollbar" || name == "vertical_scrollbar") {
                if (!value.is_string()) {
                    fail(path, "expected 'auto', 'always', or 'hidden'");
                }
                const std::string policy = value.as_string();
                if (policy != "auto" && policy != "always" && policy != "hidden") {
                    fail(path, "unsupported scrollbar policy '" + policy + "'");
                }
                return;
            }
            if (name == "padding") {
                if (is_number(value)) {
                    validate_number(value, path, true);
                    return;
                }
                if (!value.is_list() || value.size() != 4) {
                    fail(path, "expected a number or [left, top, right, bottom]");
                }
                for (size_t index = 0; index < value.size(); ++index) {
                    validate_number(value[index], path + "[" + std::to_string(index) + "]", true);
                }
                return;
            }
            if (name == "orientation") {
                if (!value.is_string())
                    fail(path, "expected 'horizontal' or 'vertical'");
                const std::string orientation = value.as_string();
                if (orientation != "horizontal" && orientation != "vertical") {
                    fail(path, "unsupported orientation '" + orientation + "'");
                }
                return;
            }
            if (name == "safe_area") {
                if (!value.is_string() || (value.as_string() != "respect" && value.as_string() != "ignore")) {
                    fail(path, "expected 'respect' or 'ignore'");
                }
                return;
            }
            if (name == "align_items" || name == "align_self" || name == "line_alignment") {
                if (!value.is_string())
                    fail(path, "expected an alignment string");
                const std::string alignment = value.as_string();
                const bool allow_auto = name == "align_self";
                if (alignment != "stretch" && alignment != "start" && alignment != "center" && alignment != "end" &&
                    !(allow_auto && alignment == "auto")) {
                    fail(path, "unsupported alignment '" + alignment + "'");
                }
                return;
            }
            if (name == "basis") {
                if (is_number(value)) {
                    validate_number(value, path, true);
                } else if (!value.is_string() || value.as_string() != "preferred") {
                    fail(path, "expected 'preferred' or a non-negative fixed extent");
                }
                return;
            }
            if (name == "background_color" || name == "hover_color" || name == "pressed_color" ||
                name == "active_color" || name == "icon_color" || name == "color") {
                validate_color(value, path);
                return;
            }
            if (name == "icon" || name == "tooltip" || name == "text") {
                if (!value.is_string())
                    fail(path, "expected a string");
                return;
            }
            if (name == "wrap") {
                if (!value.is_string()) {
                    fail(path, "expected 'none', 'word', or 'character'");
                }
                const std::string mode = value.as_string();
                if (mode != "none" && mode != "word" && mode != "character") {
                    fail(path, "unsupported text wrap mode '" + mode + "'");
                }
                return;
            }
            if (name == "overflow") {
                if (!value.is_string()) {
                    fail(path, "expected 'clip' or 'ellipsis'");
                }
                const std::string mode = value.as_string();
                if (mode != "clip" && mode != "ellipsis") {
                    fail(path, "unsupported text overflow mode '" + mode + "'");
                }
                return;
            }
            if (name == "anchor") {
                if (!value.is_string())
                    fail(path, "expected an anchor string");
                const std::string anchor = value.as_string();
                if (anchor != "fill" && anchor != "top-left" && anchor != "top-right" && anchor != "bottom-left" &&
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

        void validate_widget_property(const tc_uiscript_type_descriptor& descriptor,
                                      const std::string& name,
                                      tc::trent_view value,
                                      const std::string& path) {
            if (descriptor.validate_property) {
                if (!descriptor.validate_property(name.c_str(), value.raw())) {
                    fail(path, "widget type rejected property value");
                }
                return;
            }
            validate_property(name, value, path);
        }

        bool contains_property(const tc_uiscript_type_descriptor& descriptor, const std::string& name) {
            for (size_t index = 0; index < descriptor.property_count; ++index) {
                if (descriptor.properties[index] && name == descriptor.properties[index]) {
                    return true;
                }
            }
            return false;
        }

        bool contains_child_property(const tc_uiscript_type_descriptor& descriptor, const std::string& name) {
            for (size_t index = 0; index < descriptor.child_property_count; ++index) {
                if (descriptor.child_properties[index] && name == descriptor.child_properties[index]) {
                    return true;
                }
            }
            return false;
        }

        struct SelectorDomain {
            double min_width = 0.0;
            double max_width = INFINITY;
            double min_height = 0.0;
            double max_height = INFINITY;
            std::string orientation;
        };

        SelectorDomain selector_domain(tc::trent_view selector) {
            SelectorDomain domain;
            if (tc::trent_view value = selector["min_width"]; value) {
                domain.min_width = value.as_numer();
            }
            if (tc::trent_view value = selector["max_width"]; value) {
                domain.max_width = value.as_numer();
            }
            if (tc::trent_view value = selector["min_height"]; value) {
                domain.min_height = value.as_numer();
            }
            if (tc::trent_view value = selector["max_height"]; value) {
                domain.max_height = value.as_numer();
            }
            if (tc::trent_view value = selector["orientation"]; value) {
                domain.orientation = value.as_string();
            }
            if (tc::trent_view value = selector["width_class"]; value) {
                const std::string width_class = value.as_string();
                if (width_class == "compact") {
                    domain.max_width = std::min(domain.max_width, 600.0);
                } else if (width_class == "medium") {
                    domain.min_width = std::max(domain.min_width, 600.0);
                    domain.max_width = std::min(domain.max_width, 840.0);
                } else {
                    domain.min_width = std::max(domain.min_width, 840.0);
                }
            }
            return domain;
        }

        bool selectors_overlap(tc::trent_view lhs, tc::trent_view rhs) {
            const SelectorDomain a = selector_domain(lhs);
            const SelectorDomain b = selector_domain(rhs);
            return std::max(a.min_width, b.min_width) < std::min(a.max_width, b.max_width) &&
                   std::max(a.min_height, b.min_height) < std::min(a.max_height, b.max_height) &&
                   (a.orientation.empty() || b.orientation.empty() || a.orientation == b.orientation);
        }

        bool overrides_overlap(tc::trent_view lhs, tc::trent_view rhs) {
            for (const auto entry : lhs.as_dict()) {
                if (rhs[entry.key ? entry.key : ""]) {
                    return true;
                }
            }
            return false;
        }

        std::vector<UiScriptVariant> parse_variants(tc::trent_view source,
                                                    const std::string& path,
                                                    const std::string& type_name,
                                                    const tc_uiscript_type_descriptor* parent_descriptor) {
            const tc::trent_view variants = source["variants"];
            std::vector<UiScriptVariant> result;
            if (!variants) {
                return result;
            }
            if (!variants.is_list()) {
                fail(path + ".variants", "expected a list");
            }
            result.reserve(variants.size());
            for (size_t index = 0; index < variants.size(); ++index) {
                const std::string variant_path = path + ".variants[" + std::to_string(index) + "]";
                const tc::trent_view encoded = variants[index];
                if (!encoded.is_dict()) {
                    fail(variant_path, "expected a mapping");
                }
                for (const auto entry : encoded.as_dict()) {
                    const std::string key = entry.key ? entry.key : "";
                    if (key != "when" && key != "set" && key != "priority") {
                        fail(variant_path, "unsupported variant property '" + key + "'");
                    }
                }
                const tc::trent_view selector = encoded["when"];
                const tc::trent_view overrides = encoded["set"];
                if (!selector.is_dict() || selector.size() == 0) {
                    fail(variant_path + ".when", "expected a non-empty selector mapping");
                }
                if (!overrides.is_dict() || overrides.size() == 0) {
                    fail(variant_path + ".set", "expected a non-empty override mapping");
                }
                static const std::unordered_set<std::string> selector_keys{
                    "min_width", "max_width", "min_height", "max_height", "orientation", "width_class"};
                for (const auto entry : selector.as_dict()) {
                    const std::string key = entry.key ? entry.key : "";
                    const tc::trent_view value = entry.view();
                    if (!selector_keys.contains(key)) {
                        fail(variant_path + ".when", "unsupported selector '" + key + "'");
                    }
                    if (key == "orientation") {
                        if (!value.is_string() ||
                            (value.as_string() != "portrait" && value.as_string() != "landscape")) {
                            fail(variant_path + ".when.orientation", "expected 'portrait' or 'landscape'");
                        }
                    } else if (key == "width_class") {
                        if (!value.is_string() || (value.as_string() != "compact" && value.as_string() != "medium" &&
                                                   value.as_string() != "expanded")) {
                            fail(variant_path + ".when.width_class", "expected 'compact', 'medium', or 'expanded'");
                        }
                    } else {
                        validate_number(value, variant_path + ".when." + key, true);
                    }
                }
                const SelectorDomain domain = selector_domain(selector);
                if (domain.min_width >= domain.max_width) {
                    fail(variant_path + ".when", "width selector has an empty [min, max) range");
                }
                if (domain.min_height >= domain.max_height) {
                    fail(variant_path + ".when", "height selector has an empty [min, max) range");
                }

                static const std::unordered_set<std::string> box_types{
                    "termin.gui.BoxLayout", "termin.gui.HStack", "termin.gui.VStack"};
                for (const auto entry : overrides.as_dict()) {
                    const std::string key = entry.key ? entry.key : "";
                    const bool generic = key == "visible" || key == "layout";
                    const bool box =
                        box_types.contains(type_name) && (key == "orientation" || key == "spacing" || key == "padding");
                    const bool grid_placement =
                        parent_descriptor && contains_child_property(*parent_descriptor, key) &&
                        (key == "row" || key == "column" || key == "row_span" || key == "column_span");
                    const bool safe_area = key == "safe_area" && path == "root";
                    if (!generic && !box && !grid_placement && !safe_area) {
                        fail(variant_path + ".set", "unsupported responsive override '" + key + "'");
                    }
                    if (safe_area) {
                        const tc::trent_view value = entry.view();
                        if (!value.is_string() || (value.as_string() != "respect" && value.as_string() != "ignore")) {
                            fail(variant_path + ".set.safe_area", "expected 'respect' or 'ignore'");
                        }
                    } else {
                        validate_property(key, entry.view(), variant_path + ".set." + key);
                    }
                }
                const tc::trent_view priority = encoded["priority"];
                if (priority && !priority.is_integer()) {
                    fail(variant_path + ".priority", "expected an integer");
                }
                UiScriptVariant variant;
                variant.selector = tc::trent::copy_of(selector.raw());
                variant.overrides = tc::trent::copy_of(overrides.raw());
                variant.priority = priority ? priority.as_integer() : 0;
                variant.source_path = variant_path;
                for (const UiScriptVariant& previous : result) {
                    if (previous.priority == variant.priority &&
                        selectors_overlap(previous.selector.view(), selector) &&
                        overrides_overlap(previous.overrides.view(), overrides)) {
                        fail(variant_path,
                             "ambiguous overlap at priority " + std::to_string(variant.priority) +
                                 "; assign distinct priorities");
                    }
                }
                result.push_back(std::move(variant));
            }
            std::stable_sort(result.begin(), result.end(), [](const UiScriptVariant& lhs, const UiScriptVariant& rhs) {
                return lhs.priority < rhs.priority;
            });
            return result;
        }

        void validate_box_placement(const UiScriptNode& node, const tc_uiscript_type_descriptor* parent_descriptor) {
            if (!parent_descriptor || !contains_child_property(*parent_descriptor, "basis")) {
                return;
            }
            const tc::trent_view minimum = node.properties["min_extent"];
            const tc::trent_view maximum = node.properties["max_extent"];
            if (minimum && maximum && maximum.as_numer() > 0.0 && minimum.as_numer() > maximum.as_numer()) {
                fail(node.source_path + ".max_extent", "must be zero (unbounded) or >= min_extent");
            }
            const tc::trent_view basis = node.properties["basis"];
            if (!basis || !is_number(basis)) {
                return;
            }
            const double fixed = basis.as_numer();
            const tc::trent_view grow = node.properties["grow"];
            const tc::trent_view shrink = node.properties["shrink"];
            if ((grow && grow.as_numer() != 0.0) || (shrink && shrink.as_numer() != 0.0)) {
                fail(node.source_path + ".basis", "a fixed basis cannot grow or shrink");
            }
            if ((minimum && minimum.as_numer() != fixed) ||
                (maximum && maximum.as_numer() != 0.0 && maximum.as_numer() != fixed)) {
                fail(node.source_path + ".basis", "a fixed basis conflicts with min/max primary extent");
            }
        }

        void validate_grid_structure(const std::string& type_name, tc::trent_view source, const std::string& path) {
            if (type_name != "termin.gui.GridLayout") {
                return;
            }
            const tc::trent_view columns = source["columns"];
            const tc::trent_view rows = source["rows"];
            if (!columns) {
                fail(path + ".columns", "GridLayout requires column tracks");
            }
            if (!rows) {
                fail(path + ".rows", "GridLayout requires row tracks");
            }
            const tc::trent_view children = source["children"];
            if (!children || !children.is_list()) {
                return;
            }
            for (size_t index = 0; index < children.size(); ++index) {
                const tc::trent_view child = children[index];
                const std::string child_path = path + ".children[" + std::to_string(index) + "]";
                if (!child.is_dict()) {
                    continue;
                }
                const tc::trent_view row = child["row"];
                const tc::trent_view column = child["column"];
                if (!row) {
                    fail(child_path + ".row", "GridLayout child requires a row");
                }
                if (!column) {
                    fail(child_path + ".column", "GridLayout child requires a column");
                }
                if (!row.is_integer() || !column.is_integer()) {
                    continue;
                }
                const tc::trent_view row_span_value = child["row_span"];
                const tc::trent_view column_span_value = child["column_span"];
                if ((row_span_value && !row_span_value.is_integer()) ||
                    (column_span_value && !column_span_value.is_integer())) {
                    continue;
                }
                const int64_t row_span = row_span_value ? row_span_value.as_integer() : 1;
                const int64_t column_span = column_span_value ? column_span_value.as_integer() : 1;
                if (row.as_integer() < 0 || column.as_integer() < 0 || row_span <= 0 || column_span <= 0) {
                    continue;
                }
                if (row.as_integer() >= static_cast<int64_t>(rows.size()) ||
                    row.as_integer() + row_span > static_cast<int64_t>(rows.size())) {
                    fail(child_path + ".row", "row placement exceeds declared tracks");
                }
                if (column.as_integer() >= static_cast<int64_t>(columns.size()) ||
                    column.as_integer() + column_span > static_cast<int64_t>(columns.size())) {
                    fail(child_path + ".column", "column placement exceeds declared tracks");
                }
            }
        }

        void validate_grid_variant_placements(const UiScriptNode& node) {
            if (node.type_name != "termin.gui.GridLayout") {
                return;
            }
            const size_t row_count = node.properties["rows"].size();
            const size_t column_count = node.properties["columns"].size();
            for (const UiScriptNode& child : node.children) {
                const size_t base_row = static_cast<size_t>(child.properties["row"].as_integer());
                const size_t base_column = static_cast<size_t>(child.properties["column"].as_integer());
                const tc::trent_view base_row_span_value = child.properties["row_span"];
                const tc::trent_view base_column_span_value = child.properties["column_span"];
                const size_t base_row_span =
                    base_row_span_value ? static_cast<size_t>(base_row_span_value.as_integer()) : 1;
                const size_t base_column_span =
                    base_column_span_value ? static_cast<size_t>(base_column_span_value.as_integer()) : 1;
                for (const UiScriptVariant& variant : child.variants) {
                    const tc::trent_view overrides = variant.overrides;
                    const size_t row = overrides["row"] ? static_cast<size_t>(overrides["row"].as_integer()) : base_row;
                    const size_t column =
                        overrides["column"] ? static_cast<size_t>(overrides["column"].as_integer()) : base_column;
                    const size_t row_span =
                        overrides["row_span"] ? static_cast<size_t>(overrides["row_span"].as_integer()) : base_row_span;
                    const size_t column_span = overrides["column_span"]
                                                   ? static_cast<size_t>(overrides["column_span"].as_integer())
                                                   : base_column_span;
                    if (row + row_span > row_count) {
                        fail(variant.source_path + ".set.row", "responsive row placement exceeds declared tracks");
                    }
                    if (column + column_span > column_count) {
                        fail(variant.source_path + ".set.column",
                             "responsive column placement exceeds declared tracks");
                    }
                }
            }
        }

        UiScriptNode parse_node(tc::trent_view source,
                                const std::string& path,
                                std::unordered_set<std::string>& names,
                                std::vector<std::string>& dependencies,
                                std::unordered_set<std::string>& dependency_set,
                                const tc_uiscript_type_descriptor* parent_descriptor = nullptr) {
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
            const tc_uiscript_type_descriptor* descriptor = tc_uiscript_type_descriptor_get(type_name.c_str());
            if (!descriptor) {
                fail(path + ".type", "widget type '" + type_name + "' has no native UiScript contract");
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

            static const std::unordered_set<std::string> structural{"type", "name", "children", "variants"};
            static const std::unordered_set<std::string> common{"visible", "enabled", "layout"};
            for (const auto entry : source.as_dict()) {
                const std::string key = entry.key ? entry.key : "";
                if (structural.contains(key)) {
                    continue;
                }
                const bool parent_property = parent_descriptor && contains_child_property(*parent_descriptor, key);
                const bool root_property = path == "root" && key == "safe_area";
                if (!common.contains(key) && !contains_property(*descriptor, key) && !parent_property &&
                    !root_property) {
                    fail(path, "unsupported " + type_name + " property '" + key + "'");
                }
                if (contains_property(*descriptor, key)) {
                    validate_widget_property(*descriptor, key, entry.view(), path + "." + key);
                } else {
                    validate_property(key, entry.view(), path + "." + key);
                }
                node.properties.set(key, tc::trent::copy_of(*entry.value));
            }
            validate_box_placement(node, parent_descriptor);
            validate_grid_structure(type_name, source, path);
            node.variants = parse_variants(source, path, type_name, parent_descriptor);

            const tc::trent_view children = source["children"];
            if (children) {
                if (!children.is_list()) {
                    fail(path + ".children", "expected a list");
                }
                if (!descriptor->attach_child && !children.as_list().empty()) {
                    fail(path + ".children", type_name + " does not accept children");
                }
                if (children.size() > descriptor->max_child_count) {
                    fail(path + ".children",
                         type_name + " accepts at most " + std::to_string(descriptor->max_child_count) + " child");
                }
                node.children.reserve(children.size());
                for (size_t index = 0; index < children.size(); ++index) {
                    node.children.push_back(parse_node(children[index],
                                                       path + ".children[" + std::to_string(index) + "]",
                                                       names,
                                                       dependencies,
                                                       dependency_set,
                                                       descriptor));
                }
            }
            validate_grid_variant_placements(node);
            return node;
        }

        MaterializedWidget materialize_node(tc_ui_document_handle document,
                                            const UiScriptNode& node,
                                            std::vector<tc_widget_handle>& created,
                                            std::unordered_map<std::string, MaterializedWidget>& named,
                                            std::unordered_map<std::string, tc_widget_handle>& all_handles) {
            const tc_widget_handle handle = tc_ui_document_create_registered_widget(document, node.type_name.c_str());
            if (tc_widget_handle_is_invalid(handle)) {
                fail(node.source_path, "registered widget factory failed");
            }
            created.push_back(handle);
            all_handles.emplace(node.source_path, handle);
            tc_widget* widget = tc_ui_document_resolve_widget(document, handle);
            if (!widget) {
                fail(node.source_path, "created widget could not be resolved");
            }
            if (widget->native_language != TC_LANGUAGE_C && widget->native_language != TC_LANGUAGE_CXX &&
                widget->native_language != TC_LANGUAGE_RUST) {
                fail(node.source_path + ".type", "widget type '" + node.type_name + "' is not native");
            }
            if (!node.name.empty() && (!tc_widget_set_name(widget, node.name.c_str()) ||
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
                const tc_ui_widget_layout_spec spec = parse_layout_spec(layout, node.source_path + ".layout");
                if (!tc_widget_set_layout_spec(widget, &spec)) {
                    fail(node.source_path + ".layout", "widget rejected normalized layout spec");
                }
            }
            if (tc::trent_view safe_area = node.properties["safe_area"]; safe_area) {
                const tc_ui_root_layout_policy policy =
                    safe_area.as_string() == "respect" ? TC_UI_ROOT_LAYOUT_SAFE_AREA : TC_UI_ROOT_LAYOUT_FULL_VIEWPORT;
                if (node.source_path != "root" || !tc_ui_document_set_root_layout_policy(document, policy)) {
                    fail(node.source_path + ".safe_area", "document rejected root safe-area policy");
                }
            }
            const tc_uiscript_type_descriptor* descriptor = tc_uiscript_type_descriptor_get(node.type_name.c_str());
            if (!descriptor) {
                fail(node.source_path + ".type", "UiScript contract disappeared during materialization");
            }
            if (descriptor->apply_properties && !descriptor->apply_properties(widget, node.properties.raw())) {
                fail(node.source_path, "widget rejected validated properties");
            }

            MaterializedWidget result{handle, node.type_name, node.properties};
            if (!node.name.empty()) {
                named.emplace(node.name, result);
            }
            for (const UiScriptNode& child_node : node.children) {
                MaterializedWidget child = materialize_node(document, child_node, created, named, all_handles);
                tc_widget* child_widget = tc_ui_document_resolve_widget(document, child.handle);
                if (!child_widget || !descriptor->attach_child ||
                    !descriptor->attach_child(widget, child_widget, child_node.properties.raw())) {
                    fail(child_node.source_path, "native parent rejected child");
                }
            }
            return result;
        }

        void rollback(tc_ui_document_handle document, const std::vector<tc_widget_handle>& created) {
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

    class ResponsiveRuntime {
    private:
        struct Binding {
            tc_widget_handle handle = tc_widget_handle_invalid();
            tc_widget_handle parent = tc_widget_handle_invalid();
            std::string type_name;
            std::vector<UiScriptVariant> variants;
            bool base_visible = true;
            tc_ui_widget_layout_spec base_layout = tc_ui_widget_layout_spec_default();
            bool is_box = false;
            Orientation base_orientation = Orientation::Horizontal;
            EdgeInsets base_padding{};
            float base_spacing = 0.0f;
            bool has_grid_placement = false;
            GridItem base_grid_placement{};
            bool is_root = false;
            tc_ui_root_layout_policy base_safe_area = TC_UI_ROOT_LAYOUT_FULL_VIEWPORT;
            std::vector<bool> active;
        };

        tc_ui_document_handle document_ = tc_ui_document_handle_invalid();
        uint64_t callback_token_ = 0;
        std::vector<Binding> bindings_;

        static bool matches(tc::trent_view selector, float width, float height) {
            if (tc::trent_view value = selector["min_width"]; value && width < value.as_numer()) {
                return false;
            }
            if (tc::trent_view value = selector["max_width"]; value && width >= value.as_numer()) {
                return false;
            }
            if (tc::trent_view value = selector["min_height"]; value && height < value.as_numer()) {
                return false;
            }
            if (tc::trent_view value = selector["max_height"]; value && height >= value.as_numer()) {
                return false;
            }
            if (tc::trent_view value = selector["orientation"]; value) {
                const bool portrait = height > width;
                if ((value.as_string() == "portrait") != portrait) {
                    return false;
                }
            }
            if (tc::trent_view value = selector["width_class"]; value) {
                const std::string width_class = width < 600.0f ? "compact" : (width < 840.0f ? "medium" : "expanded");
                if (value.as_string() != width_class) {
                    return false;
                }
            }
            return true;
        }

        static float number(tc::trent_view value) {
            return static_cast<float>(value.as_numer());
        }

        static EdgeInsets padding(tc::trent_view value) {
            if (is_number(value)) {
                const float all = number(value);
                return {all, all, all, all};
            }
            return {number(value[0]), number(value[1]), number(value[2]), number(value[3])};
        }

        static void prepare_callback(tc_ui_document_handle, tc_ui_rect* rect, void* user_data) noexcept {
            auto* runtime = static_cast<ResponsiveRuntime*>(user_data);
            try {
                runtime->refresh(*rect);
            } catch (const std::exception& error) {
                tc_log_error("[termin-gui-native] responsive UiScript refresh failed: %s", error.what());
            } catch (...) {
                tc_log_error("[termin-gui-native] responsive UiScript refresh failed "
                             "with an unknown exception");
            }
        }

        void collect(const UiScriptNode& node,
                     tc_widget_handle parent,
                     const std::string& parent_type,
                     const std::unordered_map<std::string, tc_widget_handle>& handles) {
            const auto found = handles.find(node.source_path);
            if (found == handles.end()) {
                throw UiScriptError(node.source_path + ": responsive runtime lost materialized widget");
            }
            const tc_widget_handle handle = found->second;
            if (!node.variants.empty()) {
                tc_widget* widget = tc_ui_document_resolve_widget(document_, handle);
                if (!widget) {
                    throw UiScriptError(node.source_path + ": responsive runtime could not resolve widget");
                }
                Binding binding;
                binding.handle = handle;
                binding.parent = parent;
                binding.type_name = node.type_name;
                binding.variants = node.variants;
                binding.base_visible = tc_widget_is_visible(widget);
                binding.base_layout = tc_widget_layout_spec(widget);
                binding.is_root = tc_widget_handle_is_invalid(parent);
                if (binding.is_root) {
                    binding.base_safe_area = tc_ui_document_root_layout_policy(document_);
                }
                static const std::unordered_set<std::string> box_types{
                    "termin.gui.BoxLayout", "termin.gui.HStack", "termin.gui.VStack"};
                binding.is_box = box_types.contains(node.type_name);
                if (binding.is_box) {
                    const auto* box = static_cast<const BoxLayout*>(widget->body);
                    binding.base_orientation = box->orientation();
                    binding.base_padding = box->padding();
                    binding.base_spacing = box->spacing();
                }
                if (!tc_widget_handle_is_invalid(parent)) {
                    tc_widget* parent_widget = tc_ui_document_resolve_widget(document_, parent);
                    if (parent_widget && parent_widget->body && parent_type == "termin.gui.GridLayout") {
                        const auto* grid = static_cast<const GridLayout*>(parent_widget->body);
                        for (const GridItem& item : grid->items()) {
                            if (tc_widget_handle_eq(item.handle, handle)) {
                                binding.has_grid_placement = true;
                                binding.base_grid_placement = item;
                                break;
                            }
                        }
                    }
                }
                binding.active.assign(binding.variants.size(), false);
                bindings_.push_back(std::move(binding));
            }
            for (const UiScriptNode& child : node.children) {
                collect(child, handle, node.type_name, handles);
            }
        }

        void apply(Binding& binding, const std::vector<bool>& active) {
            tc_widget* widget = tc_ui_document_resolve_widget(document_, binding.handle);
            if (!widget) {
                tc_log_error("[termin-gui-native] responsive UiScript target became stale");
                return;
            }
            bool visible = binding.base_visible;
            tc_ui_widget_layout_spec layout = binding.base_layout;
            bool has_layout_override = false;
            Orientation orientation = binding.base_orientation;
            EdgeInsets box_padding = binding.base_padding;
            float spacing = binding.base_spacing;
            GridItem placement = binding.base_grid_placement;
            tc_ui_root_layout_policy safe_area = binding.base_safe_area;

            for (size_t index = 0; index < binding.variants.size(); ++index) {
                if (!active[index]) {
                    continue;
                }
                const tc::trent_view overrides = binding.variants[index].overrides;
                if (tc::trent_view value = overrides["visible"]; value) {
                    visible = value.as_bool();
                }
                if (tc::trent_view value = overrides["layout"]; value) {
                    layout = parse_layout_spec(value, binding.variants[index].source_path + ".set.layout");
                    has_layout_override = true;
                }
                if (tc::trent_view value = overrides["orientation"]; value) {
                    orientation = value.as_string() == "vertical" ? Orientation::Vertical : Orientation::Horizontal;
                }
                if (tc::trent_view value = overrides["padding"]; value) {
                    box_padding = padding(value);
                }
                if (tc::trent_view value = overrides["spacing"]; value) {
                    spacing = number(value);
                }
                if (tc::trent_view value = overrides["row"]; value) {
                    placement.row = static_cast<size_t>(value.as_integer());
                }
                if (tc::trent_view value = overrides["column"]; value) {
                    placement.column = static_cast<size_t>(value.as_integer());
                }
                if (tc::trent_view value = overrides["row_span"]; value) {
                    placement.row_span = static_cast<size_t>(value.as_integer());
                }
                if (tc::trent_view value = overrides["column_span"]; value) {
                    placement.column_span = static_cast<size_t>(value.as_integer());
                }
                if (tc::trent_view value = overrides["safe_area"]; value) {
                    safe_area =
                        value.as_string() == "respect" ? TC_UI_ROOT_LAYOUT_SAFE_AREA : TC_UI_ROOT_LAYOUT_FULL_VIEWPORT;
                }
            }
            tc_widget_set_visible(widget, visible);
            if (has_layout_override || binding.active != active) {
                if (!tc_widget_set_layout_spec(widget, &layout)) {
                    throw UiScriptError("responsive target rejected a validated layout override");
                }
            }
            if (binding.is_box) {
                auto* box = static_cast<BoxLayout*>(widget->body);
                box->set_orientation(orientation).set_padding(box_padding).set_spacing(spacing);
            }
            if (binding.has_grid_placement) {
                tc_widget* parent_widget = tc_ui_document_resolve_widget(document_, binding.parent);
                if (!parent_widget || !static_cast<GridLayout*>(parent_widget->body)
                                           ->set_child_placement(binding.handle,
                                                                 placement.row,
                                                                 placement.column,
                                                                 placement.row_span,
                                                                 placement.column_span)) {
                    throw UiScriptError("responsive grid placement rejected by parent");
                }
            }
            if (binding.is_root && !tc_ui_document_set_root_layout_policy(document_, safe_area)) {
                throw UiScriptError("responsive root rejected safe-area policy");
            }
            binding.active = active;
        }

        void refresh(tc_ui_rect& rect) {
            float width = rect.width;
            float height = rect.height;
            tc_ui_presentation_metrics metrics;
            if (tc_ui_document_has_presentation_metrics(document_) &&
                tc_ui_document_presentation_metrics(document_, &metrics)) {
                tc_ui_rect viewport;
                if (tc_ui_presentation_metrics_logical_viewport(&metrics, &viewport)) {
                    width = viewport.width;
                    height = viewport.height;
                }
            }
            for (Binding& binding : bindings_) {
                std::vector<bool> active;
                active.reserve(binding.variants.size());
                for (const UiScriptVariant& variant : binding.variants) {
                    active.push_back(matches(variant.selector, width, height));
                }
                if (active != binding.active) {
                    apply(binding, active);
                }
            }
            if (tc_ui_document_has_presentation_metrics(document_)) {
                tc_ui_rect policy_rect;
                if (tc_ui_document_presentation_layout_rect(document_, &policy_rect)) {
                    rect = policy_rect;
                }
            }
        }

    public:
        ResponsiveRuntime(tc_ui_document_handle document,
                          const UiScriptNode& root,
                          const std::unordered_map<std::string, tc_widget_handle>& handles)
            : document_(document) {
            collect(root, tc_widget_handle_invalid(), "", handles);
            if (!bindings_.empty()) {
                callback_token_ =
                    tc_ui_internal_add_layout_prepare(document_, &ResponsiveRuntime::prepare_callback, this);
                if (callback_token_ == 0) {
                    throw UiScriptError("failed to register responsive layout runtime");
                }
            }
        }

        ~ResponsiveRuntime() {
            if (callback_token_ != 0) {
                tc_ui_internal_remove_layout_prepare(document_, callback_token_);
            }
        }
    };

    UiScriptDescription UiScriptParser::parse(const std::string& source) const {
        tc::trent document;
        try {
            document = tc::yaml::parse(source);
        } catch (const std::exception& error) {
            throw UiScriptError(std::string("invalid native UiScript YAML: ") + error.what());
        }
        return parse_document(document);
    }

    UiScriptDescription UiScriptParser::parse_document(tc::trent_view document) const {
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
        if (!version.is_integer() || version.as_integer() != static_cast<int64_t>(UISCRIPT_VERSION)) {
            fail("document.uiscript", "expected dialect version " + std::to_string(UISCRIPT_VERSION));
        }
        if (!document.contains("root")) {
            fail("document", "missing root");
        }
        UiScriptDescription description;
        std::unordered_set<std::string> names;
        std::unordered_set<std::string> dependency_set;
        description.root = parse_node(document["root"], "root", names, description.type_dependencies, dependency_set);
        return description;
    }

    LoadedUiScript::LoadedUiScript() = default;

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
        responsive_runtime_ = std::move(other.responsive_runtime_);
        owns_document_ = other.owns_document_;
        closed_ = other.closed_;
        other.document_ = tc_ui_document_handle_invalid();
        other.root_ = {};
        other.widgets_.clear();
        other.responsive_runtime_.reset();
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
        responsive_runtime_.reset();
        if (owns_document_) {
            if (tc_ui_document_is_valid(document_)) {
                tc_ui_document_destroy(document_);
            }
        } else if (tc_ui_document_is_valid(document_) && tc_ui_document_resolve_widget(document_, root_.handle)) {
            tc_ui_document_destroy_widget_recursive(document_, root_.handle);
        }
        document_ = tc_ui_document_handle_invalid();
        root_ = {};
        widgets_.clear();
    }

    LoadedUiScript UiScriptLoader::load(const std::string& path, TcDocument document) const {
        return load_string(read_file(path), document, path);
    }

    LoadedUiScript
    UiScriptLoader::load_string(const std::string& source, TcDocument document, const std::string& source_name) const {
        try {
            return materialize(parser.parse(source), document);
        } catch (const std::exception& error) {
            tc_log_error(
                "[termin-gui-native] failed to load native UiScript '%s': %s", source_name.c_str(), error.what());
            throw;
        }
    }

    LoadedUiScript UiScriptLoader::materialize(const UiScriptDescription& description,
                                               TcDocument supplied_document) const {
        const bool owns_document = !supplied_document.valid();
        const tc_ui_document_handle document = owns_document ? tc_ui_document_create() : supplied_document.handle();
        if (!tc_ui_document_is_valid(document)) {
            throw UiScriptError("failed to create target UI document");
        }
        std::vector<tc_widget_handle> created;
        std::unordered_map<std::string, tc_widget_handle> all_handles;
        LoadedUiScript loaded;
        loaded.document_ = document;
        loaded.description_ = description;
        loaded.owns_document_ = owns_document;
        loaded.closed_ = false;
        try {
            loaded.root_ = materialize_node(document, description.root, created, loaded.widgets_, all_handles);
            if (!tc_ui_document_add_root(document, loaded.root_.handle)) {
                fail(description.root.source_path, "native document rejected root");
            }
            loaded.responsive_runtime_ = std::make_unique<ResponsiveRuntime>(document, description.root, all_handles);
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

    LoadedUiScript
    UiScriptLoader::reload(LoadedUiScript& loaded, const std::string& source, const std::string& source_name) const {
        if (loaded.closed_ || !tc_ui_document_is_valid(loaded.document_)) {
            throw UiScriptError("cannot reload a closed native UiScript");
        }
        UiScriptDescription description;
        try {
            description = parser.parse(source);
        } catch (const std::exception& error) {
            tc_log_error(
                "[termin-gui-native] failed to reload native UiScript '%s': %s", source_name.c_str(), error.what());
            throw;
        }
        return reload(loaded, description);
    }

    LoadedUiScript UiScriptLoader::reload(LoadedUiScript& loaded, const UiScriptDescription& description) const {
        if (loaded.closed_ || !tc_ui_document_is_valid(loaded.document_)) {
            throw UiScriptError("cannot reload a closed native UiScript");
        }
        LoadedUiScript replacement = materialize(description, TcDocument(loaded.document_));
        replacement.owns_document_ = loaded.owns_document_;
        loaded.owns_document_ = false;
        loaded.close();
        return replacement;
    }

} // namespace termin::gui_native
