#include "termin_visual_scene/scene2d.hpp"

#include <algorithm>
#include <cmath>
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

bool same_handle(GraphicItemHandle a, GraphicItemHandle b) {
    return a.scene_id == b.scene_id &&
           a.index == b.index &&
           a.generation == b.generation;
}

}  // namespace

bool VisualScene2D::owns_locked_(GraphicItemHandle item) const {
    if (tc_graphic_item_handle_is_invalid(item) || item.scene_id != storage_.id()) {
        return false;
    }
    const auto found = records_.find(item.index);
    if (found == records_.end() || !same_handle(found->second.handle, item)) {
        return false;
    }
    GraphicItemView view{};
    return storage_.resolve(item, view);
}

VisualScene2D::Record* VisualScene2D::record_locked_(GraphicItemHandle item) {
    if (!owns_locked_(item)) return nullptr;
    return &records_.find(item.index)->second;
}

const VisualScene2D::Record* VisualScene2D::record_locked_(
    GraphicItemHandle item) const {
    if (!owns_locked_(item)) return nullptr;
    return &records_.find(item.index)->second;
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
    GraphicItemHandle handle{};
    try {
        handle = storage_.create(parent);
        records_.emplace(
            handle.index,
            Record{
                handle,
                GraphicItemState2D{},
                std::move(payload),
                next_stable_order_++,
                ++revision_,
                revision_,
            });
    } catch (const std::exception& error) {
        if (!tc_graphic_item_handle_is_invalid(handle)) {
            storage_.destroy_leaf(handle);
        }
        tc::Log::error("VisualScene2D::create: %s", error.what());
        return std::nullopt;
    }
    return handle;
}

bool VisualScene2D::destroy_leaf(GraphicItemHandle item) {
    std::scoped_lock lock(mutex_);
    if (!owns_locked_(item)) {
        tc::Log::error("VisualScene2D::destroy_leaf: item is stale or foreign");
        return false;
    }
    if (!storage_.destroy_leaf(item)) return false;
    records_.erase(item.index);
    ++revision_;
    return true;
}

void VisualScene2D::collect_subtree_locked_(
    GraphicItemHandle root,
    std::vector<GraphicItemHandle>& out) const {
    out.push_back(root);
    GraphicItemView view{};
    if (!storage_.resolve(root, view)) return;
    auto child = view.first_child;
    while (!tc_graphic_item_handle_is_invalid(child)) {
        GraphicItemView child_view{};
        if (!storage_.resolve(child, child_view)) return;
        collect_subtree_locked_(child, out);
        child = child_view.next_sibling;
    }
}

std::optional<termin::Bounds2f> VisualScene2D::subtree_local_bounds_locked_(
    GraphicItemHandle root) const {
    const auto* record = record_locked_(root);
    if (!record) return std::nullopt;
    auto result = payload_bounds(record->payload);

    GraphicItemView view{};
    if (!storage_.resolve(root, view)) return result;
    auto child = view.first_child;
    while (!tc_graphic_item_handle_is_invalid(child)) {
        GraphicItemView child_view{};
        const auto* child_record = record_locked_(child);
        if (!child_record || !storage_.resolve(child, child_view)) return result;
        const auto child_bounds = subtree_local_bounds_locked_(child);
        if (child_bounds) {
            const auto transformed =
                child_record->state.local_transform.transform_bounds(*child_bounds);
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
    std::vector<GraphicItemHandle> removed;
    collect_subtree_locked_(item, removed);
    if (!storage_.destroy_subtree(item)) return false;
    for (const auto handle : removed) records_.erase(handle.index);
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
    auto* record = record_locked_(item);
    record->revision = ++revision_;
    record->topology_revision = revision_;
    return true;
}

bool VisualScene2D::detach(GraphicItemHandle item) {
    std::scoped_lock lock(mutex_);
    if (!owns_locked_(item)) {
        tc::Log::error("VisualScene2D::detach: item is stale or foreign");
        return false;
    }
    if (!storage_.detach(item)) return false;
    auto* record = record_locked_(item);
    record->revision = ++revision_;
    record->topology_revision = revision_;
    return true;
}

void VisualScene2D::clear() {
    std::scoped_lock lock(mutex_);
    if (records_.empty()) return;
    storage_.clear();
    records_.clear();
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
    auto* record = record_locked_(item);
    if (!record) {
        tc::Log::error("VisualScene2D::set_state: item is stale or foreign");
        return false;
    }
    record->state = std::move(state);
    record->revision = ++revision_;
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
    auto* record = record_locked_(item);
    if (!record) {
        tc::Log::error("VisualScene2D::set_payload: item is stale or foreign");
        return false;
    }
    record->payload = std::move(payload);
    record->revision = ++revision_;
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
    auto* record = record_locked_(item);
    if (!record) {
        tc::Log::error("VisualScene2D::set_item: item is stale or foreign");
        return false;
    }
    record->state = std::move(state);
    record->payload = std::move(payload);
    record->revision = ++revision_;
    return true;
}

bool VisualScene2D::snapshot_locked_(
    GraphicItemHandle item,
    GraphicItemSnapshot2D& out) const {
    const auto* record = record_locked_(item);
    if (!record) return false;

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
        const auto* ancestor_record = record_locked_(ancestor);
        if (!ancestor_record) return false;
        world = world * ancestor_record->state.local_transform;
        visible = visible && ancestor_record->state.visible;
        enabled = enabled && ancestor_record->state.enabled;
        opacity *= ancestor_record->state.opacity;
        if (ancestor_record->state.clip) {
            clips.push_back(transformed_clip(*ancestor_record->state.clip, world));
        }
    }

    GraphicItemView topology{};
    if (!storage_.resolve(item, topology)) return false;
    out.handle = item;
    out.parent = topology.parent;
    out.state = record->state;
    out.payload = record->payload;
    out.world_transform = world;
    out.effective_visible = visible;
    out.effective_enabled = enabled;
    out.effective_opacity = opacity;
    out.stable_order = record->stable_order;
    out.revision = record->revision;
    out.topology_revision = record->topology_revision;
    out.depth = static_cast<std::uint32_t>(ancestry.size() - 1);
    out.diagnostics = std::abs(world.determinant()) <= 1e-8f
        ? GraphicItemDiagnostic2D::SingularWorldTransform
        : GraphicItemDiagnostic2D::None;
    out.local_bounds = std::holds_alternative<GroupItem2D>(record->payload)
        ? subtree_local_bounds_locked_(item)
        : payload_bounds(record->payload);
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
    result.reserve(records_.size());
    for (const auto& [index, record] : records_) {
        (void)index;
        GraphicItemSnapshot2D snapshot;
        if (snapshot_locked_(record.handle, snapshot)) {
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
    return records_.size();
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
