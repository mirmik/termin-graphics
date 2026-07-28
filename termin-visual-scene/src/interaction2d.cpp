#include "termin_visual_scene/interaction2d.hpp"

#include <algorithm>
#include <cmath>
#include <type_traits>
#include <utility>

#include <tcbase/tc_log.hpp>

namespace termin::visual {
namespace {

bool equal_handle(GraphicItemHandle a, GraphicItemHandle b) {
    return a.scene_id == b.scene_id &&
           a.index == b.index &&
           a.generation == b.generation;
}

bool valid_handle(GraphicItemHandle value) {
    return !tc_graphic_item_handle_is_invalid(value);
}

bool bounds_contains(termin::Bounds2f bounds, termin::Vec2f point) {
    return point.x >= bounds.x0 && point.x <= bounds.x1 &&
           point.y >= bounds.y0 && point.y <= bounds.y1;
}

bool rect_contains(termin::Rect2f rect, termin::Vec2f point) {
    return point.x >= rect.x && point.x <= rect.x + rect.width &&
           point.y >= rect.y && point.y <= rect.y + rect.height;
}

bool rounded_rect_contains(
    termin::Rect2f rect,
    float radius,
    termin::Vec2f point) {
    if (!rect_contains(rect, point)) return false;
    const float r = std::min(
        radius,
        std::min(rect.width, rect.height) * 0.5f);
    if (r <= 0.0f) return true;
    const float cx = std::clamp(point.x, rect.x + r, rect.x + rect.width - r);
    const float cy = std::clamp(point.y, rect.y + r, rect.y + rect.height - r);
    const float dx = point.x - cx;
    const float dy = point.y - cy;
    return dx * dx + dy * dy <= r * r;
}

bool ellipse_contains(termin::Rect2f bounds, termin::Vec2f point) {
    if (bounds.width <= 0.0f || bounds.height <= 0.0f) return false;
    const float nx =
        (point.x - (bounds.x + bounds.width * 0.5f)) /
        (bounds.width * 0.5f);
    const float ny =
        (point.y - (bounds.y + bounds.height * 0.5f)) /
        (bounds.height * 0.5f);
    return nx * nx + ny * ny <= 1.0f;
}

bool path_hit(
    const tgfx::Path2f& path,
    termin::Vec2f point,
    const std::optional<tgfx::FillPaint>& fill,
    const std::optional<tgfx::StrokePaint>& stroke) {
    const auto flat = path.flatten();
    return (fill && flat.contains(point, fill->rule)) ||
           (stroke && flat.stroke_contains(point, *stroke));
}

bool payload_hit(
    const GraphicItemPayload2D& payload,
    termin::Vec2f local_point,
    const std::optional<termin::Bounds2f>& local_bounds) {
    return std::visit(
        [&](const auto& value) {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, GroupItem2D>) {
                return false;
            } else if constexpr (std::is_same_v<T, RectItem2D>) {
                return rect_contains(value.rect, local_point);
            } else if constexpr (std::is_same_v<T, RoundedRectItem2D>) {
                return rounded_rect_contains(value.rect, value.radius, local_point);
            } else if constexpr (std::is_same_v<T, EllipseItem2D>) {
                return ellipse_contains(value.bounds, local_point);
            } else if constexpr (std::is_same_v<T, PathItem2D>) {
                return path_hit(value.path, local_point, value.fill, value.stroke);
            } else if constexpr (std::is_same_v<T, PolylineItem2D>) {
                tgfx::Path2f path;
                if (!path.move_to(value.points.front())) return false;
                for (std::size_t i = 1; i < value.points.size(); ++i) {
                    if (!path.line_to(value.points[i])) return false;
                }
                if (value.closed && !path.close()) return false;
                return path.flatten().stroke_contains(local_point, value.stroke);
            } else if constexpr (std::is_same_v<T, HitRegionItem2D>) {
                return value.path.flatten().contains(local_point, value.rule);
            } else {
                return local_bounds && bounds_contains(*local_bounds, local_point);
            }
        },
        payload);
}

bool clips_contain(
    const GraphicItemSnapshot2D& item,
    termin::Vec2f world_point) {
    for (const auto& clip : item.effective_clips) {
        if (!clip.path.flatten().contains(world_point, clip.rule)) return false;
    }
    return true;
}

const GraphicItemSnapshot2D* find_snapshot(
    const std::vector<GraphicItemSnapshot2D>& values,
    GraphicItemHandle handle) {
    const auto found = std::find_if(
        values.begin(),
        values.end(),
        [&](const auto& value) { return equal_handle(value.handle, handle); });
    return found == values.end() ? nullptr : &*found;
}

bool is_ancestor(
    const std::vector<GraphicItemSnapshot2D>& values,
    GraphicItemHandle ancestor,
    GraphicItemHandle descendant) {
    auto* cursor = find_snapshot(values, descendant);
    while (cursor && valid_handle(cursor->parent)) {
        if (equal_handle(cursor->parent, ancestor)) return true;
        cursor = find_snapshot(values, cursor->parent);
    }
    return false;
}

std::optional<GraphicItemHandle> hit_test_values(
    const std::vector<GraphicItemSnapshot2D>& values,
    termin::Vec2f world_point) {
    const GraphicItemSnapshot2D* best = nullptr;
    for (const auto& item : values) {
        if (!item.effective_visible || !item.effective_enabled ||
            item.effective_opacity <= 0.0f ||
            item.diagnostics == GraphicItemDiagnostic2D::SingularWorldTransform ||
            !clips_contain(item, world_point)) {
            continue;
        }
        termin::Affine2f inverse{};
        if (!item.world_transform.try_inverse(inverse)) continue;
        const auto local = inverse.transform_point(world_point);
        if (!payload_hit(item.payload, local, item.local_bounds)) continue;
        if (!best ||
            item.state.z_order > best->state.z_order ||
            (item.state.z_order == best->state.z_order &&
             is_ancestor(values, best->handle, item.handle)) ||
            (item.state.z_order == best->state.z_order &&
             !is_ancestor(values, item.handle, best->handle) &&
             item.stable_order > best->stable_order)) {
            best = &item;
        }
    }
    return best ? std::optional<GraphicItemHandle>(best->handle) : std::nullopt;
}

}  // namespace

