#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <termin/gui_native/signal.hpp>
#include <termin/gui_native/tc_ui_document.h>

namespace termin::gui_native {

struct SceneTransform {
    float origin_x = 0.0f;
    float origin_y = 0.0f;
    float zoom = 1.0f;

    tc_ui_point world_to_screen(tc_ui_point point) const;
    tc_ui_point screen_to_world(tc_ui_point point) const;
};

class GraphicsScene;
class SceneView;

class GraphicsItem : public std::enable_shared_from_this<GraphicsItem> {
public:
    using PaintCallback =
        std::function<void(GraphicsItem&, tc_ui_paint_context*, const SceneTransform&)>;
    using HitTestCallback = std::function<bool(const GraphicsItem&, float, float)>;

    explicit GraphicsItem(std::string stable_id = {});
    ~GraphicsItem();

    GraphicsItem(const GraphicsItem&) = delete;
    GraphicsItem& operator=(const GraphicsItem&) = delete;

    uint64_t id() const { return id_; }
    const std::string& stable_id() const { return stable_id_; }
    void set_stable_id(std::string stable_id);

    std::shared_ptr<GraphicsItem> parent() const { return parent_.lock(); }
    const std::vector<std::shared_ptr<GraphicsItem>>& children() const { return children_; }
    bool add_child(std::shared_ptr<GraphicsItem> child);
    bool remove_child(const std::shared_ptr<GraphicsItem>& child);
    void clear_children();

    tc_ui_point position() const { return position_; }
    void set_position(tc_ui_point position);
    tc_ui_size size() const { return size_; }
    void set_size(tc_ui_size size);
    float z_index() const { return z_index_; }
    void set_z_index(float z_index);
    bool visible() const { return visible_; }
    void set_visible(bool visible);
    bool enabled() const { return enabled_; }
    void set_enabled(bool enabled);
    bool selectable() const { return selectable_; }
    void set_selectable(bool selectable);
    bool draggable() const { return draggable_; }
    void set_draggable(bool draggable);
    bool selected() const { return selected_; }
    bool hovered() const { return hovered_; }

    tc_ui_point world_position() const;
    tc_ui_rect world_bounds() const;
    bool contains_local(float x, float y) const;
    std::shared_ptr<GraphicsItem> hit_test(float world_x, float world_y);

    void set_paint_callback(PaintCallback callback);
    void set_hit_test_callback(HitTestCallback callback);
    void paint(tc_ui_paint_context* context, const SceneTransform& transform);

    tc_widget_handle embedded_widget() const { return embedded_widget_; }
    void set_embedded_widget(tc_widget_handle handle);
    void clear_embedded_widget();

private:
    friend class GraphicsScene;
    friend class SceneView;

    bool is_ancestor_of(const GraphicsItem* item) const;
    void set_scene_recursive(GraphicsScene* scene);
    void notify_changed();
    void set_selected_internal(bool selected);
    void set_hovered_internal(bool hovered);

    uint64_t id_ = 0;
    std::string stable_id_;
    std::weak_ptr<GraphicsItem> parent_;
    std::vector<std::shared_ptr<GraphicsItem>> children_;
    GraphicsScene* scene_ = nullptr;
    tc_ui_point position_{};
    tc_ui_size size_{100.0f, 60.0f};
    float z_index_ = 0.0f;
    bool visible_ = true;
    bool enabled_ = true;
    bool selectable_ = true;
    bool draggable_ = false;
    bool selected_ = false;
    bool hovered_ = false;
    PaintCallback paint_callback_;
    HitTestCallback hit_test_callback_;
    tc_widget_handle embedded_widget_ = tc_widget_handle_invalid();
};

class GraphicsScene : public std::enable_shared_from_this<GraphicsScene> {
public:
    GraphicsScene() = default;
    ~GraphicsScene();

    GraphicsScene(const GraphicsScene&) = delete;
    GraphicsScene& operator=(const GraphicsScene&) = delete;

    const std::vector<std::shared_ptr<GraphicsItem>>& items() const { return items_; }
    bool add_item(std::shared_ptr<GraphicsItem> item);
    bool remove_item(const std::shared_ptr<GraphicsItem>& item);
    void clear();
    std::shared_ptr<GraphicsItem> hit_test(float world_x, float world_y) const;

    const std::vector<std::shared_ptr<GraphicsItem>>& selected_items() const {
        return selected_items_;
    }
    bool set_selected(const std::shared_ptr<GraphicsItem>& item);
    bool toggle_selected(const std::shared_ptr<GraphicsItem>& item);
    bool clear_selection();
    bool contains(const GraphicsItem* item) const;
    uint64_t revision() const { return revision_; }

    Signal<GraphicsScene&>& changed() { return changed_; }
    Signal<GraphicsScene&, const std::vector<std::shared_ptr<GraphicsItem>>&>& selection_changed() {
        return selection_changed_;
    }

    void notify_item_changed();

private:
    friend class GraphicsItem;

    bool contains_recursive(const GraphicsItem* root, const GraphicsItem* item) const;
    void clear_selected_recursive(const std::shared_ptr<GraphicsItem>& item);
    void notify_selection_changed();

    std::vector<std::shared_ptr<GraphicsItem>> items_;
    std::vector<std::shared_ptr<GraphicsItem>> selected_items_;
    uint64_t revision_ = 1;
    Signal<GraphicsScene&> changed_;
    Signal<GraphicsScene&, const std::vector<std::shared_ptr<GraphicsItem>>&> selection_changed_;
};

} // namespace termin::gui_native
