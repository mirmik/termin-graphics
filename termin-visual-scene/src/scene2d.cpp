#include "termin_visual_scene/scene2d.hpp"

#include <algorithm>
#include <cmath>
#include <string_view>
#include <type_traits>
#include <utility>

#include <tcbase/tc_log.hpp>

namespace termin::visual {
namespace {

bool finite(float value) {
    return std::isfinite(value);
}

bool valid_bounds(termin::Bounds2f value) {
    return finite(value.x0) && finite(value.y0) &&
           finite(value.x1) && finite(value.y1) &&
           value.x1 >= value.x0 && value.y1 >= value.y0;
}

bool valid_rect(termin::Rect2f value) {
    return finite(value.x) && finite(value.y) &&
           finite(value.width) && finite(value.height) &&
           value.width >= 0.0f && value.height >= 0.0f;
}

bool valid_point(termin::Vec2f value) {
    return finite(value.x) && finite(value.y);
}

bool valid_rule(tgfx::FillRule value) {
    return value == tgfx::FillRule::NonZero ||
           value == tgfx::FillRule::EvenOdd;
}

bool valid_clip(const GeometricClip2D& value) {
    return !value.path.empty() && valid_rule(value.rule);
}

bool validate_state(const GraphicItemState2D& value) {
    return value.local_transform.is_finite() &&
           finite(value.opacity) && value.opacity >= 0.0f &&
           value.opacity <= 1.0f &&
           (!value.clip || valid_clip(*value.clip));
}

bool validate_payload(const GraphicItemPayload2D& payload) {
    return std::visit(
        [](const auto& value) {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, GroupItem2D>) {
                return true;
            } else if constexpr (std::is_same_v<T, RectItem2D>) {
                return valid_rect(value.rect) && value.fill.validate() &&
                       (!value.stroke || value.stroke->validate());
            } else if constexpr (std::is_same_v<T, RoundedRectItem2D>) {
                return valid_rect(value.rect) && finite(value.radius) &&
                       value.radius >= 0.0f && value.fill.validate() &&
                       (!value.stroke || value.stroke->validate());
            } else if constexpr (std::is_same_v<T, EllipseItem2D>) {
                return valid_rect(value.bounds) && value.fill.validate() &&
                       (!value.stroke || value.stroke->validate());
            } else if constexpr (std::is_same_v<T, PathItem2D>) {
                return !value.path.empty() && (value.fill || value.stroke) &&
                       (!value.fill || value.fill->validate()) &&
                       (!value.stroke || value.stroke->validate());
            } else if constexpr (std::is_same_v<T, PolylineItem2D>) {
                if (value.points.size() < 2 || !value.stroke.validate()) {
                    return false;
                }
                return std::all_of(
                    value.points.begin(), value.points.end(), valid_point);
            } else if constexpr (std::is_same_v<T, TextItem2D>) {
                return !value.text.empty() && value.font.valid() &&
                       valid_point(value.origin) && finite(value.size_px) &&
                       value.size_px > 0.0f && value.color.is_finite() &&
                       valid_bounds(value.layout_bounds);
            } else if constexpr (std::is_same_v<T, ImageItem2D>) {
                return value.image.valid() && valid_rect(value.rect) &&
                       valid_rect(value.uv) && value.tint.is_finite() &&
                       (value.sampling == tgfx::DrawTextureSampling2D::Linear ||
                        value.sampling == tgfx::DrawTextureSampling2D::Nearest);
            } else if constexpr (std::is_same_v<T, HitRegionItem2D>) {
                return !value.path.empty() && valid_rule(value.rule);
            } else {
                return !value.key.empty() && valid_bounds(value.local_bounds);
            }
        },
        payload);
}

termin::Bounds2f rect_bounds(termin::Rect2f rect) {
    return {rect.x, rect.y, rect.x + rect.width, rect.y + rect.height};
}

termin::Bounds2f expanded(termin::Bounds2f bounds, float amount) {
    return {
        bounds.x0 - amount,
        bounds.y0 - amount,
        bounds.x1 + amount,
        bounds.y1 + amount,
    };
}

termin::Bounds2f merged(termin::Bounds2f a, termin::Bounds2f b) {
    return {
        std::min(a.x0, b.x0),
        std::min(a.y0, b.y0),
        std::max(a.x1, b.x1),
        std::max(a.y1, b.y1),
    };
}

std::optional<termin::Bounds2f> payload_bounds(
    const GraphicItemPayload2D& payload) {
    return std::visit(
        [](const auto& value) -> std::optional<termin::Bounds2f> {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, GroupItem2D>) {
                return std::nullopt;
            } else if constexpr (
                std::is_same_v<T, RectItem2D> ||
                std::is_same_v<T, RoundedRectItem2D>) {
                auto result = rect_bounds(value.rect);
                if (value.stroke) result = expanded(result, value.stroke->width * 0.5f);
                return result;
            } else if constexpr (std::is_same_v<T, EllipseItem2D>) {
                auto result = rect_bounds(value.bounds);
                if (value.stroke) result = expanded(result, value.stroke->width * 0.5f);
                return result;
            } else if constexpr (std::is_same_v<T, PathItem2D>) {
                auto result = value.path.bounds();
                if (value.stroke) {
                    result = merged(result, value.path.stroke_bounds(*value.stroke));
                }
                return result;
            } else if constexpr (std::is_same_v<T, PolylineItem2D>) {
                termin::Bounds2f result{
                    value.points.front().x,
                    value.points.front().y,
                    value.points.front().x,
                    value.points.front().y,
                };
                for (const auto point : value.points) {
                    result.x0 = std::min(result.x0, point.x);
                    result.y0 = std::min(result.y0, point.y);
                    result.x1 = std::max(result.x1, point.x);
                    result.y1 = std::max(result.y1, point.y);
                }
                return expanded(result, value.stroke.width * 0.5f);
            } else if constexpr (std::is_same_v<T, TextItem2D>) {
                return value.layout_bounds;
            } else if constexpr (std::is_same_v<T, ImageItem2D>) {
                return rect_bounds(value.rect);
            } else if constexpr (std::is_same_v<T, HitRegionItem2D>) {
                return value.path.bounds();
            } else {
                return value.local_bounds;
            }
        },
        payload);
}

