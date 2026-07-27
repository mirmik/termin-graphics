#include <termin/gui_native/graphics_scene.hpp>

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

#include <tcbase/tc_log.h>
#include <termin_visual_scene/interaction2d.hpp>

namespace termin::gui_native {
namespace {

bool same_handle(
    termin::visual::GraphicItemHandle left,
    termin::visual::GraphicItemHandle right) {
    return left.scene_id == right.scene_id &&
           left.index == right.index &&
           left.generation == right.generation;
}

tc_ui_rect rect_from_bounds(const termin::Bounds2f& bounds) {
    return {
        bounds.x0,
        bounds.y0,
        bounds.x1 - bounds.x0,
        bounds.y1 - bounds.y0,
    };
}

} // namespace

tc_ui_point SceneTransform::world_to_screen(tc_ui_point point) const {
    return {origin_x + point.x * zoom, origin_y + point.y * zoom};
}

tc_ui_point SceneTransform::screen_to_world(tc_ui_point point) const {
    return {(point.x - origin_x) / zoom, (point.y - origin_y) / zoom};
}

std::size_t GraphicsScene::HandleHash::operator()(
    const HandleKey& value) const noexcept {
    std::size_t seed = std::hash<std::uint64_t>{}(value.scene_id);
    seed ^= std::hash<std::uint32_t>{}(value.index) +
            0x9e3779b9u + (seed << 6u) + (seed >> 2u);
    seed ^= std::hash<std::uint32_t>{}(value.generation) +
            0x9e3779b9u + (seed << 6u) + (seed >> 2u);
    return seed;
}

GraphicsScene::HandleKey GraphicsScene::key_(
    termin::visual::GraphicItemHandle handle) {
    return {handle.scene_id, handle.index, handle.generation};
}

std::optional<GraphicsScene::Metadata> GraphicsScene::metadata_(
    termin::visual::GraphicItemHandle handle) const {
    std::lock_guard lock(metadata_mutex_);
    const auto found = metadata_by_handle_.find(key_(handle));
    if (found == metadata_by_handle_.end()) return std::nullopt;
    return found->second;
}

bool GraphicsScene::update_metadata_(
    termin::visual::GraphicItemHandle handle,
    const Metadata& metadata) {
    if (!scene_.snapshot(handle)) {
        tc_log_error(
            "[termin-gui-native] rejected metadata update for stale graphic item");
        return false;
    }
    {
        std::lock_guard lock(metadata_mutex_);
        const auto found = metadata_by_handle_.find(key_(handle));
        if (found == metadata_by_handle_.end()) {
            tc_log_error(
                "[termin-gui-native] graphic item metadata is missing");
            return false;
        }
        found->second = metadata;
    }
    notify_changed_();
    return true;
}

void GraphicsScene::notify_changed_() {
    changed_.emit(*this);
}

GraphicItemRef GraphicsScene::create_(
    std::string stable_id,
    termin::visual::GraphicItemPayload2D payload,
    const std::optional<GraphicItemRef>& parent) {
    termin::visual::GraphicItemHandle parent_handle =
        tc_graphic_item_handle_invalid();
    if (parent) {
        if (parent->scene_ != this || !parent->valid()) {
            tc_log_error(
                "[termin-gui-native] rejected graphic item with foreign or stale parent");
            return {};
        }
        parent_handle = parent->handle_;
    }
    const auto created = scene_.create(std::move(payload), parent_handle);
    if (!created) {
        tc_log_error("[termin-gui-native] failed to create visual scene item");
        return {};
    }
    {
        std::lock_guard lock(metadata_mutex_);
        metadata_by_handle_.emplace(
            key_(*created),
            Metadata{std::move(stable_id)});
    }
    notify_changed_();
    return {this, lifetime_token_, *created};
}

GraphicItemRef GraphicsScene::create_group(
    std::string stable_id,
    std::optional<GraphicItemRef> parent) {
    return create_(
        std::move(stable_id),
        termin::visual::GroupItem2D{},
        parent);
}

GraphicItemRef GraphicsScene::create_rect(
    std::string stable_id,
    termin::Rect2f rect,
    tgfx::FillPaint fill,
    std::optional<tgfx::StrokePaint> stroke,
    std::optional<GraphicItemRef> parent) {
    return create_(
        std::move(stable_id),
        termin::visual::RectItem2D{rect, fill, std::move(stroke)},
        parent);
}

GraphicItemRef GraphicsScene::create_rounded_rect(
    std::string stable_id,
    termin::Rect2f rect,
    float radius,
    tgfx::FillPaint fill,
    std::optional<tgfx::StrokePaint> stroke,
    std::optional<GraphicItemRef> parent) {
    return create_(
        std::move(stable_id),
        termin::visual::RoundedRectItem2D{
            rect, radius, fill, std::move(stroke)},
        parent);
}

GraphicItemRef GraphicsScene::create_ellipse(
    std::string stable_id,
    termin::Rect2f bounds,
    tgfx::FillPaint fill,
    std::optional<tgfx::StrokePaint> stroke,
    std::optional<GraphicItemRef> parent) {
    return create_(
        std::move(stable_id),
        termin::visual::EllipseItem2D{
            bounds, fill, std::move(stroke)},
        parent);
}

GraphicItemRef GraphicsScene::create_path(
    std::string stable_id,
    tgfx::Path2f path,
    std::optional<tgfx::FillPaint> fill,
    std::optional<tgfx::StrokePaint> stroke,
    std::optional<GraphicItemRef> parent) {
    return create_(
        std::move(stable_id),
        termin::visual::PathItem2D{
            std::move(path), std::move(fill), std::move(stroke)},
        parent);
}

GraphicItemRef GraphicsScene::create_polyline(
    std::string stable_id,
    std::vector<termin::Vec2f> points,
    tgfx::StrokePaint stroke,
    bool closed,
    std::optional<GraphicItemRef> parent) {
    return create_(
        std::move(stable_id),
        termin::visual::PolylineItem2D{
            std::move(points), std::move(stroke), closed},
        parent);
}

GraphicItemRef GraphicsScene::create_text(
    std::string stable_id,
    std::string text,
    termin::Vec2f origin,
    float size_px,
    tgfx::Color4f color,
    termin::Bounds2f layout_bounds,
    std::optional<GraphicItemRef> parent) {
    return create_(
        std::move(stable_id),
        termin::visual::TextItem2D{
            std::move(text),
            termin::visual::StableResourceRef2D{"ui://default-font"},
            origin,
            size_px,
            color,
            tgfx::TextAnchor2D::Left,
            layout_bounds,
        },
        parent);
}

GraphicItemRef GraphicsScene::create_hit_region(
    std::string stable_id,
    tgfx::Path2f path,
    std::optional<GraphicItemRef> parent) {
    return create_(
        std::move(stable_id),
        termin::visual::HitRegionItem2D{std::move(path)},
        parent);
}

bool GraphicsScene::destroy(const GraphicItemRef& item) {
    if (item.scene_ != this || !item.valid()) {
        tc_log_error(
            "[termin-gui-native] rejected destruction of foreign or stale graphic item");
        return false;
    }
    std::vector<HandleKey> removed;
    const auto snapshots = scene_.snapshots();
    const auto root = scene_.snapshot(item.handle_);
    if (!root) return false;
    for (const auto& snapshot : snapshots) {
        auto current = snapshot;
        while (true) {
            if (same_handle(current.handle, item.handle_)) {
                removed.push_back(key_(snapshot.handle));
                break;
            }
            if (tc_graphic_item_handle_is_invalid(current.parent)) break;
            const auto next = scene_.snapshot(current.parent);
            if (!next) break;
            current = *next;
        }
    }
    if (!scene_.destroy_subtree(item.handle_)) return false;
    {
        std::lock_guard lock(metadata_mutex_);
        for (const auto& key : removed) metadata_by_handle_.erase(key);
    }
    notify_changed_();
    return true;
}

void GraphicsScene::clear() {
    scene_.clear();
    {
        std::lock_guard lock(metadata_mutex_);
        metadata_by_handle_.clear();
    }
    notify_changed_();
}

std::optional<GraphicItemRef> GraphicsScene::item(
    termin::visual::GraphicItemHandle handle) {
    if (!scene_.snapshot(handle)) return std::nullopt;
    return GraphicItemRef{this, lifetime_token_, handle};
}

std::vector<GraphicItemRef> GraphicsScene::items() {
    std::vector<GraphicItemRef> result;
    const auto snapshots = scene_.snapshots();
    result.reserve(snapshots.size());
    for (const auto& snapshot : snapshots) {
        result.push_back(
            GraphicItemRef{this, lifetime_token_, snapshot.handle});
    }
    return result;
}

std::optional<GraphicItemRef> GraphicsScene::hit_test(
    float world_x,
    float world_y) {
    const auto hit = termin::visual::hit_test(scene_, {world_x, world_y});
    if (!hit) return std::nullopt;
    return GraphicItemRef{this, lifetime_token_, *hit};
}

bool GraphicItemRef::valid() const {
    return scene_ && !lifetime_.expired() &&
           scene_->scene_.snapshot(handle_).has_value();
}

std::string GraphicItemRef::stable_id() const {
    if (!scene_ || lifetime_.expired()) return {};
    const auto metadata = scene_->metadata_(handle_);
    return metadata ? metadata->stable_id : std::string{};
}

bool GraphicItemRef::set_stable_id(std::string stable_id) {
    if (!scene_ || lifetime_.expired()) return false;
    auto metadata = scene_->metadata_(handle_);
    if (!metadata) return false;
    metadata->stable_id = std::move(stable_id);
    return scene_->update_metadata_(handle_, *metadata);
}

std::optional<GraphicItemRef> GraphicItemRef::parent() const {
    if (!scene_ || lifetime_.expired()) return std::nullopt;
    const auto snapshot = scene_->scene_.snapshot(handle_);
    if (!snapshot || tc_graphic_item_handle_is_invalid(snapshot->parent)) {
        return std::nullopt;
    }
    return GraphicItemRef{scene_, lifetime_, snapshot->parent};
}

std::vector<GraphicItemRef> GraphicItemRef::children() const {
    std::vector<GraphicItemRef> result;
    if (!scene_ || lifetime_.expired()) return result;
    for (const auto& snapshot : scene_->scene_.snapshots()) {
        if (same_handle(snapshot.parent, handle_)) {
            result.push_back(
                GraphicItemRef{scene_, lifetime_, snapshot.handle});
        }
    }
    return result;
}

bool GraphicItemRef::reparent(
    const std::optional<GraphicItemRef>& parent_ref) {
    if (!scene_ || lifetime_.expired()) return false;
    termin::visual::GraphicItemHandle parent_handle =
        tc_graphic_item_handle_invalid();
    if (parent_ref) {
        if (parent_ref->scene_ != scene_ || !parent_ref->valid()) return false;
        parent_handle = parent_ref->handle_;
    }
    const bool changed = tc_graphic_item_handle_is_invalid(parent_handle)
        ? scene_->scene_.detach(handle_)
        : scene_->scene_.reparent(handle_, parent_handle);
    if (changed) scene_->notify_changed_();
    return changed;
}

tc_ui_point GraphicItemRef::position() const {
    if (!scene_ || lifetime_.expired()) return {};
    const auto snapshot = scene_->scene_.snapshot(handle_);
    if (!snapshot) return {};
    return {snapshot->state.local_transform.tx,
            snapshot->state.local_transform.ty};
}

bool GraphicItemRef::set_position(tc_ui_point position_value) {
    if (!scene_ || lifetime_.expired() ||
        !std::isfinite(position_value.x) ||
        !std::isfinite(position_value.y)) {
        tc_log_error(
            "[termin-gui-native] rejected invalid graphic item position");
        return false;
    }
    const auto snapshot = scene_->scene_.snapshot(handle_);
    if (!snapshot) return false;
    auto state = snapshot->state;
    state.local_transform.tx = position_value.x;
    state.local_transform.ty = position_value.y;
    if (!scene_->scene_.set_state(handle_, state)) return false;
    scene_->notify_changed_();
    return true;
}

tc_ui_size GraphicItemRef::size() const {
    if (!scene_ || lifetime_.expired()) return {};
    const auto snapshot = scene_->scene_.snapshot(handle_);
    if (!snapshot || !snapshot->local_bounds) return {};
    return {
        snapshot->local_bounds->x1 - snapshot->local_bounds->x0,
        snapshot->local_bounds->y1 - snapshot->local_bounds->y0,
    };
}

bool GraphicItemRef::set_size(tc_ui_size size_value) {
    if (!scene_ || lifetime_.expired() ||
        !std::isfinite(size_value.width) ||
        !std::isfinite(size_value.height) ||
        size_value.width < 0.0f || size_value.height < 0.0f) {
        tc_log_error("[termin-gui-native] rejected invalid graphic item size");
        return false;
    }
    const auto snapshot = scene_->scene_.snapshot(handle_);
    if (!snapshot) return false;
    auto payload = snapshot->payload;
    const auto resize = [&](termin::Rect2f& rect) {
        rect.width = size_value.width;
        rect.height = size_value.height;
    };
    bool supported = true;
    std::visit(
        [&](auto& value) {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, termin::visual::RectItem2D> ||
                          std::is_same_v<T, termin::visual::RoundedRectItem2D>) {
                resize(value.rect);
            } else if constexpr (
                std::is_same_v<T, termin::visual::EllipseItem2D>) {
                resize(value.bounds);
            } else {
                supported = false;
            }
        },
        payload);
    if (!supported) {
        tc_log_error(
            "[termin-gui-native] item payload does not support size mutation");
        return false;
    }
    if (!scene_->scene_.set_payload(handle_, std::move(payload))) return false;
    scene_->notify_changed_();
    return true;
}