std::optional<GraphicItemHandle> hit_test(
    const VisualScene2D& scene,
    termin::Vec2f world_point) {
    if (!std::isfinite(world_point.x) || !std::isfinite(world_point.y)) {
        tc::Log::error("VisualScene2D hit_test: non-finite point rejected");
        return std::nullopt;
    }
    return hit_test_values(scene.snapshots(), world_point);
}

std::size_t SceneInteraction2D::HandleHash::operator()(
    const HandleKey& value) const noexcept {
    std::size_t result = std::hash<std::uint64_t>{}(value.scene_id);
    result ^= std::hash<std::uint32_t>{}(value.index) +
              0x9e3779b9u + (result << 6u) + (result >> 2u);
    result ^= std::hash<std::uint32_t>{}(value.generation) +
              0x9e3779b9u + (result << 6u) + (result >> 2u);
    return result;
}

SceneInteraction2D::HandleKey SceneInteraction2D::key_(
    GraphicItemHandle handle) {
    return {handle.scene_id, handle.index, handle.generation};
}

GraphicItemHandle SceneInteraction2D::handle_(
    const std::unordered_map<PointerId2D, Tracked>& values,
    PointerId2D pointer) {
    const auto found = values.find(pointer);
    return found == values.end()
        ? tc_graphic_item_handle_invalid()
        : found->second.handle;
}

void SceneInteraction2D::reconcile_locked_(
    const std::vector<GraphicItemSnapshot2D>& snapshots) {
    auto reconcile = [&](auto& values) {
        for (auto it = values.begin(); it != values.end();) {
            const auto* current = find_snapshot(snapshots, it->second.handle);
            if (!current || !current->effective_enabled ||
                current->topology_revision != it->second.topology_revision) {
                it = values.erase(it);
            } else {
                ++it;
            }
        }
    };
    reconcile(hovered_);
    reconcile(pressed_);
    reconcile(captured_);
    for (auto it = action_handlers_.begin(); it != action_handlers_.end();) {
        GraphicItemHandle handle{
            it->first.scene_id,
            it->first.index,
            it->first.generation,
        };
        if (!find_snapshot(snapshots, handle)) {
            it = action_handlers_.erase(it);
        } else {
            ++it;
        }
    }
}