GeometricClip2D transformed_clip(
    const GeometricClip2D& clip,
    const termin::Affine2f& transform) {
    const auto flat = clip.path.flatten(0.25f, transform);
    tgfx::Path2f path;
    for (const auto& contour : flat.contours) {
        if (contour.points.empty()) continue;
        path.move_to(contour.points.front());
        for (std::size_t i = 1; i < contour.points.size(); ++i) {
            path.line_to(contour.points[i]);
        }
        if (contour.closed) path.close();
    }
    return {std::move(path), clip.rule};
}

template<typename T>
constexpr const char* builtin_type_name() {
    if constexpr (std::is_same_v<T, GroupItem2D>) {
        return "termin.visual.Group2D";
    } else if constexpr (std::is_same_v<T, RectItem2D>) {
        return "termin.visual.Rect2D";
    } else if constexpr (std::is_same_v<T, RoundedRectItem2D>) {
        return "termin.visual.RoundedRect2D";
    } else if constexpr (std::is_same_v<T, EllipseItem2D>) {
        return "termin.visual.Ellipse2D";
    } else if constexpr (std::is_same_v<T, PathItem2D>) {
        return "termin.visual.Path2D";
    } else if constexpr (std::is_same_v<T, PolylineItem2D>) {
        return "termin.visual.Polyline2D";
    } else if constexpr (std::is_same_v<T, TextItem2D>) {
        return "termin.visual.Text2D";
    } else if constexpr (std::is_same_v<T, ImageItem2D>) {
        return "termin.visual.Image2D";
    } else if constexpr (std::is_same_v<T, HitRegionItem2D>) {
        return "termin.visual.HitRegion2D";
    } else {
        return "termin.visual.CustomBatch2D";
    }
}

void ensure_runtime_type(const char* type_name, const char* parent_name) {
    static std::mutex registry_mutex;
    std::scoped_lock lock(registry_mutex);
    if (tc_runtime_type_registry_has_type(type_name)) return;
    auto* descriptor = tc_runtime_type_descriptor_create(
        type_name, "termin-visual-scene", parent_name);
    if (descriptor == nullptr ||
        !tc_runtime_type_registry_commit_descriptor(descriptor)) {
        throw std::runtime_error(
            std::string("failed to register graphic item type ") +
            type_name);
    }
}