std::int64_t GraphicItemRef::z_order() const {
    if (!scene_ || lifetime_.expired()) return 0;
    const auto snapshot = scene_->scene_.snapshot(handle_);
    return snapshot ? snapshot->state.z_order : 0;
}

bool GraphicItemRef::set_z_order(std::int64_t z_order_value) {
    if (!scene_ || lifetime_.expired()) return false;
    const auto snapshot = scene_->scene_.snapshot(handle_);
    if (!snapshot) return false;
    auto state = snapshot->state;
    state.z_order = z_order_value;
    if (!scene_->scene_.set_state(handle_, state)) return false;
    scene_->notify_changed_();
    return true;
}

bool GraphicItemRef::visible() const {
    if (!scene_ || lifetime_.expired()) return false;
    const auto snapshot = scene_->scene_.snapshot(handle_);
    return snapshot && snapshot->state.visible;
}

bool GraphicItemRef::set_visible(bool visible_value) {
    if (!scene_ || lifetime_.expired()) return false;
    const auto snapshot = scene_->scene_.snapshot(handle_);
    if (!snapshot) return false;
    auto state = snapshot->state;
    state.visible = visible_value;
    if (!scene_->scene_.set_state(handle_, state)) return false;
    scene_->notify_changed_();
    return true;
}

