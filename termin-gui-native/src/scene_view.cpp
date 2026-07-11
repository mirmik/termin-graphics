#include <termin/gui_native/scene_view.hpp>

#include "widgets_internal.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace termin::gui_native {

namespace {

template <typename T>
std::vector<std::shared_ptr<T>> sorted_by_z(const std::vector<std::shared_ptr<T>>& items,
                                            bool reverse = false) {
    std::vector<std::shared_ptr<T>> sorted = items;
    std::stable_sort(sorted.begin(), sorted.end(), [reverse](const auto& left, const auto& right) {
        return reverse ? left->z_index() > right->z_index() : left->z_index() < right->z_index();
    });
    return sorted;
}

bool same_handle(tc_widget_handle left, tc_widget_handle right) {
    return tc_widget_handle_eq(left, right);
}

} // namespace

SceneView::SceneView(std::shared_ptr<GraphicsScene> scene)
    : NativeWidget("SceneView"),
      scene_(scene ? std::move(scene) : std::make_shared<GraphicsScene>()) {
    set_style_role(TC_UI_STYLE_PANEL);
    set_focusable(true);
    set_preferred_size({480.0f, 320.0f});
    connect_scene();
}

SceneView::~SceneView() { disconnect_scene(); }

void SceneView::connect_scene() {
    if (scene_ && scene_connection_ == 0) {
        scene_connection_ =
            scene_->changed().connect([this](GraphicsScene&) { on_scene_changed(); });
    }
}

void SceneView::disconnect_scene() {
    if (scene_ && scene_connection_ != 0)
        scene_->changed().disconnect(scene_connection_);
    scene_connection_ = 0;
}

void SceneView::on_scene_changed() {
    mark_dirty(TC_WIDGET_DIRTY_LAYOUT | TC_WIDGET_DIRTY_PAINT | TC_WIDGET_DIRTY_STATE);
}

void SceneView::set_scene(std::shared_ptr<GraphicsScene> scene) {
    disconnect_scene();
    scene_ = scene ? std::move(scene) : std::make_shared<GraphicsScene>();
    drag_item_.reset();
    set_hovered_item(nullptr);
    connect_scene();
    on_scene_changed();
}

void SceneView::set_zoom(float zoom, tc_ui_point anchor) {
    if (!std::isfinite(zoom) || !std::isfinite(anchor.x) || !std::isfinite(anchor.y)) {
        tc_log_error("[termin-gui-native] SceneView rejected invalid zoom request");
        throw std::invalid_argument("scene zoom and anchor must be finite");
    }
    const tc_ui_point world_anchor = screen_to_world(anchor);
    const float next = detail::clamp_float(zoom, min_zoom_, max_zoom_);
    if (std::fabs(next - zoom_) <= 0.000001f)
        return;
    zoom_ = next;
    offset_.x = anchor.x - bounds().x - world_anchor.x * zoom_;
    offset_.y = anchor.y - bounds().y - world_anchor.y * zoom_;
    mark_dirty(TC_WIDGET_DIRTY_LAYOUT | TC_WIDGET_DIRTY_PAINT | TC_WIDGET_DIRTY_STATE);
    emit_transform_changed();
}

void SceneView::set_zoom_range(float minimum, float maximum) {
    if (!std::isfinite(minimum) || !std::isfinite(maximum) || minimum <= 0.0f ||
        maximum < minimum) {
        tc_log_error("[termin-gui-native] SceneView rejected invalid zoom range");
        throw std::invalid_argument("scene zoom range must be finite, positive and ordered");
    }
    min_zoom_ = minimum;
    max_zoom_ = maximum;
    const float clamped = detail::clamp_float(zoom_, minimum, maximum);
    if (clamped != zoom_) {
        zoom_ = clamped;
        emit_transform_changed();
    }
    on_scene_changed();
}

