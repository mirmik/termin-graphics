#include <termin/gui_native/scene_view.hpp>

#include "widgets_internal.hpp"

#include <algorithm>
#include <cmath>
#include <exception>
#include <limits>
#include <stdexcept>

#include <tcbase/input_enums.hpp>
#include <termin/gui_native/draw_list2d_bridge.hpp>
#include <termin_visual_scene/render_snapshot2d.hpp>

namespace termin::gui_native {
namespace {

using termin::visual::GraphicItemHandle;

bool same_handle(GraphicItemHandle left, GraphicItemHandle right) {
    return left.scene_id == right.scene_id &&
           left.index == right.index &&
           left.generation == right.generation;
}

bool same_widget(tc_widget_handle left, tc_widget_handle right) {
    return tc_widget_handle_eq(left, right);
}

class UiSceneResources final
    : public termin::visual::SceneRenderResourceResolver2D {
public:
    std::optional<tgfx::FontHandle> resolve_font(
        const termin::visual::StableResourceRef2D& reference) override {
        if (reference.uri == "ui://default-font") {
            return tgfx::FontHandle{
                std::numeric_limits<std::uint32_t>::max()};
        }
        return std::nullopt;
    }

    std::optional<tgfx::TextureHandle> resolve_image(
        const termin::visual::StableResourceRef2D&) override {
        return std::nullopt;
    }

    std::optional<termin::visual::ResolvedCustomBatch2D>
    resolve_custom_batch(
        const termin::visual::CustomBatchItem2D&) override {
        return std::nullopt;
    }
};

} // namespace

SceneView::SceneView(std::shared_ptr<GraphicsScene> scene)
    : NativeWidget("SceneView"),
      scene_(scene ? std::move(scene) : std::make_shared<GraphicsScene>()) {
    set_style_role(TC_UI_STYLE_PANEL);
    set_focusable(true);
    set_preferred_size({480.0f, 320.0f});
    connect_scene();
}

SceneView::~SceneView() {
    disconnect_scene();
}

void SceneView::connect_scene() {
    if (scene_ && scene_connection_ == 0) {
        scene_connection_ = scene_->changed().connect(
            [this](GraphicsScene&) { on_scene_changed(); });
    }
}

void SceneView::disconnect_scene() {
    if (scene_ && scene_connection_ != 0) {
        scene_->changed().disconnect(scene_connection_);
    }
    scene_connection_ = 0;
}

void SceneView::on_scene_changed() {
    selection_.reconcile(scene_->visual_scene());
    mark_dirty(
        TC_WIDGET_DIRTY_LAYOUT |
        TC_WIDGET_DIRTY_PAINT |
        TC_WIDGET_DIRTY_STATE);
}

void SceneView::set_scene(std::shared_ptr<GraphicsScene> scene) {
    disconnect_scene();
    scene_ = scene ? std::move(scene) : std::make_shared<GraphicsScene>();
    interaction_.cancel_all();
    selection_.clear();
    drag_.cancel();
    clear_widget_portals();
    connect_scene();
    on_scene_changed();
}

void SceneView::set_zoom(float zoom, tc_ui_point anchor) {
    if (!std::isfinite(zoom) ||
        !std::isfinite(anchor.x) ||
        !std::isfinite(anchor.y)) {
        tc_log_error(
            "[termin-gui-native] SceneView rejected invalid zoom request");
        throw std::invalid_argument(
            "scene zoom and anchor must be finite");
    }
    const tc_ui_point world_anchor = screen_to_world(anchor);
    const float next = detail::clamp_float(zoom, min_zoom_, max_zoom_);
    if (std::fabs(next - zoom_) <= 0.000001f) return;
    zoom_ = next;
    offset_.x = anchor.x - bounds().x - world_anchor.x * zoom_;
    offset_.y = anchor.y - bounds().y - world_anchor.y * zoom_;
    mark_dirty(
        TC_WIDGET_DIRTY_LAYOUT |
        TC_WIDGET_DIRTY_PAINT |
        TC_WIDGET_DIRTY_STATE);
    emit_transform_changed();
}

void SceneView::set_zoom_range(float minimum, float maximum) {
    if (!std::isfinite(minimum) || !std::isfinite(maximum) ||
        minimum <= 0.0f || maximum < minimum) {
        tc_log_error(
            "[termin-gui-native] SceneView rejected invalid zoom range");
        throw std::invalid_argument(
            "scene zoom range must be finite, positive and ordered");
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
        tc_log_error(
            "[termin-gui-native] SceneView rejected invalid zoom factor");
        throw std::invalid_argument(
            "scene zoom factor must be finite and greater than one");
    }
    zoom_factor_ = factor;
}

void SceneView::set_offset(tc_ui_point offset) {
    if (!std::isfinite(offset.x) || !std::isfinite(offset.y)) {
        tc_log_error(
            "[termin-gui-native] SceneView rejected non-finite offset");
        throw std::invalid_argument("scene offset must be finite");
    }
    if (offset_.x == offset.x && offset_.y == offset.y) return;
    offset_ = offset;
    mark_dirty(
        TC_WIDGET_DIRTY_LAYOUT |
        TC_WIDGET_DIRTY_PAINT |
        TC_WIDGET_DIRTY_STATE);
    emit_transform_changed();
}

void SceneView::set_show_grid(bool show) {
    if (show_grid_ == show) return;
    show_grid_ = show;
    mark_dirty(TC_WIDGET_DIRTY_PAINT);
}

void SceneView::set_grid_step(float step) {
    if (!std::isfinite(step) || step <= 0.0f) {
        tc_log_error(
            "[termin-gui-native] SceneView rejected invalid grid step");
        throw std::invalid_argument(
            "scene grid step must be finite and positive");
    }
    grid_step_ = step;
    mark_dirty(TC_WIDGET_DIRTY_PAINT);
}

void SceneView::set_scene_colors(
    Color background,
    Color grid,
    Color axes) {
    background_ = background;
    grid_ = grid;
    axes_ = axes;
    mark_dirty(TC_WIDGET_DIRTY_PAINT);
}

SceneTransform SceneView::transform() const {
    return {
        bounds().x + offset_.x,
        bounds().y + offset_.y,
        zoom_,
    };
}

tc_ui_point SceneView::world_to_screen(tc_ui_point point) const {
    return transform().world_to_screen(point);
}

tc_ui_point SceneView::screen_to_world(tc_ui_point point) const {
    return transform().screen_to_world(point);
}

bool SceneView::set_widget_portal(
    const GraphicItemRef& item,
    tc_widget_handle widget) {
    if (item.scene_ != scene_.get() || !item.valid() ||
        tc_widget_handle_is_invalid(widget)) {
        tc_log_error(
            "[termin-gui-native] SceneView rejected stale portal association");
        return false;
    }
    const auto duplicate_widget = std::find_if(
        portals_.begin(),
        portals_.end(),
        [&](const WidgetPortal& portal) {
            return same_widget(portal.widget, widget) &&
                   !same_handle(portal.item, item.handle_);
        });
    if (duplicate_widget != portals_.end()) {
        tc_log_error(
            "[termin-gui-native] one widget cannot belong to two scene portals");
        return false;
    }
    const auto found = std::find_if(
        portals_.begin(),
        portals_.end(),
        [&](const WidgetPortal& portal) {
            return same_handle(portal.item, item.handle_);
        });
    if (found != portals_.end()) {
        found->widget = widget;
    } else {
        portals_.push_back({item.handle_, widget});
    }
    on_scene_changed();
    return true;
}

bool SceneView::clear_widget_portal(const GraphicItemRef& item) {
    if (item.scene_ != scene_.get()) return false;
    const auto before = portals_.size();
    std::erase_if(portals_, [&](const WidgetPortal& portal) {
        return same_handle(portal.item, item.handle_);
    });
    if (portals_.size() == before) return false;
    on_scene_changed();
    return true;
}

void SceneView::clear_widget_portals() {
    if (portals_.empty()) return;
    portals_.clear();
    on_scene_changed();
}

std::vector<GraphicItemRef> SceneView::selected_items() {
    selection_.reconcile(scene_->visual_scene());
    std::vector<GraphicItemRef> result;
    for (const auto handle : selection_.selection()) {
        if (auto item = scene_->item(handle)) result.push_back(*item);
    }
    return result;
}

std::optional<GraphicItemRef> SceneView::hovered_item() {
    const auto handle = interaction_.hovered(0);
    if (tc_graphic_item_handle_is_invalid(handle)) return std::nullopt;
    return scene_->item(selectable_ancestor(handle));
}

void SceneView::reconcile_portals(tc_ui_document_handle document) {
    std::vector<WidgetPortal> next;
    next.reserve(portals_.size());
    for (const auto& portal : portals_) {
        if (!scene_->visual_scene().snapshot(portal.item)) continue;
        tc_widget* widget =
            tc_ui_document_resolve_widget(document, portal.widget);
        if (!widget) {
            tc_log_error(
                "[termin-gui-native] SceneView detached stale widget portal");
            continue;
        }
        if (widget->parent && widget->parent != c_widget()) {
            tc_log_error(
                "[termin-gui-native] SceneView cannot steal portal widget");
            continue;
        }
        if (!widget->parent &&
            !tc_widget_append_child(c_widget(), widget)) {
            tc_log_error(
                "[termin-gui-native] SceneView failed to attach portal widget");
            continue;
        }
        next.push_back(portal);
    }
    for (const auto& previous : portals_) {
        const bool retained = std::any_of(
            next.begin(),
            next.end(),
            [&](const WidgetPortal& portal) {
                return same_widget(portal.widget, previous.widget);
            });
        if (retained) continue;
        tc_widget* widget =
            tc_ui_document_resolve_widget(document, previous.widget);
        if (widget && widget->parent == c_widget()) tc_widget_detach(widget);
    }
    portals_ = std::move(next);
}

void SceneView::layout_portals(tc_ui_document_handle document) {
    for (const auto& portal : portals_) {
        const auto snapshot = scene_->visual_scene().snapshot(portal.item);
        if (!snapshot || !snapshot->effective_visible ||
            !snapshot->world_bounds) {
            continue;
        }
        tc_widget* widget =
            tc_ui_document_resolve_widget(document, portal.widget);
        if (!widget || widget->parent != c_widget()) continue;
        const auto& world = *snapshot->world_bounds;
        const auto screen =
            world_to_screen({world.x0, world.y0});
        detail::layout_widget(
            widget,
            document,
            {
                screen.x,
                screen.y,
                std::max(1.0f, (world.x1 - world.x0) * zoom_),
                std::max(1.0f, (world.y1 - world.y0) * zoom_),
            });
    }
}

void SceneView::paint_portals(
    tc_ui_document_handle document,
    tc_ui_paint_context* context) {
    auto sorted = portals_;
    std::stable_sort(
        sorted.begin(),
        sorted.end(),
        [&](const WidgetPortal& left, const WidgetPortal& right) {
            const auto a = scene_->visual_scene().snapshot(left.item);
            const auto b = scene_->visual_scene().snapshot(right.item);
            if (!a || !b) return static_cast<bool>(b);
            if (a->state.z_order != b->state.z_order) {
                return a->state.z_order < b->state.z_order;
            }
            return a->stable_order < b->stable_order;
        });
    for (const auto& portal : sorted) {
        const auto snapshot = scene_->visual_scene().snapshot(portal.item);
        if (!snapshot || !snapshot->effective_visible) continue;
        tc_widget* widget =
            tc_ui_document_resolve_widget(document, portal.widget);
        if (widget && widget->parent == c_widget()) {
            detail::paint_widget(widget, document, context);
        }
    }
}

tc_ui_size SceneView::measure(
    tc_ui_document_handle,
    tc_ui_constraints constraints) {
    return detail::clamp_size(preferred_size(), constraints);
}

void SceneView::layout(
    tc_ui_document_handle document,
    tc_ui_rect rect) {
    NativeWidget::layout(document, rect);
    reconcile_portals(document);
    layout_portals(document);
}

void SceneView::paint(
    tc_ui_document_handle document,
    tc_ui_paint_context* context) {
    reconcile_portals(document);
    tc_ui_painter_fill_rect(context, bounds(), background_.c_color());
    tc_ui_painter_push_clip(context, bounds());
    if (show_grid_ && grid_step_ > 0.0f) {
        const tc_ui_point world_min =
            screen_to_world({bounds().x, bounds().y});
        const tc_ui_point world_max = screen_to_world({
            bounds().x + bounds().width,
            bounds().y + bounds().height,
        });
        const double first_x =
            std::floor(world_min.x / grid_step_) * grid_step_;
        const double first_y =
            std::floor(world_min.y / grid_step_) * grid_step_;
        constexpr std::size_t max_lines = 10000;
        std::size_t lines = 0;
        for (double x = first_x;
             x <= world_max.x + grid_step_ && lines < max_lines;
             x += grid_step_, ++lines) {
            const float screen_x =
                world_to_screen({static_cast<float>(x), 0.0f}).x;
            tc_ui_painter_draw_line(
                context,
                {screen_x, bounds().y},
                {screen_x, bounds().y + bounds().height},
                (std::fabs(x) < 0.0001 ? axes_ : grid_).c_color(),
                1.0f);
        }
        lines = 0;
        for (double y = first_y;
             y <= world_max.y + grid_step_ && lines < max_lines;
             y += grid_step_, ++lines) {
            const float screen_y =
                world_to_screen({0.0f, static_cast<float>(y)}).y;
            tc_ui_painter_draw_line(
                context,
                {bounds().x, screen_y},
                {bounds().x + bounds().width, screen_y},
                (std::fabs(y) < 0.0001 ? axes_ : grid_).c_color(),
                1.0f);
        }
    }

    UiSceneResources resources;
    const auto prepared =
        scene_->visual_scene().prepare_render_snapshot(resources);
    if (!prepared) {
        tc_log_error(
            "[termin-gui-native] SceneView failed to prepare visual scene");
    } else {
        tgfx::DrawList2DBuilder builder;
        const auto current = transform();
        const termin::Affine2f camera{
            current.zoom,
            0.0f,
            0.0f,
            current.zoom,
            current.origin_x,
            current.origin_y,
        };
        const bool built =
            builder.push_transform(camera) &&
            builder.append(prepared->draw_list()) &&
            builder.pop_transform();
        auto draw_list = built ? builder.freeze() : std::nullopt;
        if (!draw_list ||
            !append_draw_list2d(context, std::move(*draw_list))) {
            tc_log_error(
                "[termin-gui-native] SceneView failed to append visual DrawList2D");
        }
    }

    selection_.reconcile(scene_->visual_scene());
    for (const auto handle : selection_.selection()) {
        const auto snapshot = scene_->visual_scene().snapshot(handle);
        if (!snapshot || !snapshot->world_bounds) continue;
        const auto& world = *snapshot->world_bounds;
        const auto screen = world_to_screen({world.x0, world.y0});
        tc_ui_painter_stroke_rect(
            context,
            {
                screen.x,
                screen.y,
                (world.x1 - world.x0) * zoom_,
                (world.y1 - world.y0) * zoom_,
            },
            {0.70f, 0.85f, 1.0f, 1.0f},
            1.5f);
    }
    paint_portals(document, context);
    tc_ui_painter_pop_clip(context);
}

GraphicItemHandle SceneView::selectable_ancestor(
    GraphicItemHandle item) const {
    while (!tc_graphic_item_handle_is_invalid(item)) {
        const auto metadata = scene_->metadata_(item);
        if (metadata && metadata->selectable) return item;
        const auto snapshot = scene_->visual_scene().snapshot(item);
        if (!snapshot) break;
        item = snapshot->parent;
    }
    return tc_graphic_item_handle_invalid();
}

GraphicItemHandle SceneView::draggable_ancestor(
    GraphicItemHandle item) const {
    while (!tc_graphic_item_handle_is_invalid(item)) {
        const auto metadata = scene_->metadata_(item);
        if (metadata && metadata->draggable) return item;
        const auto snapshot = scene_->visual_scene().snapshot(item);
        if (!snapshot) break;
        item = snapshot->parent;
    }
    return tc_graphic_item_handle_invalid();
}

void SceneView::emit_transform_changed() {
    const SceneTransform current = transform();
    transform_changed_.emit(*this, current);
}

tc_ui_event_result SceneView::pointer_event(
    tc_ui_document_handle document,
    const tc_ui_pointer_event* event) {
    if (!event) return TC_UI_EVENT_IGNORED;
    const bool captured = tc_widget_handle_eq(
        tc_ui_document_pointer_capture(document),
        handle());
    const auto pointer_button = event->button < 0
        ? 0u
        : static_cast<std::uint32_t>(event->button);
    const tc_ui_point world =
        screen_to_world({event->x, event->y});
    if (pointer_handler_) {
        try {
            if (pointer_handler_(*this, world, *event)) {
                return TC_UI_EVENT_HANDLED;
            }
        } catch (const std::exception& error) {
            tc_log_error(
                "[termin-gui-native] SceneView pointer handler failed: %s",
                error.what());
        } catch (...) {
            tc_log_error(
                "[termin-gui-native] SceneView pointer handler failed");
        }
    }

    if (event->type == TC_UI_POINTER_CANCEL) {
        const bool active =
            panning_ ||
            !tc_graphic_item_handle_is_invalid(drag_.target()) ||
            captured;
        panning_ = false;
        interaction_.route(
            scene_->visual_scene(),
            {0, termin::visual::PointerEventKind2D::Cancel,
             {world.x, world.y}, pointer_button});
        drag_.cancel();
        if (active) {
            mark_dirty(TC_WIDGET_DIRTY_STATE | TC_WIDGET_DIRTY_PAINT);
        }
        return active ? TC_UI_EVENT_HANDLED : TC_UI_EVENT_IGNORED;
    }
    if (event->type == TC_UI_POINTER_WHEEL &&
        detail::rect_contains(bounds(), event->x, event->y)) {
        const float factor =
            event->wheel_y > 0.0f
                ? zoom_factor_
                : 1.0f / zoom_factor_;
        set_zoom(zoom_ * factor, {event->x, event->y});
        return TC_UI_EVENT_HANDLED;
    }
    if (event->type == TC_UI_POINTER_DOWN &&
        event->button ==
            tcbase::mouse_button_value(tcbase::MouseButton::MIDDLE) &&
        detail::rect_contains(bounds(), event->x, event->y)) {
        panning_ = true;
        pan_start_ = {event->x, event->y};
        pan_start_offset_ = offset_;
        tc_ui_document_set_focus(document, handle());
        tc_ui_document_set_pointer_capture(document, handle());
        return TC_UI_EVENT_HANDLED;
    }
    if (event->type == TC_UI_POINTER_DOWN &&
        event->button ==
            tcbase::mouse_button_value(tcbase::MouseButton::LEFT) &&
        detail::rect_contains(bounds(), event->x, event->y)) {
        tc_ui_document_set_focus(document, handle());
        auto dispatch = interaction_.route(
            scene_->visual_scene(),
            {0, termin::visual::PointerEventKind2D::Down,
             {world.x, world.y}, pointer_button});
        const auto selected = selectable_ancestor(dispatch.target);
        if ((event->modifiers & TC_UI_MOD_CTRL) != 0) {
            selection_.toggle(scene_->visual_scene(), selected);
        } else {
            selection_.select(scene_->visual_scene(), selected);
        }
        const auto draggable = draggable_ancestor(dispatch.target);
        if (!tc_graphic_item_handle_is_invalid(draggable)) {
            interaction_.capture(scene_->visual_scene(), 0, draggable);
            dispatch.target = draggable;
            dispatch.captured = draggable;
            if (drag_.handle(scene_->visual_scene(), dispatch)) {
                tc_ui_document_set_pointer_capture(document, handle());
            }
        }
        mark_dirty(TC_WIDGET_DIRTY_STATE | TC_WIDGET_DIRTY_PAINT);
        return TC_UI_EVENT_HANDLED;
    }
    if (event->type == TC_UI_POINTER_MOVE && panning_) {
        set_offset({
            pan_start_offset_.x + event->x - pan_start_.x,
            pan_start_offset_.y + event->y - pan_start_.y,
        });
        return TC_UI_EVENT_HANDLED;
    }
    if (event->type == TC_UI_POINTER_MOVE &&
        (!tc_graphic_item_handle_is_invalid(drag_.target()) ||
         detail::rect_contains(bounds(), event->x, event->y))) {
        auto dispatch = interaction_.route(
            scene_->visual_scene(),
            {0, termin::visual::PointerEventKind2D::Move,
             {world.x, world.y}, pointer_button});
        const auto moving = drag_.target();
        if (!tc_graphic_item_handle_is_invalid(moving) &&
            drag_.handle(scene_->visual_scene(), dispatch)) {
            scene_->notify_changed_();
            if (auto item = scene_->item(moving)) {
                item_moved_.emit(*this, *item);
            }
        }
        mark_dirty(TC_WIDGET_DIRTY_STATE | TC_WIDGET_DIRTY_PAINT);
        return TC_UI_EVENT_HANDLED;
    }
    if (event->type == TC_UI_POINTER_LEAVE && !captured) {
        interaction_.route(
            scene_->visual_scene(),
            {0, termin::visual::PointerEventKind2D::Cancel,
             {world.x, world.y}, pointer_button});
        mark_dirty(TC_WIDGET_DIRTY_STATE | TC_WIDGET_DIRTY_PAINT);
    }
    if (event->type == TC_UI_POINTER_UP &&
        (panning_ ||
         !tc_graphic_item_handle_is_invalid(drag_.target()) ||
         captured)) {
        panning_ = false;
        auto dispatch = interaction_.route(
            scene_->visual_scene(),
            {0, termin::visual::PointerEventKind2D::Up,
             {world.x, world.y}, pointer_button});
        drag_.handle(scene_->visual_scene(), dispatch);
        tc_ui_document_release_pointer_capture(document, handle());
        return TC_UI_EVENT_HANDLED;
    }
    return TC_UI_EVENT_IGNORED;
}

tc_ui_event_result SceneView::key_event(
    tc_ui_document_handle,
    const tc_ui_key_event* event) {
    if (!event) return TC_UI_EVENT_IGNORED;
    if (key_handler_) {
        try {
            return key_handler_(*this, *event)
                ? TC_UI_EVENT_HANDLED
                : TC_UI_EVENT_IGNORED;
        } catch (const std::exception& error) {
            tc_log_error(
                "[termin-gui-native] SceneView key handler failed: %s",
                error.what());
        } catch (...) {
            tc_log_error(
                "[termin-gui-native] SceneView key handler failed");
        }
    }
    return TC_UI_EVENT_IGNORED;
}

tc_ui_event_result SceneView::text_event(
    tc_ui_document_handle,
    const tc_ui_text_event* event) {
    if (!event) return TC_UI_EVENT_IGNORED;
    if (text_handler_) {
        try {
            return text_handler_(*this, *event)
                ? TC_UI_EVENT_HANDLED
                : TC_UI_EVENT_IGNORED;
        } catch (const std::exception& error) {
            tc_log_error(
                "[termin-gui-native] SceneView text handler failed: %s",
                error.what());
        } catch (...) {
            tc_log_error(
                "[termin-gui-native] SceneView text handler failed");
        }
    }
    return TC_UI_EVENT_IGNORED;
}

tc_widget_handle SceneView::hit_test_portals(
    tc_ui_document_handle document,
    float x,
    float y) const {
    auto sorted = portals_;
    std::stable_sort(
        sorted.begin(),
        sorted.end(),
        [&](const WidgetPortal& left, const WidgetPortal& right) {
            const auto a = scene_->visual_scene().snapshot(left.item);
            const auto b = scene_->visual_scene().snapshot(right.item);
            if (!a || !b) return static_cast<bool>(a);
            if (a->state.z_order != b->state.z_order) {
                return a->state.z_order > b->state.z_order;
            }
            return a->stable_order > b->stable_order;
        });
    for (const auto& portal : sorted) {
        const auto snapshot =
            scene_->visual_scene().snapshot(portal.item);
        if (!snapshot || !snapshot->effective_visible ||
            !snapshot->effective_enabled) {
            continue;
        }
        tc_widget* widget =
            tc_ui_document_resolve_widget(document, portal.widget);
        if (widget && widget->parent == c_widget() &&
            widget->vtable && widget->vtable->hit_test) {
            const auto hit =
                widget->vtable->hit_test(widget, document, x, y);
            if (!tc_widget_handle_is_invalid(hit)) return hit;
        }
    }
    return tc_widget_handle_invalid();
}

tc_widget_handle SceneView::hit_test(
    tc_ui_document_handle document,
    float x,
    float y) {
    if (!visible() || !detail::rect_contains(bounds(), x, y)) {
        return tc_widget_handle_invalid();
    }
    const auto portal = hit_test_portals(document, x, y);
    if (!tc_widget_handle_is_invalid(portal)) return portal;
    return mouse_transparent()
        ? tc_widget_handle_invalid()
        : handle();
}

void SceneView::on_destroy(tc_ui_document_handle document) {
    disconnect_scene();
    interaction_.cancel_all();
    selection_.clear();
    drag_.cancel();
    for (const auto& portal : portals_) {
        tc_widget* widget =
            tc_ui_document_resolve_widget(document, portal.widget);
        if (widget && widget->parent == c_widget()) tc_widget_detach(widget);
    }
    portals_.clear();
    pointer_handler_ = {};
    key_handler_ = {};
    text_handler_ = {};
    item_moved_ = {};
    transform_changed_ = {};
}

} // namespace termin::gui_native