bool GraphicItemRef::enabled() const {
    if (!scene_ || lifetime_.expired()) return false;
    const auto snapshot = scene_->scene_.snapshot(handle_);
    return snapshot && snapshot->state.enabled;
}

bool GraphicItemRef::set_enabled(bool enabled_value) {
    if (!scene_ || lifetime_.expired()) return false;
    const auto snapshot = scene_->scene_.snapshot(handle_);
    if (!snapshot) return false;
    auto state = snapshot->state;
    state.enabled = enabled_value;
    if (!scene_->scene_.set_state(handle_, state)) return false;
    scene_->notify_changed_();
    return true;
}

bool GraphicItemRef::selectable() const {
    if (!scene_ || lifetime_.expired()) return false;
    const auto metadata = scene_->metadata_(handle_);
    return metadata && metadata->selectable;
}

bool GraphicItemRef::set_selectable(bool selectable_value) {
    if (!scene_ || lifetime_.expired()) return false;
    auto metadata = scene_->metadata_(handle_);
    if (!metadata) return false;
    metadata->selectable = selectable_value;
    return scene_->update_metadata_(handle_, *metadata);
}

bool GraphicItemRef::draggable() const {
    if (!scene_ || lifetime_.expired()) return false;
    const auto metadata = scene_->metadata_(handle_);
    return metadata && metadata->draggable;
}