PointerDispatch2D SceneInteraction2D::route(
    const VisualScene2D& scene,
    const PointerEvent2D& event) {
    PointerDispatch2D result;
    result.event = event;
    const auto snapshots = scene.snapshots();
    const auto hit = hit_test_values(snapshots, event.position);
    result.hit_target = hit.value_or(tc_graphic_item_handle_invalid());

    ActionHandler action_handler;
    FallbackHandler fallback_handler;
    {
        std::scoped_lock lock(mutex_);
        reconcile_locked_(snapshots);
        if (hit) {
            const auto* value = find_snapshot(snapshots, *hit);
            hovered_[event.pointer] = {*hit, value->topology_revision};
        } else {
            hovered_.erase(event.pointer);
        }

        if (event.kind == PointerEventKind2D::Down) {
            if (hit) {
                const auto* snapshot = find_snapshot(snapshots, *hit);
                const Tracked value{*hit, snapshot->topology_revision};
                pressed_[event.pointer] = value;
                captured_[event.pointer] = value;
                result.target = *hit;
            }
        } else if (event.kind == PointerEventKind2D::Move) {
            result.target = handle_(captured_, event.pointer);
            if (!valid_handle(result.target)) result.target = result.hit_target;
        } else if (event.kind == PointerEventKind2D::Up) {
            result.target = handle_(captured_, event.pointer);
            if (!valid_handle(result.target)) result.target = result.hit_target;
            const auto pressed_target = handle_(pressed_, event.pointer);
            if (valid_handle(pressed_target) && hit &&
                equal_handle(pressed_target, *hit)) {
                result.action = ActionEvent2D{pressed_target, event.pointer, "activate"};
                const auto handler = action_handlers_.find(key_(pressed_target));
                if (handler != action_handlers_.end()) action_handler = handler->second;
            }
            pressed_.erase(event.pointer);
            captured_.erase(event.pointer);
        } else {
            result.target = handle_(captured_, event.pointer);
            if (!valid_handle(result.target)) {
                result.target = handle_(pressed_, event.pointer);
            }
            hovered_.erase(event.pointer);
            pressed_.erase(event.pointer);
            captured_.erase(event.pointer);
        }

        result.hovered = handle_(hovered_, event.pointer);
        result.pressed = handle_(pressed_, event.pointer);
        result.captured = handle_(captured_, event.pointer);
        result.used_fallback = !valid_handle(result.target);
        if (result.used_fallback) fallback_handler = fallback_handler_;
    }
    if (result.action && action_handler) action_handler(*result.action);
    if (result.used_fallback && fallback_handler) fallback_handler(event);
    return result;
}

bool SceneInteraction2D::capture(
    const VisualScene2D& scene,
    PointerId2D pointer,
    GraphicItemHandle target) {
    const auto snapshots = scene.snapshots();
    const auto* value = find_snapshot(snapshots, target);
    if (!value || !value->effective_enabled) return false;
    std::scoped_lock lock(mutex_);
    captured_[pointer] = {target, value->topology_revision};
    return true;
}

void SceneInteraction2D::release(PointerId2D pointer) {
    std::scoped_lock lock(mutex_);
    captured_.erase(pointer);
}

void SceneInteraction2D::cancel_all() {
    std::scoped_lock lock(mutex_);
    hovered_.clear();
    pressed_.clear();
    captured_.clear();
}

void SceneInteraction2D::set_action_handler(
    GraphicItemHandle item,
    ActionHandler handler) {
    std::scoped_lock lock(mutex_);
    if (handler) {
        action_handlers_[key_(item)] = std::move(handler);
    } else {
        action_handlers_.erase(key_(item));
    }
}

void SceneInteraction2D::clear_action_handler(GraphicItemHandle item) {
    std::scoped_lock lock(mutex_);
    action_handlers_.erase(key_(item));
}

void SceneInteraction2D::set_fallback_handler(FallbackHandler handler) {
    std::scoped_lock lock(mutex_);
    fallback_handler_ = std::move(handler);
}

GraphicItemHandle SceneInteraction2D::hovered(PointerId2D pointer) const {
    std::scoped_lock lock(mutex_);
    return handle_(hovered_, pointer);
}