template<typename T>
void ensure_builtin_type() {
    ensure_runtime_type("termin.visual.GraphicItem", nullptr);
    ensure_runtime_type(
        builtin_type_name<T>(), "termin.visual.GraphicItem");
}

template<typename T>
struct BuiltinGraphicItem2D {
    tc_graphic_item base{};
    std::optional<GeometricClip2D> clip;
    T value;

    BuiltinGraphicItem2D(T payload, std::optional<GeometricClip2D> item_clip)
        : clip(std::move(item_clip)), value(std::move(payload)) {
        ensure_builtin_type<T>();
        tc_graphic_item_init_unowned(
            &base, vtable(), TC_LANGUAGE_CXX, this);
        base.clip_body = &clip;
    }

    static bool local_bounds(
        const tc_graphic_item* item,
        tc_bounds2f* out_bounds) {
        const auto* self =
            static_cast<const BuiltinGraphicItem2D*>(item->body);
        const auto bounds =
            payload_bounds(GraphicItemPayload2D{self->value});
        if (!bounds) return false;
        *out_bounds = *bounds;
        return true;
    }

    static bool prepare_snapshot(
        const tc_graphic_item* item,
        tc_graphic_item_snapshot_sink* sink) {
        if (sink == nullptr || sink->emit == nullptr) return false;
        const auto* self =
            static_cast<const BuiltinGraphicItem2D*>(item->body);
        return sink->emit(
            sink, builtin_type_name<T>(), &self->value);
    }

    static bool hit_test(
        const tc_graphic_item* item,
        tc_vec2f point,
        float tolerance) {
        tc_bounds2f bounds{};
        if (!local_bounds(item, &bounds)) return false;
        return point.x >= bounds.x0 - tolerance &&
               point.x <= bounds.x1 + tolerance &&
               point.y >= bounds.y0 - tolerance &&
               point.y <= bounds.y1 + tolerance;
    }

    static const tc_graphic_item_vtable* vtable() {
        static const tc_graphic_item_vtable result{
            .type_name = builtin_type_name<T>(),
            .local_bounds = local_bounds,
            .prepare_snapshot = prepare_snapshot,
            .hit_test = hit_test,
        };
        return &result;
    }
};

template<typename T>
void delete_builtin_item(tc_graphic_item* item) {
    delete static_cast<BuiltinGraphicItem2D<T>*>(item->body);
}

struct BuiltinCandidate {
    tc_graphic_item* item = nullptr;
    tc_graphic_item_deleter deleter = nullptr;
};

BuiltinCandidate make_builtin(
    GraphicItemPayload2D payload,
    std::optional<GeometricClip2D> clip = std::nullopt) {
    return std::visit(
        [&](auto value) {
            using T = std::decay_t<decltype(value)>;
            auto* body = new BuiltinGraphicItem2D<T>(
                std::move(value), std::move(clip));
            return BuiltinCandidate{
                &body->base,
                delete_builtin_item<T>,
            };
        },
        std::move(payload));
}

bool copy_builtin_payload(
    tc_graphic_item_snapshot_sink* sink,
    const char* type_name,
    const void* state) {
    if (sink == nullptr || sink->body == nullptr ||
        type_name == nullptr || state == nullptr) {
        return false;
    }
    auto& out = *static_cast<GraphicItemPayload2D*>(sink->body);
#define TC_COPY_BUILTIN(TYPE)                                                \
    if (std::string_view(type_name) == builtin_type_name<TYPE>()) {          \
        out = *static_cast<const TYPE*>(state);                               \
        return true;                                                          \
    }
    TC_COPY_BUILTIN(GroupItem2D)
    TC_COPY_BUILTIN(RectItem2D)
    TC_COPY_BUILTIN(RoundedRectItem2D)
    TC_COPY_BUILTIN(EllipseItem2D)
    TC_COPY_BUILTIN(PathItem2D)
    TC_COPY_BUILTIN(PolylineItem2D)
    TC_COPY_BUILTIN(TextItem2D)
    TC_COPY_BUILTIN(ImageItem2D)
    TC_COPY_BUILTIN(HitRegionItem2D)
    TC_COPY_BUILTIN(CustomBatchItem2D)
#undef TC_COPY_BUILTIN
    return false;
}

}  // namespace

