#include <termin/gui_native/scene_view.hpp>

#include "widgets_internal.hpp"

#include <algorithm>
#include <cmath>
#include <exception>
#include <limits>
#include <stdexcept>

#include <geom/tc_affine2.h>
#include <tcbase/input_enums.hpp>
#include <termin/gui_native/draw_list2d_bridge.hpp>
#include <termin_visual_scene/scene_render2d.hpp>

namespace termin::gui_native {
    namespace {
        class SceneViewLayerSink final : public termin::visual::ScenePaintLayerSink2D {
        public:
            SceneViewLayerSink(tc_ui_paint_context* context,
                               WidgetSceneProjectionBridge& projections,
                               termin::Affine2f camera)
                : context_(context),
                  projections_(projections),
                  camera_(camera) {}

            bool append_item_layer(const tc_graphic_item& item, tgfx::DrawList2D draw_list) override {
                if (!draw_list.empty()) {
                    tgfx::DrawList2DBuilder builder;
                    if (!builder.push_transform(camera_) || !builder.append(draw_list) || !builder.pop_transform())
                        return false;
                    auto placed = builder.freeze();
                    if (!placed || !append_draw_list2d(context_, std::move(*placed)))
                        return false;
                }
                projections_.paint_source(item.handle, context_);
                return true;
            }

        private:
            tc_ui_paint_context* context_ = nullptr;
            WidgetSceneProjectionBridge& projections_;
            termin::Affine2f camera_ = termin::Affine2f::identity();
        };
    } // namespace
    namespace {

        using termin::visual::GraphicItemHandle;

        class UiSceneResources final : public termin::visual::SceneRenderResourceResolver2D {
        public:
            std::optional<tgfx::FontHandle> resolve_font(std::string_view uri) override {
                if (uri == "ui://default-font") {
                    return tgfx::FontHandle{std::numeric_limits<std::uint32_t>::max()};
                }
                return std::nullopt;
            }

            std::optional<tgfx::TextureHandle> resolve_image(std::string_view) override {
                return std::nullopt;
            }

            std::optional<termin::visual::ResolvedCustomBatch2D> resolve_custom_batch(std::string_view,
                                                                                      termin::Bounds2f) override {
                return std::nullopt;
            }
        };

    } // namespace

    tc_ui_point SceneTransform::world_to_screen(tc_ui_point point) const {
        const tc_affine2f affine = tc_affine2f_new(zoom, 0.0f, 0.0f, zoom, origin_x, origin_y);
        const tc_vec2f mapped = tc_affine2f_transform_point(affine, TC_VEC2F(point.x, point.y));
        return {mapped.x, mapped.y};
    }

    tc_ui_point SceneTransform::screen_to_world(tc_ui_point point) const {
        const tc_affine2f affine = tc_affine2f_new(zoom, 0.0f, 0.0f, zoom, origin_x, origin_y);
        tc_affine2f inverse;
        if (!tc_affine2f_try_inverse(affine, 1.0e-8f, &inverse)) {
            tc_log_error("[termin-gui-native] SceneTransform cannot invert invalid camera placement");
            return {};
        }
        const tc_vec2f mapped = tc_affine2f_transform_point(inverse, TC_VEC2F(point.x, point.y));
        return {mapped.x, mapped.y};
    }

    SceneView::SceneView(termin::visual::TcVisualScene scene)
        : NativeWidget("SceneView"),
          scene_(scene.handle()) {
        projections_.set_source_resolver(
            [this](GraphicItemHandle handle) -> std::optional<WidgetSceneProjectionSource> {
                const auto current_scene = this->scene();
                const tc_graphic_item* item = current_scene.resolve(handle);
                if (!item)
                    return std::nullopt;
                const auto local_bounds = current_scene.local_bounds(*item);
                if (!local_bounds)
                    return std::nullopt;
                return WidgetSceneProjectionSource{
                    {local_bounds->x0, local_bounds->y0, local_bounds->x1, local_bounds->y1},
                    current_scene.world_transform(*item),
                    current_scene.effective_visible(*item),
                    current_scene.effective_enabled(*item),
                };
            });
        set_style_role(TC_UI_STYLE_PANEL);
        set_focusable(true);
        set_preferred_size({480.0f, 320.0f});
    }

    void SceneView::invalidate_scene() {
        mark_dirty(TC_WIDGET_DIRTY_LAYOUT | TC_WIDGET_DIRTY_PAINT | TC_WIDGET_DIRTY_STATE);
    }

    void SceneView::set_scene(termin::visual::TcVisualScene scene) {
        scene_ = scene.handle();
        clear_widget_portals();
        invalidate_scene();
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
        sync_portal_transforms();
        mark_dirty(TC_WIDGET_DIRTY_LAYOUT | TC_WIDGET_DIRTY_PAINT | TC_WIDGET_DIRTY_STATE);
        emit_transform_changed();
    }

