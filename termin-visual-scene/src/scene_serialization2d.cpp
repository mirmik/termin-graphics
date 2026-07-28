#include "termin_visual_scene/scene_inspection2d.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <type_traits>
#include <unordered_set>
#include <utility>

#include <tcbase/tc_log.hpp>

namespace termin::visual {
namespace {

constexpr const char* kSchemaName = "termin.visual_scene.2d";

tc::trent vector2(termin::Vec2f value) {
    tc::trent result = tc::trent::list();
    result.push_back(value.x);
    result.push_back(value.y);
    return result;
}

tc::trent rect(termin::Rect2f value) {
    tc::trent result = tc::trent::list();
    result.push_back(value.x);
    result.push_back(value.y);
    result.push_back(value.width);
    result.push_back(value.height);
    return result;
}

tc::trent bounds(termin::Bounds2f value) {
    tc::trent result = tc::trent::list();
    result.push_back(value.x0);
    result.push_back(value.y0);
    result.push_back(value.x1);
    result.push_back(value.y1);
    return result;
}

tc::trent color(tgfx::Color4f value) {
    tc::trent result = tc::trent::list();
    result.push_back(value.r);
    result.push_back(value.g);
    result.push_back(value.b);
    result.push_back(value.a);
    return result;
}

const char* fill_rule_name(tgfx::FillRule value) {
    return value == tgfx::FillRule::EvenOdd ? "even-odd" : "non-zero";
}

const char* stroke_join_name(tgfx::StrokeJoin value) {
    switch (value) {
        case tgfx::StrokeJoin::Miter: return "miter";
        case tgfx::StrokeJoin::Round: return "round";
        case tgfx::StrokeJoin::Bevel: return "bevel";
    }
    return "";
}

const char* stroke_cap_name(tgfx::StrokeCap value) {
    switch (value) {
        case tgfx::StrokeCap::Butt: return "butt";
        case tgfx::StrokeCap::Round: return "round";
        case tgfx::StrokeCap::Square: return "square";
    }
    return "";
}

tc::trent path(const tgfx::Path2f& value) {
    tc::trent result = tc::trent::dict();
    tc::trent verbs = tc::trent::list();
    for (const auto verb : value.verbs()) {
        switch (verb) {
            case tgfx::Path2Verb::MoveTo: verbs.push_back("move"); break;
            case tgfx::Path2Verb::LineTo: verbs.push_back("line"); break;
            case tgfx::Path2Verb::QuadraticTo:
                verbs.push_back("quadratic");
                break;
            case tgfx::Path2Verb::CubicTo: verbs.push_back("cubic"); break;
            case tgfx::Path2Verb::Close: verbs.push_back("close"); break;
        }
    }
    tc::trent points = tc::trent::list();
    for (const auto point : value.points()) {
        points.push_back(vector2(point));
    }
    result.set("verbs", std::move(verbs));
    result.set("points", std::move(points));
    return result;
}

tc::trent fill(const tgfx::FillPaint& value) {
    tc::trent result = tc::trent::dict();
    result.set("color", color(value.color));
    result.set("rule", fill_rule_name(value.rule));
    return result;
}

tc::trent stroke(const tgfx::StrokePaint& value) {
    tc::trent result = tc::trent::dict();
    result.set("color", color(value.color));
    result.set("width", value.width);
    result.set("join", stroke_join_name(value.join));
    result.set("cap", stroke_cap_name(value.cap));
    result.set("miter_limit", value.miter_limit);
    tc::trent dash = tc::trent::list();
    for (const float length : value.dash_pattern) dash.push_back(length);
    result.set("dash", std::move(dash));
    result.set("dash_offset", value.dash_offset);
    return result;
}

tc::trent optional_stroke(const std::optional<tgfx::StrokePaint>& value) {
    return value ? stroke(*value) : tc::trent::nil();
}

tc::trent optional_fill(const std::optional<tgfx::FillPaint>& value) {
    return value ? fill(*value) : tc::trent::nil();
}

tc::trent state(const GraphicItemState2D& value) {
    tc::trent result = tc::trent::dict();
    tc::trent transform = tc::trent::list();
    transform.push_back(value.local_transform.m00);
    transform.push_back(value.local_transform.m01);
    transform.push_back(value.local_transform.m10);
    transform.push_back(value.local_transform.m11);
    transform.push_back(value.local_transform.tx);
    transform.push_back(value.local_transform.ty);
    result.set("transform", std::move(transform));
    result.set("visible", value.visible);
    result.set("enabled", value.enabled);
    result.set("opacity", value.opacity);
    result.set("z_order", value.z_order);
    if (value.clip) {
        tc::trent clip = tc::trent::dict();
        clip.set("path", path(value.clip->path));
        clip.set("rule", fill_rule_name(value.clip->rule));
        result.set("clip", std::move(clip));
    } else {
        result.set("clip", tc::trent::nil());
    }
    return result;
}

tc::trent payload(const GraphicItemPayload2D& payload_value) {
    return std::visit(
        [](const auto& value) {
            using T = std::decay_t<decltype(value)>;
            tc::trent result = tc::trent::dict();
            if constexpr (std::is_same_v<T, GroupItem2D>) {
                return result;
            } else if constexpr (std::is_same_v<T, RectItem2D>) {
                result.set("rect", rect(value.rect));
                result.set("fill", fill(value.fill));
                result.set("stroke", optional_stroke(value.stroke));
            } else if constexpr (std::is_same_v<T, RoundedRectItem2D>) {
                result.set("rect", rect(value.rect));
                result.set("radius", value.radius);
                result.set("fill", fill(value.fill));
                result.set("stroke", optional_stroke(value.stroke));
            } else if constexpr (std::is_same_v<T, EllipseItem2D>) {
                result.set("bounds", rect(value.bounds));
                result.set("fill", fill(value.fill));
                result.set("stroke", optional_stroke(value.stroke));
            } else if constexpr (std::is_same_v<T, PathItem2D>) {
                result.set("path", path(value.path));
                result.set("fill", optional_fill(value.fill));
                result.set("stroke", optional_stroke(value.stroke));
            } else if constexpr (std::is_same_v<T, PolylineItem2D>) {
                tc::trent points = tc::trent::list();
                for (const auto point : value.points) {
                    points.push_back(vector2(point));
                }
                result.set("points", std::move(points));
                result.set("stroke", stroke(value.stroke));
                result.set("closed", value.closed);
            } else if constexpr (std::is_same_v<T, TextItem2D>) {
                result.set("text", value.text);
                result.set("font", value.font.uri);
                result.set("origin", vector2(value.origin));
                result.set("size_px", value.size_px);
                result.set("color", color(value.color));
                const char* anchor = "left";
                if (value.anchor == tgfx::TextAnchor2D::Center) {
                    anchor = "center";
                } else if (value.anchor == tgfx::TextAnchor2D::Right) {
                    anchor = "right";
                }
                result.set("anchor", anchor);
                result.set("layout_bounds", bounds(value.layout_bounds));
            } else if constexpr (std::is_same_v<T, ImageItem2D>) {
                result.set("image", value.image.uri);
                result.set("rect", rect(value.rect));
                result.set("uv", rect(value.uv));
                result.set("tint", color(value.tint));
                result.set(
                    "sampling",
                    value.sampling == tgfx::DrawTextureSampling2D::Nearest
                        ? "nearest" : "linear");
            } else if constexpr (std::is_same_v<T, HitRegionItem2D>) {
                result.set("path", path(value.path));
                result.set("rule", fill_rule_name(value.rule));
            } else {
                result.set("key", value.key);
                result.set("local_bounds", bounds(value.local_bounds));
            }
            return result;
        },
        payload_value);
}

[[noreturn]] void invalid(const std::string& message) {
    throw std::runtime_error(message);
}

tc::trent_view field(
    tc::trent_view object,
    const char* name,
    tc::trent_type expected) {
    if (!object.is_dict() || !object.contains(name)) {
        invalid(std::string("missing field '") + name + "'");
    }
    const tc::trent_view result = object.dict_get(name);
    if (result.type() != expected) {
        invalid(std::string("invalid field '") + name + "'");
    }
    return result;
}

std::string string_field(tc::trent_view object, const char* name) {
    return field(object, name, tc::trent_type::string).as_string();
}

bool bool_field(tc::trent_view object, const char* name) {
    return field(object, name, tc::trent_type::boolean).as_bool();
}

std::int64_t integer_field(tc::trent_view object, const char* name) {
    if (!object.is_dict() || !object.contains(name)
        || !object.dict_get(name).is_integer()) {
        invalid(std::string("invalid integer field '") + name + "'");
    }
    return object.dict_get(name).as_integer();
}

float number(tc::trent_view value, const char* context) {
    if (!value.is_numer()) invalid(std::string("invalid number in ") + context);
    const double parsed = value.as_numer();
    if (!std::isfinite(parsed)
        || parsed < -std::numeric_limits<float>::max()
        || parsed > std::numeric_limits<float>::max()) {
        invalid(std::string("non-finite number in ") + context);
    }
    return static_cast<float>(parsed);
}

float number_field(tc::trent_view object, const char* name) {
    if (!object.is_dict() || !object.contains(name)) {
        invalid(std::string("missing field '") + name + "'");
    }
    return number(object.dict_get(name), name);
}

std::uint64_t unsigned_string_field(
    tc::trent_view object,
    const char* name) {
    const std::string text = string_field(object, name);
    std::uint64_t result = 0;
    const auto parsed =
        std::from_chars(text.data(), text.data() + text.size(), result);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size()) {
        invalid(std::string("invalid unsigned field '") + name + "'");
    }
    return result;
}

template <std::size_t N>
std::array<float, N> number_array(
    tc::trent_view value,
    const char* context) {
    if (!value.is_list() || value.size() != N) {
        invalid(std::string("invalid array in ") + context);
    }
    std::array<float, N> result{};
    for (std::size_t i = 0; i < N; ++i) {
        result[i] = number(value.list_at(i), context);
    }
    return result;
}

termin::Vec2f parse_vector2(tc::trent_view value, const char* context) {
    const auto data = number_array<2>(value, context);
    return {data[0], data[1]};
}

termin::Rect2f parse_rect(tc::trent_view value, const char* context) {
    const auto data = number_array<4>(value, context);
    return {data[0], data[1], data[2], data[3]};
}

termin::Bounds2f parse_bounds(tc::trent_view value, const char* context) {
    const auto data = number_array<4>(value, context);
    return {data[0], data[1], data[2], data[3]};
}

tgfx::Color4f parse_color(tc::trent_view value, const char* context) {
    const auto data = number_array<4>(value, context);
    return {data[0], data[1], data[2], data[3]};
}

tgfx::FillRule parse_fill_rule(std::string_view value) {
    if (value == "non-zero") return tgfx::FillRule::NonZero;
    if (value == "even-odd") return tgfx::FillRule::EvenOdd;
    invalid("unknown fill rule");
}

tgfx::Path2f parse_path(tc::trent_view value) {
    const auto verbs_value = field(value, "verbs", tc::trent_type::list);
    const auto points_value = field(value, "points", tc::trent_type::list);
    std::vector<tgfx::Path2Verb> verbs;
    std::vector<termin::Vec2f> points;
    verbs.reserve(verbs_value.size());
    points.reserve(points_value.size());
    for (const auto item : verbs_value.as_list()) {
        if (!item.is_string()) invalid("path verb is not a string");
        const std::string name = item.as_string();
        if (name == "move") {
            verbs.push_back(tgfx::Path2Verb::MoveTo);
        } else if (name == "line") {
            verbs.push_back(tgfx::Path2Verb::LineTo);
        } else if (name == "quadratic") {
            verbs.push_back(tgfx::Path2Verb::QuadraticTo);
        } else if (name == "cubic") {
            verbs.push_back(tgfx::Path2Verb::CubicTo);
        } else if (name == "close") {
            verbs.push_back(tgfx::Path2Verb::Close);
        } else {
            invalid("unknown path verb");
        }
    }
    for (const auto item : points_value.as_list()) {
        points.push_back(parse_vector2(item, "path point"));
    }
    tgfx::Path2f result;
    if (!result.try_assign(verbs, points)) invalid("invalid path stream");
    return result;
}

tgfx::FillPaint parse_fill(tc::trent_view value) {
    tgfx::FillPaint result{
        parse_color(field(value, "color", tc::trent_type::list), "fill color"),
        parse_fill_rule(string_field(value, "rule")),
    };
    if (!result.validate()) invalid("invalid fill paint");
    return result;
}

tgfx::StrokePaint parse_stroke(tc::trent_view value) {
    tgfx::StrokePaint result;
    result.color =
        parse_color(field(value, "color", tc::trent_type::list), "stroke color");
    result.width = number_field(value, "width");
    const std::string join = string_field(value, "join");
    if (join == "miter") result.join = tgfx::StrokeJoin::Miter;
    else if (join == "round") result.join = tgfx::StrokeJoin::Round;
    else if (join == "bevel") result.join = tgfx::StrokeJoin::Bevel;
    else invalid("unknown stroke join");
    const std::string cap = string_field(value, "cap");
    if (cap == "butt") result.cap = tgfx::StrokeCap::Butt;
    else if (cap == "round") result.cap = tgfx::StrokeCap::Round;
    else if (cap == "square") result.cap = tgfx::StrokeCap::Square;
    else invalid("unknown stroke cap");
    result.miter_limit = number_field(value, "miter_limit");
    const auto dash = field(value, "dash", tc::trent_type::list);
    for (const auto item : dash.as_list()) {
        result.dash_pattern.push_back(number(item, "stroke dash"));
    }
    result.dash_offset = number_field(value, "dash_offset");
    if (!result.validate()) invalid("invalid stroke paint");
    return result;
}

std::optional<tgfx::FillPaint> parse_optional_fill(tc::trent_view value) {
    if (value.is_nil()) return std::nullopt;
    if (!value.is_dict()) invalid("invalid optional fill");
    return parse_fill(value);
}

std::optional<tgfx::StrokePaint> parse_optional_stroke(tc::trent_view value) {
    if (value.is_nil()) return std::nullopt;
    if (!value.is_dict()) invalid("invalid optional stroke");
    return parse_stroke(value);
}

tc::trent_view optional_field(
    tc::trent_view object,
    const char* name) {
    if (!object.is_dict() || !object.contains(name)) {
        invalid(std::string("missing field '") + name + "'");
    }
    return object.dict_get(name);
}

GraphicItemState2D parse_state(tc::trent_view value) {
    GraphicItemState2D result;
    const auto transform = number_array<6>(
        field(value, "transform", tc::trent_type::list),
        "state transform");
    result.local_transform = {
        transform[0],
        transform[1],
        transform[2],
        transform[3],
        transform[4],
        transform[5],
    };
    result.visible = bool_field(value, "visible");
    result.enabled = bool_field(value, "enabled");
    result.opacity = number_field(value, "opacity");
    result.z_order = integer_field(value, "z_order");
    if (!value.contains("clip")) invalid("missing field 'clip'");
    const auto clip = value.dict_get("clip");
    if (!clip.is_nil()) {
        if (!clip.is_dict()) invalid("invalid clip");
        result.clip = GeometricClip2D{
            parse_path(field(clip, "path", tc::trent_type::dict)),
            parse_fill_rule(string_field(clip, "rule")),
        };
    }
    return result;
}

GraphicItemPayload2D parse_payload(
    const std::string& type,
    tc::trent_view value) {
    if (!value.is_dict()) invalid("payload is not a dictionary");
    if (type == "termin.visual.Group2D") return GroupItem2D{};
    if (type == "termin.visual.Rect2D") {
        return RectItem2D{
            parse_rect(field(value, "rect", tc::trent_type::list), "rect"),
            parse_fill(field(value, "fill", tc::trent_type::dict)),
            parse_optional_stroke(optional_field(value, "stroke")),
        };
    }
    if (type == "termin.visual.RoundedRect2D") {
        return RoundedRectItem2D{
            parse_rect(field(value, "rect", tc::trent_type::list), "rect"),
            number_field(value, "radius"),
            parse_fill(field(value, "fill", tc::trent_type::dict)),
            parse_optional_stroke(optional_field(value, "stroke")),
        };
    }
    if (type == "termin.visual.Ellipse2D") {
        return EllipseItem2D{
            parse_rect(field(value, "bounds", tc::trent_type::list), "bounds"),
            parse_fill(field(value, "fill", tc::trent_type::dict)),
            parse_optional_stroke(optional_field(value, "stroke")),
        };
    }
    if (type == "termin.visual.Path2D") {
        return PathItem2D{
            parse_path(field(value, "path", tc::trent_type::dict)),
            parse_optional_fill(optional_field(value, "fill")),
            parse_optional_stroke(optional_field(value, "stroke")),
        };
    }
    if (type == "termin.visual.Polyline2D") {
        std::vector<termin::Vec2f> points;
        for (const auto item :
             field(value, "points", tc::trent_type::list).as_list()) {
            points.push_back(parse_vector2(item, "polyline point"));
        }
        return PolylineItem2D{
            std::move(points),
            parse_stroke(field(value, "stroke", tc::trent_type::dict)),
            bool_field(value, "closed"),
        };
    }
    if (type == "termin.visual.Text2D") {
        tgfx::TextAnchor2D anchor;
        const std::string name = string_field(value, "anchor");
        if (name == "left") anchor = tgfx::TextAnchor2D::Left;
        else if (name == "center") anchor = tgfx::TextAnchor2D::Center;
        else if (name == "right") anchor = tgfx::TextAnchor2D::Right;
        else invalid("unknown text anchor");
        return TextItem2D{
            string_field(value, "text"),
            {string_field(value, "font")},
            parse_vector2(
                field(value, "origin", tc::trent_type::list), "text origin"),
            number_field(value, "size_px"),
            parse_color(
                field(value, "color", tc::trent_type::list), "text color"),
            anchor,
            parse_bounds(
                field(value, "layout_bounds", tc::trent_type::list),
                "text layout bounds"),
        };
    }
    if (type == "termin.visual.Image2D") {
        const std::string sampling = string_field(value, "sampling");
        tgfx::DrawTextureSampling2D sampling_value;
        if (sampling == "linear") {
            sampling_value = tgfx::DrawTextureSampling2D::Linear;
        } else if (sampling == "nearest") {
            sampling_value = tgfx::DrawTextureSampling2D::Nearest;
        } else {
            invalid("unknown image sampling");
        }
        return ImageItem2D{
            {string_field(value, "image")},
            parse_rect(field(value, "rect", tc::trent_type::list), "image rect"),
            parse_rect(field(value, "uv", tc::trent_type::list), "image uv"),
            parse_color(field(value, "tint", tc::trent_type::list), "image tint"),
            sampling_value,
        };
    }
    if (type == "termin.visual.HitRegion2D") {
        return HitRegionItem2D{
            parse_path(field(value, "path", tc::trent_type::dict)),
            parse_fill_rule(string_field(value, "rule")),
        };
    }
    if (type == "termin.visual.CustomBatch2D") {
        return CustomBatchItem2D{
            string_field(value, "key"),
            parse_bounds(
                field(value, "local_bounds", tc::trent_type::list),
                "custom bounds"),
        };
    }
    invalid("unregistered payload type '" + type + "'");
}

struct ParsedItem {
    std::optional<std::uint32_t> parent;
    std::vector<std::uint32_t> children;
    std::uint64_t stable_id = 0;
    std::uint64_t revision = 0;
    std::uint64_t topology_revision = 0;
    GraphicItemState2D state;
    GraphicItemPayload2D payload;
};

struct ParsedScene {
    std::uint64_t revision = 0;
    std::vector<ParsedItem> items;
};

ParsedScene parse_scene(const tc::trent& serialized) {
    const auto root = serialized.view();
    if (!root.is_dict()) invalid("root is not a dictionary");
    if (string_field(root, "schema") != kSchemaName) {
        invalid("unknown scene schema");
    }
    if (integer_field(root, "version") != kSceneSerializationVersion2D) {
        invalid("unsupported scene schema version");
    }
    ParsedScene result;
    result.revision = unsigned_string_field(root, "revision");
    const auto items = field(root, "items", tc::trent_type::list);
    result.items.reserve(items.size());
    std::unordered_set<std::uint64_t> stable_ids;
    std::vector<std::vector<std::uint32_t>> expected_children(items.size());

    for (std::size_t i = 0; i < items.size(); ++i) {
        const auto item = items.list_at(i);
        if (!item.is_dict()) invalid("item is not a dictionary");
        if (integer_field(item, "record") != static_cast<std::int64_t>(i)) {
            invalid("record indices are not canonical");
        }
        ParsedItem parsed;
        if (!item.contains("parent")) invalid("missing field 'parent'");
        const auto parent = item.dict_get("parent");
        if (!parent.is_nil()) {
            if (!parent.is_integer()) invalid("invalid parent record");
            const auto parent_index = parent.as_integer();
            if (parent_index < 0
                || parent_index >= static_cast<std::int64_t>(i)) {
                invalid("parent must precede its child");
            }
            parsed.parent = static_cast<std::uint32_t>(parent_index);
            expected_children[*parsed.parent].push_back(
                static_cast<std::uint32_t>(i));
        }
        for (const auto child :
             field(item, "children", tc::trent_type::list).as_list()) {
            if (!child.is_integer()
                || child.as_integer() <= static_cast<std::int64_t>(i)
                || child.as_integer()
                    >= static_cast<std::int64_t>(items.size())) {
                invalid("invalid child record");
            }
            parsed.children.push_back(
                static_cast<std::uint32_t>(child.as_integer()));
        }
        parsed.stable_id = unsigned_string_field(item, "stable_id");
        if (parsed.stable_id == 0
            || !stable_ids.insert(parsed.stable_id).second) {
            invalid("stable ids must be unique and non-zero");
        }
        parsed.revision = unsigned_string_field(item, "revision");
        parsed.topology_revision =
            unsigned_string_field(item, "topology_revision");
        if (parsed.revision > result.revision
            || parsed.topology_revision > result.revision) {
            invalid("item revision exceeds scene revision");
        }
        parsed.state =
            parse_state(field(item, "state", tc::trent_type::dict));
        const std::string type = string_field(item, "type");
        parsed.payload = parse_payload(
            type, field(item, "payload", tc::trent_type::dict));
        if (type != payload_type_name(parsed.payload)) {
            invalid("payload type registry mismatch");
        }
        result.items.push_back(std::move(parsed));
    }
    for (std::size_t i = 0; i < result.items.size(); ++i) {
        if (result.items[i].children != expected_children[i]) {
            invalid("parent and child topology disagree");
        }
    }
    return result;
}

}  // namespace