bool VisualScene2D::owns_locked_(GraphicItemHandle item) const {
    if (tc_graphic_item_handle_is_invalid(item) || item.scene_id != storage_.id()) {
        return false;
    }
    GraphicItemView view{};
    return storage_.resolve(item, view);
}

tc_graphic_item* VisualScene2D::item_locked_(GraphicItemHandle item) const {
    if (!owns_locked_(item)) return nullptr;
    return storage_.resolve_item(item);
}

std::vector<GraphicItemHandle> VisualScene2D::handles_locked_() const {
    return storage_.handles();
}

bool VisualScene2D::payload_locked_(
    GraphicItemHandle item,
    GraphicItemPayload2D& out) const {
    const auto* object = item_locked_(item);
    if (object == nullptr || object->vtable == nullptr ||
        object->vtable->prepare_snapshot == nullptr) {
        return false;
    }
    tc_graphic_item_snapshot_sink sink{
        .emit = copy_builtin_payload,
        .body = &out,
    };
    return object->vtable->prepare_snapshot(object, &sink);
}

bool VisualScene2D::state_locked_(
    GraphicItemHandle item,
    GraphicItemState2D& out) const {
    tc_graphic_item_state common{};
    auto* object = item_locked_(item);
    if (object == nullptr || !storage_.get_state(item, common)) {
        return false;
    }
    out.local_transform = common.local_transform;
    out.visible = common.visible;
    out.enabled = common.enabled;
    out.opacity = common.opacity;
    out.z_order = common.z_order;
    if (object->clip_body != nullptr) {
        out.clip =
            *static_cast<const std::optional<GeometricClip2D>*>(
                object->clip_body);
    } else {
        out.clip.reset();
    }
    return true;
}

bool VisualScene2D::replace_payload_locked_(
    GraphicItemHandle item,
    GraphicItemPayload2D payload,
    std::optional<GeometricClip2D> clip) {
    auto candidate = make_builtin(std::move(payload), std::move(clip));
    return storage_.replace(item, candidate.item, candidate.deleter);
}

bool VisualScene2D::restore_metadata_locked_(
    GraphicItemHandle item,
    std::uint64_t stable_order,
    std::uint64_t revision,
    std::uint64_t topology_revision) {
    return storage_.restore_metadata(
        item, stable_order, revision, topology_revision);
}

std::optional<GraphicItemHandle> VisualScene2D::create(
    GraphicItemPayload2D payload,
    GraphicItemHandle parent) {
    if (!validate_payload(payload)) {
        tc::Log::error("VisualScene2D::create: invalid payload rejected");
        return std::nullopt;
    }
    std::scoped_lock lock(mutex_);
    if (!tc_graphic_item_handle_is_invalid(parent) && !owns_locked_(parent)) {
        tc::Log::error("VisualScene2D::create: parent is stale or foreign");
        return std::nullopt;
    }
    try {
        auto candidate = make_builtin(std::move(payload));
        const auto handle =
            storage_.adopt(candidate.item, candidate.deleter, parent);
        ++revision_;
        return handle;
    } catch (const std::exception& error) {
        tc::Log::error("VisualScene2D::create: %s", error.what());
        return std::nullopt;
    }
}

bool VisualScene2D::destroy_leaf(GraphicItemHandle item) {
    std::scoped_lock lock(mutex_);
    if (!owns_locked_(item)) {
        tc::Log::error("VisualScene2D::destroy_leaf: item is stale or foreign");
        return false;
    }
    if (!storage_.destroy_leaf(item)) return false;
    ++revision_;
    return true;
}

std::optional<termin::Bounds2f> VisualScene2D::subtree_local_bounds_locked_(
    GraphicItemHandle root) const {
    GraphicItemPayload2D payload;
    if (!payload_locked_(root, payload)) return std::nullopt;
    auto result = payload_bounds(payload);

    GraphicItemView view{};
    if (!storage_.resolve(root, view)) return result;
    auto child = view.first_child;
    while (!tc_graphic_item_handle_is_invalid(child)) {
        GraphicItemView child_view{};
        GraphicItemState2D child_state;
        if (!state_locked_(child, child_state) ||
            !storage_.resolve(child, child_view)) {
            return result;
        }
        const auto child_bounds = subtree_local_bounds_locked_(child);
        if (child_bounds) {
            const auto transformed =
                child_state.local_transform.transform_bounds(*child_bounds);
            result = result ? std::optional<termin::Bounds2f>(
                                  merged(*result, transformed))
                            : std::optional<termin::Bounds2f>(transformed);
        }
        child = child_view.next_sibling;
    }
    return result;
}