bool GraphicItemRef::set_draggable(bool draggable_value) {
    if (!scene_ || lifetime_.expired()) return false;
    auto metadata = scene_->metadata_(handle_);
    if (!metadata) return false;
    metadata->draggable = draggable_value;
    return scene_->update_metadata_(handle_, *metadata);
}

tc_ui_point GraphicItemRef::world_position() const {
    if (!scene_ || lifetime_.expired()) return {};
    const auto snapshot = scene_->scene_.snapshot(handle_);
    if (!snapshot) return {};
    const auto point = snapshot->world_transform.transform_point({0.0f, 0.0f});
    return {point.x, point.y};
}

tc_ui_rect GraphicItemRef::world_bounds() const {
    if (!scene_ || lifetime_.expired()) return {};
    const auto snapshot = scene_->scene_.snapshot(handle_);
    if (!snapshot || !snapshot->world_bounds) return {};
    return rect_from_bounds(*snapshot->world_bounds);
}

bool GraphicItemRef::set_polyline(
    std::vector<termin::Vec2f> points,
    tgfx::StrokePaint stroke,
    bool closed) {
    if (!scene_ || lifetime_.expired()) return false;
    const auto snapshot = scene_->scene_.snapshot(handle_);
    if (!snapshot ||
        !std::holds_alternative<termin::visual::PolylineItem2D>(
            snapshot->payload)) {
        tc_log_error(
            "[termin-gui-native] set_polyline requires a polyline item");
        return false;
    }
    if (!scene_->scene_.set_payload(
            handle_,
            termin::visual::PolylineItem2D{
                std::move(points), std::move(stroke), closed})) {
        return false;
    }
    scene_->notify_changed_();
    return true;
}

} // namespace termin::gui_native