void SceneView::set_zoom_factor(float factor) {
    if (!std::isfinite(factor) || factor <= 1.0f) {
        tc_log_error("[termin-gui-native] SceneView rejected invalid zoom factor");
        throw std::invalid_argument("scene zoom factor must be finite and greater than one");
    }
    zoom_factor_ = factor;
}

void SceneView::set_offset(tc_ui_point offset) {
    if (!std::isfinite(offset.x) || !std::isfinite(offset.y)) {
        tc_log_error("[termin-gui-native] SceneView rejected non-finite offset");
        throw std::invalid_argument("scene offset must be finite");
    }
    if (offset_.x == offset.x && offset_.y == offset.y)
        return;
    offset_ = offset;
    mark_dirty(TC_WIDGET_DIRTY_LAYOUT | TC_WIDGET_DIRTY_PAINT | TC_WIDGET_DIRTY_STATE);
    emit_transform_changed();
}

void SceneView::set_show_grid(bool show) {
    if (show_grid_ == show)
        return;
    show_grid_ = show;
    mark_dirty(TC_WIDGET_DIRTY_PAINT);
}

void SceneView::set_grid_step(float step) {
    if (!std::isfinite(step) || step <= 0.0f) {
        tc_log_error("[termin-gui-native] SceneView rejected invalid grid step");
        throw std::invalid_argument("scene grid step must be finite and positive");
    }
    grid_step_ = step;
    mark_dirty(TC_WIDGET_DIRTY_PAINT);
}

void SceneView::set_scene_colors(Color background, Color grid, Color axes) {
    background_ = background;
    grid_ = grid;
    axes_ = axes;
    mark_dirty(TC_WIDGET_DIRTY_PAINT);
}

SceneTransform SceneView::transform() const {
    return {bounds().x + offset_.x, bounds().y + offset_.y, zoom_};
}

tc_ui_point SceneView::world_to_screen(tc_ui_point point) const {
    return transform().world_to_screen(point);
}

tc_ui_point SceneView::screen_to_world(tc_ui_point point) const {
    return transform().screen_to_world(point);
}

void SceneView::collect_embedded(const std::shared_ptr<GraphicsItem>& item,
                                 std::vector<std::shared_ptr<GraphicsItem>>& result) const {
    if (!item)
        return;
    if (!tc_widget_handle_is_invalid(item->embedded_widget()))
        result.push_back(item);
    for (const auto& child : item->children())
        collect_embedded(child, result);
}

void SceneView::reconcile_embedded_widgets(tc_ui_document* document) {
    if (!document)
        return;
    std::vector<std::shared_ptr<GraphicsItem>> embedded;
    for (const auto& item : scene_->items())
        collect_embedded(item, embedded);

    std::vector<tc_widget_handle> next;
    next.reserve(embedded.size());
    for (const auto& item : embedded) {
        const tc_widget_handle handle = item->embedded_widget();
        tc_widget* widget = tc_ui_document_resolve_widget(document, handle);
        if (!widget) {
            tc_log_error("[termin-gui-native] SceneView ignored stale embedded widget handle");
            continue;
        }
        if (widget->parent && widget->parent != c_widget()) {
            tc_log_error("[termin-gui-native] SceneView cannot steal an embedded widget from "
                         "another parent");
            continue;
        }
        if (!widget->parent && !tc_widget_append_child(c_widget(), widget)) {
            tc_log_error("[termin-gui-native] SceneView failed to attach embedded widget");
            continue;
        }
        next.push_back(handle);
    }

    for (tc_widget_handle previous : embedded_widgets_) {
        if (std::find_if(next.begin(), next.end(), [previous](tc_widget_handle handle) {
                return same_handle(previous, handle);
            }) != next.end()) {
            continue;
        }
        tc_widget* widget = tc_ui_document_resolve_widget(document, previous);
        if (widget && widget->parent == c_widget())
            tc_widget_detach(widget);
    }
    embedded_widgets_ = std::move(next);
}