bool VisualScene2D::destroy_subtree(GraphicItemHandle item) {
    std::scoped_lock lock(mutex_);
    if (!owns_locked_(item)) {
        tc::Log::error("VisualScene2D::destroy_subtree: item is stale or foreign");
        return false;
    }
    if (!storage_.destroy_subtree(item)) return false;
    ++revision_;
    return true;
}

bool VisualScene2D::reparent(
    GraphicItemHandle item,
    GraphicItemHandle parent) {
    std::scoped_lock lock(mutex_);
    if (!owns_locked_(item) || !owns_locked_(parent)) {
        tc::Log::error("VisualScene2D::reparent: item or parent is stale or foreign");
        return false;
    }
    if (!storage_.reparent(item, parent)) return false;
    ++revision_;
    return true;
}

bool VisualScene2D::detach(GraphicItemHandle item) {
    std::scoped_lock lock(mutex_);
    if (!owns_locked_(item)) {
        tc::Log::error("VisualScene2D::detach: item is stale or foreign");
        return false;
    }
    if (!storage_.detach(item)) return false;
    ++revision_;
    return true;
}

void VisualScene2D::clear() {
    std::scoped_lock lock(mutex_);
    if (storage_.size() == 0) return;
    storage_.clear();
    ++revision_;
}

bool VisualScene2D::set_state(
    GraphicItemHandle item,
    GraphicItemState2D state) {
    if (!validate_state(state)) {
        tc::Log::error("VisualScene2D::set_state: invalid state rejected");
        return false;
    }
    std::scoped_lock lock(mutex_);
    auto* object = item_locked_(item);
    if (!object) {
        tc::Log::error("VisualScene2D::set_state: item is stale or foreign");
        return false;
    }
    const tc_graphic_item_state common{
        .local_transform = state.local_transform,
        .visible = state.visible,
        .enabled = state.enabled,
        .opacity = state.opacity,
        .z_order = state.z_order,
    };
    if (!storage_.set_state(item, common)) return false;
    if (object->clip_body != nullptr) {
        *static_cast<std::optional<GeometricClip2D>*>(object->clip_body) =
            std::move(state.clip);
    }
    ++revision_;
    return true;
}

bool VisualScene2D::set_payload(
    GraphicItemHandle item,
    GraphicItemPayload2D payload) {
    if (!validate_payload(payload)) {
        tc::Log::error("VisualScene2D::set_payload: invalid payload rejected");
        return false;
    }
    std::scoped_lock lock(mutex_);
    GraphicItemState2D state;
    if (!state_locked_(item, state)) {
        tc::Log::error("VisualScene2D::set_payload: item is stale or foreign");
        return false;
    }
    if (!replace_payload_locked_(
            item, std::move(payload), std::move(state.clip))) {
        return false;
    }
    ++revision_;
    return true;
}

bool VisualScene2D::set_item(
    GraphicItemHandle item,
    GraphicItemState2D state,
    GraphicItemPayload2D payload) {
    if (!validate_state(state) || !validate_payload(payload)) {
        tc::Log::error("VisualScene2D::set_item: invalid state or payload rejected");
        return false;
    }
    std::scoped_lock lock(mutex_);
    if (!owns_locked_(item)) {
        tc::Log::error("VisualScene2D::set_item: item is stale or foreign");
        return false;
    }
    const auto common = tc_graphic_item_state{
        .local_transform = state.local_transform,
        .visible = state.visible,
        .enabled = state.enabled,
        .opacity = state.opacity,
        .z_order = state.z_order,
    };
    if (!replace_payload_locked_(
            item, std::move(payload), std::move(state.clip)) ||
        !storage_.set_state(item, common)) {
        return false;
    }
    ++revision_;
    return true;
}