tc::trent VisualScene2D::serialize() const {
    const SceneInspection2D inspected = inspection();
    tc::trent root = tc::trent::dict();
    root.set("schema", kSchemaName);
    root.set(
        "version",
        static_cast<std::int64_t>(kSceneSerializationVersion2D));
    root.set("revision", std::to_string(inspected.scene_revision));
    tc::trent items = tc::trent::list();
    for (const auto& item : inspected.items) {
        tc::trent encoded = tc::trent::dict();
        encoded.set("record", static_cast<std::int64_t>(item.record_index));
        if (item.parent_index) {
            encoded.set(
                "parent", static_cast<std::int64_t>(*item.parent_index));
        } else {
            encoded.set("parent", tc::trent::nil());
        }
        tc::trent children = tc::trent::list();
        for (const auto child : item.children) {
            children.push_back(static_cast<std::int64_t>(child));
        }
        encoded.set("children", std::move(children));
        encoded.set("stable_id", std::to_string(item.stable_id));
        encoded.set("revision", std::to_string(item.revision));
        encoded.set(
            "topology_revision", std::to_string(item.topology_revision));
        encoded.set("type", item.type_name);
        encoded.set("state", state(item.state));
        encoded.set("payload", payload(item.payload));
        items.push_back(std::move(encoded));
    }
    root.set("items", std::move(items));
    return root;
}

