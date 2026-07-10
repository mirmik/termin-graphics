#include <termin/gui_native/graphics_scene.hpp>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <stdexcept>

#include <tcbase/tc_log.h>

namespace termin::gui_native {

namespace {

std::atomic<uint64_t> g_next_graphics_item_id{1};

bool finite_point(tc_ui_point point) { return std::isfinite(point.x) && std::isfinite(point.y); }

bool finite_size(tc_ui_size size) {
    return std::isfinite(size.width) && std::isfinite(size.height) && size.width >= 0.0f &&
           size.height >= 0.0f;
}

template <typename T>
std::vector<std::shared_ptr<T>> sorted_by_z(const std::vector<std::shared_ptr<T>>& items,
                                            bool reverse) {
    std::vector<std::shared_ptr<T>> sorted = items;
    std::stable_sort(sorted.begin(), sorted.end(), [reverse](const auto& left, const auto& right) {
        return reverse ? left->z_index() > right->z_index() : left->z_index() < right->z_index();
    });
    return sorted;
}

} // namespace

tc_ui_point SceneTransform::world_to_screen(tc_ui_point point) const {
    return {origin_x + point.x * zoom, origin_y + point.y * zoom};
}

tc_ui_point SceneTransform::screen_to_world(tc_ui_point point) const {
    if (!std::isfinite(zoom) || zoom <= 0.0f) {
        tc_log_error("[termin-gui-native] SceneTransform cannot invert invalid zoom");
        return {};
    }
    return {(point.x - origin_x) / zoom, (point.y - origin_y) / zoom};
}

GraphicsItem::GraphicsItem(std::string stable_id)
    : id_(g_next_graphics_item_id.fetch_add(1, std::memory_order_relaxed)),
      stable_id_(std::move(stable_id)) {
    if (id_ == 0) {
        tc_log_error("[termin-gui-native] graphics item id space exhausted");
        throw std::overflow_error("graphics item id space exhausted");
    }
}

GraphicsItem::~GraphicsItem() {
    for (const auto& child : children_) {
        if (child) {
            child->parent_.reset();
            child->set_scene_recursive(nullptr);
        }
    }
}

void GraphicsItem::set_stable_id(std::string stable_id) {
    if (stable_id_ == stable_id)
        return;
    stable_id_ = std::move(stable_id);
    notify_changed();
}

bool GraphicsItem::is_ancestor_of(const GraphicsItem* item) const {
    const GraphicsItem* current = item;
    size_t depth = 0;
    while (current && depth++ < 1'000'000) {
        if (current == this)
            return true;
        const auto parent = current->parent_.lock();
        current = parent.get();
    }
    return false;
}

bool GraphicsItem::add_child(std::shared_ptr<GraphicsItem> child) {
    if (!child || child.get() == this || child->parent_.lock() || child->scene_ ||
        child->is_ancestor_of(this)) {
        tc_log_error("[termin-gui-native] GraphicsItem rejected invalid child ownership");
        return false;
    }
    child->parent_ = shared_from_this();
    child->set_scene_recursive(scene_);
    children_.push_back(std::move(child));
    notify_changed();
    return true;
}

bool GraphicsItem::remove_child(const std::shared_ptr<GraphicsItem>& child) {
    const auto found = std::find(children_.begin(), children_.end(), child);
    if (found == children_.end())
        return false;
    GraphicsScene* scene = scene_;
    if (scene)
        scene->clear_selected_recursive(*found);
    (*found)->parent_.reset();
    (*found)->set_scene_recursive(nullptr);
    children_.erase(found);
    notify_changed();
    if (scene)
        scene->notify_selection_changed();
    return true;
}

void GraphicsItem::clear_children() {
    if (children_.empty())
        return;
    GraphicsScene* scene = scene_;
    for (const auto& child : children_) {
        if (scene)
            scene->clear_selected_recursive(child);
        child->parent_.reset();
        child->set_scene_recursive(nullptr);
    }
    children_.clear();
    notify_changed();
    if (scene)
        scene->notify_selection_changed();
}

void GraphicsItem::set_position(tc_ui_point position) {
    if (!finite_point(position)) {
        tc_log_error("[termin-gui-native] GraphicsItem rejected non-finite position");
        throw std::invalid_argument("graphics item position must be finite");
    }
    if (position_.x == position.x && position_.y == position.y)
        return;
    position_ = position;
    notify_changed();
}

void GraphicsItem::set_size(tc_ui_size size) {
    if (!finite_size(size)) {
        tc_log_error("[termin-gui-native] GraphicsItem rejected invalid size");
        throw std::invalid_argument("graphics item size must be finite and non-negative");
    }
    if (size_.width == size.width && size_.height == size.height)
        return;
    size_ = size;
    notify_changed();
}

void GraphicsItem::set_z_index(float z_index) {
    if (!std::isfinite(z_index)) {
        tc_log_error("[termin-gui-native] GraphicsItem rejected non-finite z index");
        throw std::invalid_argument("graphics item z index must be finite");
    }
    if (z_index_ == z_index)
        return;
    z_index_ = z_index;
    notify_changed();
}

void GraphicsItem::set_visible(bool visible) {
    if (visible_ == visible)
        return;
    visible_ = visible;
    notify_changed();
}

void GraphicsItem::set_enabled(bool enabled) {
    if (enabled_ == enabled)
        return;
    enabled_ = enabled;
    notify_changed();
}

void GraphicsItem::set_selectable(bool selectable) {
    if (selectable_ == selectable)
        return;
    if (!selectable && selected_ && scene_)
        scene_->toggle_selected(shared_from_this());
    selectable_ = selectable;
    notify_changed();
}

void GraphicsItem::set_draggable(bool draggable) {
    if (draggable_ == draggable)
        return;
    draggable_ = draggable;
    notify_changed();
}

tc_ui_point GraphicsItem::world_position() const {
    tc_ui_point result = position_;
    std::shared_ptr<GraphicsItem> current = parent_.lock();
    size_t depth = 0;
    while (current) {
        if (++depth > 1'000'000) {
            tc_log_error("[termin-gui-native] GraphicsItem parent cycle detected");
            break;
        }
        result.x += current->position_.x;
        result.y += current->position_.y;
        current = current->parent_.lock();
    }
    return result;
}

tc_ui_rect GraphicsItem::world_bounds() const {
    const tc_ui_point world = world_position();
    return {world.x, world.y, size_.width, size_.height};
}

bool GraphicsItem::contains_local(float x, float y) const {
    if (hit_test_callback_)
        return hit_test_callback_(*this, x, y);
    return x >= 0.0f && y >= 0.0f && x <= size_.width && y <= size_.height;
}

std::shared_ptr<GraphicsItem> GraphicsItem::hit_test(float world_x, float world_y) {
    if (!visible_ || !enabled_)
        return nullptr;
    const tc_ui_point world = world_position();
    const float local_x = world_x - world.x;
    const float local_y = world_y - world.y;
    const bool contains = contains_local(local_x, local_y);
    if (!contains && !hit_test_callback_)
        return nullptr;
    for (const auto& child : sorted_by_z(children_, true)) {
        if (auto hit = child->hit_test(world_x, world_y))
            return hit;
    }
    return contains ? shared_from_this() : nullptr;
}

void GraphicsItem::set_paint_callback(PaintCallback callback) {
    paint_callback_ = std::move(callback);
    notify_changed();
}

void GraphicsItem::set_hit_test_callback(HitTestCallback callback) {
    hit_test_callback_ = std::move(callback);
    notify_changed();
}

void GraphicsItem::paint(tc_ui_paint_context* context, const SceneTransform& transform) {
    if (!paint_callback_)
        return;
    try {
        paint_callback_(*this, context, transform);
    } catch (const std::exception& error) {
        tc_log_error("[termin-gui-native] GraphicsItem paint callback failed: %s", error.what());
    } catch (...) {
        tc_log_error("[termin-gui-native] GraphicsItem paint callback failed");
    }
}

void GraphicsItem::set_embedded_widget(tc_widget_handle handle) {
    if (tc_widget_handle_eq(embedded_widget_, handle))
        return;
    embedded_widget_ = handle;
    notify_changed();
}

void GraphicsItem::clear_embedded_widget() { set_embedded_widget(tc_widget_handle_invalid()); }

void GraphicsItem::set_scene_recursive(GraphicsScene* scene) {
    scene_ = scene;
    for (const auto& child : children_)
        child->set_scene_recursive(scene);
}

void GraphicsItem::notify_changed() {
    if (scene_)
        scene_->notify_item_changed();
}

void GraphicsItem::set_selected_internal(bool selected) { selected_ = selected; }

void GraphicsItem::set_hovered_internal(bool hovered) {
    if (hovered_ == hovered)
        return;
    hovered_ = hovered;
    notify_changed();
}

GraphicsScene::~GraphicsScene() {
    for (const auto& selected : selected_items_)
        selected->set_selected_internal(false);
    for (const auto& item : items_)
        item->set_scene_recursive(nullptr);
}

bool GraphicsScene::add_item(std::shared_ptr<GraphicsItem> item) {
    if (!item || item->parent() || item->scene_ || contains(item.get())) {
        tc_log_error("[termin-gui-native] GraphicsScene rejected invalid root ownership");
        return false;
    }
    item->set_scene_recursive(this);
    items_.push_back(std::move(item));
    notify_item_changed();
    return true;
}

bool GraphicsScene::remove_item(const std::shared_ptr<GraphicsItem>& item) {
    const auto found = std::find(items_.begin(), items_.end(), item);
    if (found == items_.end())
        return false;
    clear_selected_recursive(*found);
    (*found)->set_scene_recursive(nullptr);
    items_.erase(found);
    notify_item_changed();
    notify_selection_changed();
    return true;
}

void GraphicsScene::clear() {
    if (items_.empty() && selected_items_.empty())
        return;
    for (const auto& selected : selected_items_)
        selected->set_selected_internal(false);
    for (const auto& item : items_)
        item->set_scene_recursive(nullptr);
    items_.clear();
    selected_items_.clear();
    notify_item_changed();
    notify_selection_changed();
}

std::shared_ptr<GraphicsItem> GraphicsScene::hit_test(float world_x, float world_y) const {
    if (!std::isfinite(world_x) || !std::isfinite(world_y))
        return nullptr;
    for (const auto& item : sorted_by_z(items_, true)) {
        if (auto hit = item->hit_test(world_x, world_y))
            return hit;
    }
    return nullptr;
}

bool GraphicsScene::set_selected(const std::shared_ptr<GraphicsItem>& item) {
    if (item && (!item->selectable() || !contains(item.get())))
        return false;
    if (selected_items_.size() == (item ? 1u : 0u) && (!item || selected_items_.front() == item)) {
        return false;
    }
    for (const auto& selected : selected_items_)
        selected->set_selected_internal(false);
    selected_items_.clear();
    if (item) {
        item->set_selected_internal(true);
        selected_items_.push_back(item);
    }
    notify_selection_changed();
    return true;
}

bool GraphicsScene::toggle_selected(const std::shared_ptr<GraphicsItem>& item) {
    if (!item || !item->selectable() || !contains(item.get()))
        return false;
    const auto found = std::find(selected_items_.begin(), selected_items_.end(), item);
    if (found != selected_items_.end()) {
        item->set_selected_internal(false);
        selected_items_.erase(found);
    } else {
        item->set_selected_internal(true);
        selected_items_.push_back(item);
    }
    notify_selection_changed();
    return true;
}

bool GraphicsScene::clear_selection() {
    if (selected_items_.empty())
        return false;
    for (const auto& selected : selected_items_)
        selected->set_selected_internal(false);
    selected_items_.clear();
    notify_selection_changed();
    return true;
}

bool GraphicsScene::contains_recursive(const GraphicsItem* root, const GraphicsItem* item) const {
    if (root == item)
        return true;
    for (const auto& child : root->children()) {
        if (contains_recursive(child.get(), item))
            return true;
    }
    return false;
}

bool GraphicsScene::contains(const GraphicsItem* item) const {
    if (!item)
        return false;
    for (const auto& root : items_) {
        if (contains_recursive(root.get(), item))
            return true;
    }
    return false;
}

void GraphicsScene::clear_selected_recursive(const std::shared_ptr<GraphicsItem>& item) {
    const auto found = std::find(selected_items_.begin(), selected_items_.end(), item);
    if (found != selected_items_.end()) {
        item->set_selected_internal(false);
        selected_items_.erase(found);
    }
    for (const auto& child : item->children())
        clear_selected_recursive(child);
}

void GraphicsScene::notify_item_changed() {
    ++revision_;
    if (revision_ == 0)
        revision_ = 1;
    changed_.emit(*this);
}

void GraphicsScene::notify_selection_changed() {
    notify_item_changed();
    selection_changed_.emit(*this, selected_items_);
}

} // namespace termin::gui_native