bool VisualScene2D::snapshot_locked_(
    GraphicItemHandle item,
    GraphicItemSnapshot2D& out) const {
    auto* object = item_locked_(item);
    if (!object) return false;
    GraphicItemState2D item_state;
    GraphicItemPayload2D item_payload;
    if (!state_locked_(item, item_state) ||
        !payload_locked_(item, item_payload)) {
        return false;
    }

    std::vector<GraphicItemHandle> ancestry;
    GraphicItemHandle cursor = item;
    while (!tc_graphic_item_handle_is_invalid(cursor)) {
        ancestry.push_back(cursor);
        GraphicItemView view{};
        if (!storage_.resolve(cursor, view)) return false;
        cursor = view.parent;
    }
    std::reverse(ancestry.begin(), ancestry.end());

    termin::Affine2f world = termin::Affine2f::identity();
    bool visible = true;
    bool enabled = true;
    float opacity = 1.0f;
    std::vector<GeometricClip2D> clips;
    for (const auto ancestor : ancestry) {
        GraphicItemState2D ancestor_state;
        if (!state_locked_(ancestor, ancestor_state)) return false;
        world = world * ancestor_state.local_transform;
        visible = visible && ancestor_state.visible;
        enabled = enabled && ancestor_state.enabled;
        opacity *= ancestor_state.opacity;
        if (ancestor_state.clip) {
            clips.push_back(
                transformed_clip(*ancestor_state.clip, world));
        }
    }

    GraphicItemView topology{};
    if (!storage_.resolve(item, topology)) return false;
    out.handle = item;
    out.parent = topology.parent;
    out.state = std::move(item_state);
    out.payload = std::move(item_payload);
    out.world_transform = world;
    out.effective_visible = visible;
    out.effective_enabled = enabled;
    out.effective_opacity = opacity;
    out.stable_order = object->stable_order;
    out.revision = object->revision;
    out.topology_revision = object->topology_revision;
    out.depth = static_cast<std::uint32_t>(ancestry.size() - 1);
    out.diagnostics = std::abs(world.determinant()) <= 1e-8f
        ? GraphicItemDiagnostic2D::SingularWorldTransform
        : GraphicItemDiagnostic2D::None;
    out.local_bounds = std::holds_alternative<GroupItem2D>(out.payload)
        ? subtree_local_bounds_locked_(item)
        : payload_bounds(out.payload);
    out.world_bounds = out.local_bounds
        ? std::optional<termin::Bounds2f>(
              world.transform_bounds(*out.local_bounds))
        : std::nullopt;
    out.effective_clips = std::move(clips);
    return true;
}

std::optional<GraphicItemSnapshot2D> VisualScene2D::snapshot(
    GraphicItemHandle item) const {
    std::scoped_lock lock(mutex_);
    GraphicItemSnapshot2D result;
    if (!snapshot_locked_(item, result)) {
        tc::Log::error("VisualScene2D::snapshot: item is stale or foreign");
        return std::nullopt;
    }
    return result;
}

std::vector<GraphicItemSnapshot2D> VisualScene2D::snapshots_locked_() const {
    std::vector<GraphicItemSnapshot2D> result;
    const auto handles = handles_locked_();
    result.reserve(handles.size());
    for (const auto handle : handles) {
        GraphicItemSnapshot2D snapshot;
        if (snapshot_locked_(handle, snapshot)) {
            result.push_back(std::move(snapshot));
        }
    }
    std::stable_sort(
        result.begin(),
        result.end(),
        [](const auto& a, const auto& b) {
            if (a.state.z_order != b.state.z_order) {
                return a.state.z_order < b.state.z_order;
            }
            return a.stable_order < b.stable_order;
        });
    return result;
}

std::vector<GraphicItemSnapshot2D> VisualScene2D::snapshots() const {
    std::scoped_lock lock(mutex_);
    return snapshots_locked_();
}

std::size_t VisualScene2D::size() const {
    std::scoped_lock lock(mutex_);
    return storage_.size();
}

bool VisualScene2D::contains(GraphicItemHandle item) const {
    std::scoped_lock lock(mutex_);
    return owns_locked_(item);
}

std::uint64_t VisualScene2D::revision() const {
    std::scoped_lock lock(mutex_);
    return revision_;
}

std::uint64_t VisualScene2D::id() const {
    std::scoped_lock lock(mutex_);
    return storage_.id();
}

}  // namespace termin::visual