bool VisualScene2D::restore(const tc::trent& serialized) {
    try {
        {
            std::scoped_lock lock(mutex_);
            if (!records_.empty()) {
                tc::Log::error(
                    "VisualScene2D::restore: destination scene is not empty");
                return false;
            }
        }
        ParsedScene parsed = parse_scene(serialized);
        VisualScene2D staging;
        std::vector<GraphicItemHandle> handles;
        handles.reserve(parsed.items.size());
        std::uint64_t maximum_stable_id = 0;
        for (const auto& item : parsed.items) {
            const GraphicItemHandle parent = item.parent
                ? handles[*item.parent]
                : tc_graphic_item_handle_invalid();
            const auto handle = staging.create(item.payload, parent);
            if (!handle || !staging.set_state(*handle, item.state)) {
                tc::Log::error(
                    "VisualScene2D::restore: staged item construction failed");
                return false;
            }
            handles.push_back(*handle);
            auto& record = staging.records_.at(handle->index);
            record.stable_order = item.stable_id;
            record.revision = item.revision;
            record.topology_revision = item.topology_revision;
            maximum_stable_id =
                std::max(maximum_stable_id, item.stable_id);
        }
        if (maximum_stable_id == std::numeric_limits<std::uint64_t>::max()) {
            invalid("stable id space is exhausted");
        }
        staging.next_stable_order_ = maximum_stable_id + 1;
        staging.revision_ = parsed.revision;

        std::scoped_lock lock(mutex_);
        if (!records_.empty()) {
            tc::Log::error(
                "VisualScene2D::restore: destination changed during restore");
            return false;
        }
        storage_ = std::move(staging.storage_);
        records_ = std::move(staging.records_);
        next_stable_order_ = staging.next_stable_order_;
        revision_ = staging.revision_;
        return true;
    } catch (const std::exception& error) {
        tc::Log::error("VisualScene2D::restore: %s", error.what());
        return false;
    } catch (...) {
        tc::Log::error("VisualScene2D::restore: unknown failure");
        return false;
    }
}

}  // namespace termin::visual