void SceneView::layout_item(tc_ui_document* document, const std::shared_ptr<GraphicsItem>& item,
                            const SceneTransform& scene_transform) {
    if (!item || !item->visible())
        return;
    const tc_widget_handle embedded = item->embedded_widget();
    if (!tc_widget_handle_is_invalid(embedded)) {
        tc_widget* widget = tc_ui_document_resolve_widget(document, embedded);
        if (widget && widget->parent == c_widget()) {
            const tc_ui_rect world = item->world_bounds();
            const tc_ui_point screen = scene_transform.world_to_screen({world.x, world.y});
            detail::layout_widget(widget, document,
                                  {screen.x, screen.y, std::max(1.0f, world.width * zoom_),
                                   std::max(1.0f, world.height * zoom_)});
        }
    }
    for (const auto& child : item->children())
        layout_item(document, child, scene_transform);
}

tc_ui_size SceneView::measure(tc_ui_document*, tc_ui_constraints constraints) {
    return detail::clamp_size(preferred_size(), constraints);
}

void SceneView::layout(tc_ui_document* document, tc_ui_rect rect) {
    NativeWidget::layout(document, rect);
    reconcile_embedded_widgets(document);
    const SceneTransform current = transform();
    for (const auto& item : scene_->items())
        layout_item(document, item, current);
}

void SceneView::paint_item(tc_ui_document* document, tc_ui_paint_context* context,
                           const std::shared_ptr<GraphicsItem>& item,
                           const SceneTransform& scene_transform) {
    if (!item || !item->visible())
        return;
    item->paint(context, scene_transform);
    const tc_widget_handle embedded = item->embedded_widget();
    if (!tc_widget_handle_is_invalid(embedded)) {
        tc_widget* widget = tc_ui_document_resolve_widget(document, embedded);
        if (widget && widget->parent == c_widget())
            detail::paint_widget(widget, document, context);
    }
    for (const auto& child : sorted_by_z(item->children()))
        paint_item(document, context, child, scene_transform);
}

void SceneView::paint(tc_ui_document* document, tc_ui_paint_context* context) {
    reconcile_embedded_widgets(document);
    tc_ui_painter_fill_rect(context, bounds(), background_.c_color());
    tc_ui_painter_push_clip(context, bounds());
    const SceneTransform current = transform();
    if (show_grid_ && grid_step_ > 0.0f) {
        const tc_ui_point world_min = screen_to_world({bounds().x, bounds().y});
        const tc_ui_point world_max =
            screen_to_world({bounds().x + bounds().width, bounds().y + bounds().height});
        const double first_x = std::floor(world_min.x / grid_step_) * grid_step_;
        const double first_y = std::floor(world_min.y / grid_step_) * grid_step_;
        const size_t max_lines = 10000;
        size_t lines = 0;
        for (double x = first_x; x <= world_max.x + grid_step_ && lines < max_lines;
             x += grid_step_, ++lines) {
            const float screen_x = world_to_screen({static_cast<float>(x), 0.0f}).x;
            tc_ui_painter_draw_line(context, {screen_x, bounds().y},
                                    {screen_x, bounds().y + bounds().height},
                                    (std::fabs(x) < 0.0001 ? axes_ : grid_).c_color(), 1.0f);
        }
        lines = 0;
        for (double y = first_y; y <= world_max.y + grid_step_ && lines < max_lines;
             y += grid_step_, ++lines) {
            const float screen_y = world_to_screen({0.0f, static_cast<float>(y)}).y;
            tc_ui_painter_draw_line(context, {bounds().x, screen_y},
                                    {bounds().x + bounds().width, screen_y},
                                    (std::fabs(y) < 0.0001 ? axes_ : grid_).c_color(), 1.0f);
        }
    }
    for (const auto& item : sorted_by_z(scene_->items()))
        paint_item(document, context, item, current);
    tc_ui_painter_pop_clip(context);
}