    void SceneView::set_zoom_range(float minimum, float maximum) {
        if (!std::isfinite(minimum) || !std::isfinite(maximum) || minimum <= 0.0f || maximum < minimum) {
            tc_log_error("[termin-gui-native] SceneView rejected invalid zoom range");
            throw std::invalid_argument("scene zoom range must be finite, positive and ordered");
        }
        min_zoom_ = minimum;
        max_zoom_ = maximum;
        const float clamped = detail::clamp_float(zoom_, minimum, maximum);
        if (clamped != zoom_) {
            zoom_ = clamped;
            sync_portal_transforms();
            emit_transform_changed();
        }
        invalidate_scene();
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
        sync_portal_transforms();
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

    void SceneView::set_scene_colors(SrgbColor background, SrgbColor grid, SrgbColor axes) {
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

    bool SceneView::set_widget_portal(GraphicItemHandle item, tc_widget_handle widget) {
        const bool result = projections_.set_projection(item, widget);
        if (result)
            invalidate_scene();
        return result;
    }

    bool SceneView::clear_widget_portal(GraphicItemHandle item) {
        const bool result = projections_.clear_projection(item);
        if (result)
            invalidate_scene();
        return result;
    }

    void SceneView::clear_widget_portals() {
        if (projections_.size() == 0)
            return;
        projections_.clear();
        invalidate_scene();
    }

    void SceneView::sync_portal_transforms() {
        const tc_ui_document_handle document = c_widget()->document;
        if (tc_ui_document_handle_is_invalid(document))
            return;
        const SceneTransform current = transform();
        projections_.bind_target(document, handle());
        projections_.set_camera(
            tc_affine2f_new(current.zoom, 0.0f, 0.0f, current.zoom, current.origin_x, current.origin_y));
    }

    tc_ui_size SceneView::measure(tc_ui_document_handle, tc_ui_constraints constraints) {
        return detail::clamp_size(preferred_size(), constraints);
    }

    void SceneView::layout(tc_ui_document_handle document, tc_ui_rect rect) {
        NativeWidget::layout(document, rect);
        projections_.bind_target(document, handle());
        sync_portal_transforms();
    }

    void SceneView::paint(tc_ui_document_handle document, tc_ui_paint_context* context) {
        projections_.bind_target(document, handle());
        projections_.reconcile();
        tc_ui_painter_fill_rect(context, bounds(), to_tc_ui_srgb(background_));
        tc_ui_painter_push_clip(context, bounds());
        if (show_grid_ && grid_step_ > 0.0f) {
            const tc_ui_point world_min = screen_to_world({bounds().x, bounds().y});
            const tc_ui_point world_max = screen_to_world({
                bounds().x + bounds().width,
                bounds().y + bounds().height,
            });
            const double first_x = std::floor(world_min.x / grid_step_) * grid_step_;
            const double first_y = std::floor(world_min.y / grid_step_) * grid_step_;
            constexpr std::size_t max_lines = 10000;
            std::size_t lines = 0;
            for (double x = first_x; x <= world_max.x + grid_step_ && lines < max_lines; x += grid_step_, ++lines) {
                const float screen_x = world_to_screen({static_cast<float>(x), 0.0f}).x;
                tc_ui_painter_draw_line(context,
                                        {screen_x, bounds().y},
                                        {screen_x, bounds().y + bounds().height},
                                        to_tc_ui_srgb(std::fabs(x) < 0.0001 ? axes_ : grid_),
                                        1.0f);
            }
            lines = 0;
            for (double y = first_y; y <= world_max.y + grid_step_ && lines < max_lines; y += grid_step_, ++lines) {
                const float screen_y = world_to_screen({0.0f, static_cast<float>(y)}).y;
                tc_ui_painter_draw_line(context,
                                        {bounds().x, screen_y},
                                        {bounds().x + bounds().width, screen_y},
                                        to_tc_ui_srgb(std::fabs(y) < 0.0001 ? axes_ : grid_),
                                        1.0f);
            }
        }

        UiSceneResources resources;
        const auto current = transform();
        const termin::Affine2f camera{
            current.zoom,
            0.0f,
            0.0f,
            current.zoom,
            current.origin_x,
            current.origin_y,
        };
        const auto current_scene = scene();
        if (current_scene.valid()) {
            if (projections_.size() != 0) {
                SceneViewLayerSink sink(context, projections_, camera);
                if (!current_scene.paint_layers(sink, resources))
                    tc_log_error("[termin-gui-native] SceneView failed to paint interleaved visual scene");
            } else {
                tgfx::DrawList2DBuilder builder;
                const bool built = builder.push_transform(camera) && current_scene.paint(builder, resources) &&
                                   builder.pop_transform();
                auto draw_list = built ? builder.freeze() : std::nullopt;
                if (!draw_list || !append_draw_list2d(context, std::move(*draw_list)))
                    tc_log_error("[termin-gui-native] SceneView failed to paint visual scene");
            }
        }
        tc_ui_painter_pop_clip(context);
    }

    void SceneView::emit_transform_changed() {
        const SceneTransform current = transform();
        transform_changed_.emit(*this, current);
    }

    tc_ui_event_result SceneView::pointer_event(tc_ui_document_handle document, const tc_ui_pointer_event* event) {
        if (!event)
            return TC_UI_EVENT_IGNORED;
        const bool captured = tc_widget_handle_eq(tc_ui_document_pointer_capture(document), handle());
        const tc_ui_point world = screen_to_world({event->x, event->y});
        bool domain_handled = false;
        if (pointer_handler_) {
            try {
                domain_handled = pointer_handler_(*this, world, *event);
            } catch (const std::exception& error) {
                tc_log_error("[termin-gui-native] SceneView pointer handler failed: %s", error.what());
            } catch (...) {
                tc_log_error("[termin-gui-native] SceneView pointer handler failed");
            }
        }

        if (event->type == TC_UI_POINTER_CANCEL) {
            const bool active = panning_ || captured || domain_handled;
            panning_ = false;
            if (captured) {
                tc_ui_document_release_pointer_capture(document, handle());
            }
            if (active)
                invalidate_scene();
            return active ? TC_UI_EVENT_HANDLED : TC_UI_EVENT_IGNORED;
        }
        if (domain_handled) {
            if (event->type == TC_UI_POINTER_DOWN) {
                tc_ui_document_set_focus(document, handle());
                tc_ui_document_set_pointer_capture(document, handle());
            } else if (event->type == TC_UI_POINTER_UP) {
                tc_ui_document_release_pointer_capture(document, handle());
            }
            invalidate_scene();
            return TC_UI_EVENT_HANDLED;
        }
        if (event->type == TC_UI_POINTER_WHEEL && detail::rect_contains(bounds(), event->x, event->y)) {
            const float factor = event->wheel_y > 0.0f ? zoom_factor_ : 1.0f / zoom_factor_;
            set_zoom(zoom_ * factor, {event->x, event->y});
            return TC_UI_EVENT_HANDLED;
        }
        if (event->type == TC_UI_POINTER_DOWN &&
            event->button == tcbase::mouse_button_value(tcbase::MouseButton::MIDDLE) &&
            detail::rect_contains(bounds(), event->x, event->y)) {
            panning_ = true;
            pan_start_ = {event->x, event->y};
            pan_start_offset_ = offset_;
            tc_ui_document_set_focus(document, handle());
            tc_ui_document_set_pointer_capture(document, handle());
            return TC_UI_EVENT_HANDLED;
        }
        if (event->type == TC_UI_POINTER_MOVE && panning_) {
            set_offset({
                pan_start_offset_.x + event->x - pan_start_.x,
                pan_start_offset_.y + event->y - pan_start_.y,
            });
            return TC_UI_EVENT_HANDLED;
        }
        if (event->type == TC_UI_POINTER_UP && (panning_ || captured)) {
            panning_ = false;
            tc_ui_document_release_pointer_capture(document, handle());
            return TC_UI_EVENT_HANDLED;
        }
        return TC_UI_EVENT_IGNORED;
    }

    tc_ui_event_result SceneView::key_event(tc_ui_document_handle, const tc_ui_key_event* event) {
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

    tc_ui_event_result SceneView::text_event(tc_ui_document_handle, const tc_ui_text_event* event) {
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

    tc_widget_handle SceneView::hit_test(tc_ui_document_handle document, float x, float y) {
        if (!visible() || !detail::rect_contains(bounds(), x, y)) {
            return tc_widget_handle_invalid();
        }
        projections_.bind_target(document, handle());
        const auto current_scene = scene();
        if (current_scene.valid() && projections_.size() != 0) {
            tc_widget_handle result = tc_widget_handle_invalid();
            const tc_ui_point world = screen_to_world({x, y});
            current_scene.visit_hit_layers({world.x, world.y},
                                           [&](const tc_graphic_item& item, termin::Vec2f, bool item_hit) {
                                               result = projections_.hit_test_source(item.handle, x, y);
                                               if (!tc_widget_handle_is_invalid(result))
                                                   return true;
                                               return item_hit;
                                           });
            if (!tc_widget_handle_is_invalid(result))
                return result;
        }
        return mouse_transparent() ? tc_widget_handle_invalid() : handle();
    }

    void SceneView::on_destroy(tc_ui_document_handle document) {
        projections_.bind_target(document, handle());
        projections_.clear();
        pointer_handler_ = {};
        key_handler_ = {};
        text_handler_ = {};
        transform_changed_ = {};
    }

} // namespace termin::gui_native