GraphicItemHandle SceneInteraction2D::pressed(PointerId2D pointer) const {
    std::scoped_lock lock(mutex_);
    return handle_(pressed_, pointer);
}

GraphicItemHandle SceneInteraction2D::captured(PointerId2D pointer) const {
    std::scoped_lock lock(mutex_);
    return handle_(captured_, pointer);
}

void SelectionController2D::handle(
    const VisualScene2D& scene,
    const PointerDispatch2D& dispatch) {
    if (dispatch.event.kind != PointerEventKind2D::Down) return;
    selection_.clear();
    if (!valid_handle(dispatch.target)) return;
    const auto item = scene.snapshot(dispatch.target);
    if (item) {
        selection_.push_back({dispatch.target, item->topology_revision});
    }
}

void SelectionController2D::clear() {
    selection_.clear();
}

bool SelectionController2D::select(
    const VisualScene2D& scene,
    GraphicItemHandle item) {
    selection_.clear();
    if (!valid_handle(item)) return true;
    const auto snapshot = scene.snapshot(item);
    if (!snapshot || !snapshot->effective_enabled) return false;
    selection_.push_back({item, snapshot->topology_revision});
    return true;
}

bool SelectionController2D::toggle(
    const VisualScene2D& scene,
    GraphicItemHandle item) {
    reconcile(scene);
    if (!valid_handle(item)) return true;
    const auto found = std::find_if(
        selection_.begin(),
        selection_.end(),
        [&](const Entry& entry) {
            return equal_handle(entry.handle, item);
        });
    if (found != selection_.end()) {
        selection_.erase(found);
        return true;
    }
    const auto snapshot = scene.snapshot(item);
    if (!snapshot || !snapshot->effective_enabled) return false;
    selection_.push_back({item, snapshot->topology_revision});
    return true;
}

void SelectionController2D::reconcile(const VisualScene2D& scene) {
    const auto snapshots = scene.snapshots();
    std::erase_if(selection_, [&](const Entry& value) {
        const auto* item = find_snapshot(snapshots, value.handle);
        return !item || !item->effective_enabled ||
               item->topology_revision != value.topology_revision;
    });
}

std::vector<GraphicItemHandle> SelectionController2D::selection() const {
    std::vector<GraphicItemHandle> result;
    result.reserve(selection_.size());
    for (const auto& entry : selection_) result.push_back(entry.handle);
    return result;
}

bool DragController2D::handle(
    VisualScene2D& scene,
    const PointerDispatch2D& dispatch) {
    if (dispatch.event.kind == PointerEventKind2D::Down &&
        valid_handle(dispatch.target)) {
        const auto item = scene.snapshot(dispatch.target);
        if (!item) return false;
        termin::Affine2f parent_world = termin::Affine2f::identity();
        if (valid_handle(item->parent)) {
            const auto parent = scene.snapshot(item->parent);
            if (!parent) return false;
            parent_world = parent->world_transform;
        }
        if (!parent_world.try_inverse(world_to_parent_)) return false;
        target_ = dispatch.target;
        pointer_ = dispatch.event.pointer;
        start_world_ = dispatch.event.position;
        start_state_ = item->state;
        return true;
    }
    if (!valid_handle(target_) || dispatch.event.pointer != pointer_) return false;
    if (dispatch.event.kind == PointerEventKind2D::Move) {
        if (!equal_handle(dispatch.target, target_)) {
            cancel();
            return false;
        }
        const auto world_delta = termin::Vec2f{
            dispatch.event.position.x - start_world_.x,
            dispatch.event.position.y - start_world_.y,
        };
        const auto parent_delta = world_to_parent_.transform_vector(world_delta);
        auto moved = start_state_;
        moved.local_transform =
            termin::Affine2f::translation(parent_delta) *
            start_state_.local_transform;
        if (!scene.set_state(target_, moved)) {
            cancel();
            return false;
        }
        return true;
    }
    if (dispatch.event.kind == PointerEventKind2D::Up ||
        dispatch.event.kind == PointerEventKind2D::Cancel) {
        cancel();
        return true;
    }
    return false;
}

void DragController2D::cancel() noexcept {
    target_ = tc_graphic_item_handle_invalid();
    pointer_ = 0;
}

}  // namespace termin::visual