std::shared_ptr<GraphicsItem>
SceneView::selectable_ancestor(std::shared_ptr<GraphicsItem> item) const {
    while (item && !item->selectable())
        item = item->parent();
    return item;
}

std::shared_ptr<GraphicsItem>
SceneView::draggable_ancestor(std::shared_ptr<GraphicsItem> item) const {
    while (item && !item->draggable())
        item = item->parent();
    return item;
}

void SceneView::set_hovered_item(std::shared_ptr<GraphicsItem> item) {
    if (hovered_item_ == item)
        return;
    if (hovered_item_)
        hovered_item_->set_hovered_internal(false);
    hovered_item_ = std::move(item);
    if (hovered_item_)
        hovered_item_->set_hovered_internal(true);
    mark_dirty(TC_WIDGET_DIRTY_PAINT | TC_WIDGET_DIRTY_STATE);
}

void SceneView::emit_transform_changed() {
    const SceneTransform current = transform();
    transform_changed_.emit(*this, current);
}

tc_ui_event_result SceneView::pointer_event(tc_ui_document* document,
                                            const tc_ui_pointer_event* event) {
    if (!event)
        return TC_UI_EVENT_IGNORED;
    const bool captured = tc_widget_handle_eq(tc_ui_document_pointer_capture(document), handle());
    const tc_ui_point world = screen_to_world({event->x, event->y});
    if (pointer_handler_) {
        try {
            if (pointer_handler_(*this, world, *event))
                return TC_UI_EVENT_HANDLED;
        } catch (const std::exception& error) {
            tc_log_error("[termin-gui-native] SceneView pointer handler failed: %s", error.what());
        } catch (...) {
            tc_log_error("[termin-gui-native] SceneView pointer handler failed");
        }
    }
    if (event->type == TC_UI_POINTER_WHEEL && detail::rect_contains(bounds(), event->x, event->y)) {
        const float factor = event->wheel_y > 0.0f ? zoom_factor_ : 1.0f / zoom_factor_;
        set_zoom(zoom_ * factor, {event->x, event->y});
        return TC_UI_EVENT_HANDLED;
    }
    if (event->type == TC_UI_POINTER_DOWN &&
        event->button == pointer_button_value(PointerButton::Middle) &&
        detail::rect_contains(bounds(), event->x, event->y)) {
        panning_ = true;
        pan_start_ = {event->x, event->y};
        pan_start_offset_ = offset_;
        tc_ui_document_set_focus(document, handle());
        tc_ui_document_set_pointer_capture(document, handle());
        return TC_UI_EVENT_HANDLED;
    }
    if (event->type == TC_UI_POINTER_DOWN &&
        event->button == pointer_button_value(PointerButton::Left) &&
        detail::rect_contains(bounds(), event->x, event->y)) {
        tc_ui_document_set_focus(document, handle());
        const auto hit = scene_->hit_test(world.x, world.y);
        if ((event->modifiers & TC_UI_MOD_CTRL) != 0)
            scene_->toggle_selected(selectable_ancestor(hit));
        else
            scene_->set_selected(selectable_ancestor(hit));
        drag_item_ = draggable_ancestor(hit);
        if (drag_item_) {
            drag_item_start_ = drag_item_->position();
            drag_pointer_start_ = world;
            tc_ui_document_set_pointer_capture(document, handle());
        }
        return TC_UI_EVENT_HANDLED;
    }
    if (event->type == TC_UI_POINTER_MOVE && panning_) {
        set_offset({pan_start_offset_.x + event->x - pan_start_.x,
                    pan_start_offset_.y + event->y - pan_start_.y});
        return TC_UI_EVENT_HANDLED;
    }
    if (event->type == TC_UI_POINTER_MOVE && drag_item_) {
        drag_item_->set_position({drag_item_start_.x + world.x - drag_pointer_start_.x,
                                  drag_item_start_.y + world.y - drag_pointer_start_.y});
        item_moved_.emit(*this, drag_item_);
        return TC_UI_EVENT_HANDLED;
    }
    if (event->type == TC_UI_POINTER_MOVE && detail::rect_contains(bounds(), event->x, event->y)) {
        set_hovered_item(scene_->hit_test(world.x, world.y));
        return TC_UI_EVENT_HANDLED;
    }
    if (event->type == TC_UI_POINTER_LEAVE && !captured)
        set_hovered_item(nullptr);
    if (event->type == TC_UI_POINTER_UP && (panning_ || drag_item_ || captured)) {
        panning_ = false;
        drag_item_.reset();
        tc_ui_document_release_pointer_capture(document, handle());
        return TC_UI_EVENT_HANDLED;
    }
    return TC_UI_EVENT_IGNORED;
}

tc_ui_event_result SceneView::key_event(tc_ui_document*, const tc_ui_key_event* event) {
    if (!event)
        return TC_UI_EVENT_IGNORED;
    if (key_handler_) {
        try {
            return key_handler_(*this, *event) ? TC_UI_EVENT_HANDLED : TC_UI_EVENT_IGNORED;
        } catch (const std::exception& error) {
            tc_log_error("[termin-gui-native] SceneView key handler failed: %s", error.what());
        } catch (...) {
            tc_log_error("[termin-gui-native] SceneView key handler failed");
        }
    }
    return TC_UI_EVENT_IGNORED;
}

tc_ui_event_result SceneView::text_event(tc_ui_document*, const tc_ui_text_event* event) {
    if (!event)
        return TC_UI_EVENT_IGNORED;
    if (text_handler_) {
        try {
            return text_handler_(*this, *event) ? TC_UI_EVENT_HANDLED : TC_UI_EVENT_IGNORED;
        } catch (const std::exception& error) {
            tc_log_error("[termin-gui-native] SceneView text handler failed: %s", error.what());
        } catch (...) {
            tc_log_error("[termin-gui-native] SceneView text handler failed");
        }
    }
    return TC_UI_EVENT_IGNORED;
}

tc_widget_handle SceneView::hit_test_embedded(tc_ui_document* document,
                                              const std::shared_ptr<GraphicsItem>& item, float x,
                                              float y) const {
    if (!item || !item->visible() || !item->enabled())
        return tc_widget_handle_invalid();
    for (const auto& child : sorted_by_z(item->children(), true)) {
        const tc_widget_handle hit = hit_test_embedded(document, child, x, y);
        if (!tc_widget_handle_is_invalid(hit))
            return hit;
    }
    const tc_widget_handle embedded = item->embedded_widget();
    tc_widget* widget = tc_ui_document_resolve_widget(document, embedded);
    if (widget && widget->parent == c_widget() && widget->vtable && widget->vtable->hit_test)
        return widget->vtable->hit_test(widget, document, x, y);
    return tc_widget_handle_invalid();
}

tc_widget_handle SceneView::hit_test(tc_ui_document* document, float x, float y) {
    if (!visible() || !detail::rect_contains(bounds(), x, y))
        return tc_widget_handle_invalid();
    for (const auto& item : sorted_by_z(scene_->items(), true)) {
        const tc_widget_handle hit = hit_test_embedded(document, item, x, y);
        if (!tc_widget_handle_is_invalid(hit))
            return hit;
    }
    return mouse_transparent() ? tc_widget_handle_invalid() : handle();
}

void SceneView::on_destroy(tc_ui_document* document) {
    disconnect_scene();
    set_hovered_item(nullptr);
    for (tc_widget_handle handle : embedded_widgets_) {
        tc_widget* widget = tc_ui_document_resolve_widget(document, handle);
        if (widget && widget->parent == c_widget())
            tc_widget_detach(widget);
    }
    embedded_widgets_.clear();
    pointer_handler_ = {};
    key_handler_ = {};
    text_handler_ = {};
    item_moved_ = {};
    transform_changed_ = {};
}

} // namespace termin::gui_native
