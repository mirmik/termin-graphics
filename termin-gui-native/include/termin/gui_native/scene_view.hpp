#pragma once

#include <functional>
#include <memory>
#include <vector>

#include <termin/gui_native/native_widget.hpp>
#include <termin/gui_native/signal.hpp>
#include <termin/gui_native/widget_types.hpp>
#include <termin_visual_scene/scene2d.hpp>

namespace termin::gui_native {

struct SceneTransform {
    float origin_x = 0.0f;
    float origin_y = 0.0f;
    float zoom = 1.0f;

    tc_ui_point world_to_screen(tc_ui_point point) const;
    tc_ui_point screen_to_world(tc_ui_point point) const;
};

class SceneView : public NativeWidget {
public:
    using PointerHandler =
        std::function<bool(
            SceneView&,
            tc_ui_point,
            const tc_ui_pointer_event&)>;
    using KeyHandler =
        std::function<bool(SceneView&, const tc_ui_key_event&)>;
    using TextHandler =
        std::function<bool(SceneView&, const tc_ui_text_event&)>;

private:
    struct WidgetPortal {
        termin::visual::GraphicItemHandle item =
            tc_graphic_item_handle_invalid();
        tc_widget_handle widget = tc_widget_handle_invalid();
    };

    tc_visual_scene_handle scene_ =
        tc_visual_scene_handle_invalid();
    float zoom_ = 1.0f;
    float min_zoom_ = 0.1f;
    float max_zoom_ = 4.0f;
    float zoom_factor_ = 1.15f;
    tc_ui_point offset_{};
    bool show_grid_ = true;
    float grid_step_ = 40.0f;
    Color background_{0.10f, 0.11f, 0.13f, 1.0f};
    Color grid_{0.17f, 0.19f, 0.24f, 1.0f};
    Color axes_{0.30f, 0.33f, 0.42f, 1.0f};
    bool panning_ = false;
    tc_ui_point pan_start_{};
    tc_ui_point pan_start_offset_{};
    std::vector<WidgetPortal> portals_;
    PointerHandler pointer_handler_;
    KeyHandler key_handler_;
    TextHandler text_handler_;
    Signal<SceneView&, const SceneTransform&> transform_changed_;

public:
    explicit SceneView(
        termin::visual::TcVisualScene scene = {});
    ~SceneView() override = default;

    termin::visual::TcVisualScene scene() const {
        return termin::visual::TcVisualScene{scene_};
    }
    void set_scene(termin::visual::TcVisualScene scene);
    void invalidate_scene();

    float zoom() const { return zoom_; }
    void set_zoom(float zoom, tc_ui_point anchor);
    float min_zoom() const { return min_zoom_; }
    float max_zoom() const { return max_zoom_; }
    void set_zoom_range(float minimum, float maximum);
    float zoom_factor() const { return zoom_factor_; }
    void set_zoom_factor(float factor);
    tc_ui_point offset() const { return offset_; }
    void set_offset(tc_ui_point offset);
    bool show_grid() const { return show_grid_; }
    void set_show_grid(bool show);
    float grid_step() const { return grid_step_; }
    void set_grid_step(float step);
    void set_scene_colors(Color background, Color grid, Color axes);

    SceneTransform transform() const;
    tc_ui_point world_to_screen(tc_ui_point point) const;
    tc_ui_point screen_to_world(tc_ui_point point) const;

    bool set_widget_portal(
        termin::visual::GraphicItemHandle item,
        tc_widget_handle widget);
    bool clear_widget_portal(
        termin::visual::GraphicItemHandle item);
    void clear_widget_portals();

    void set_pointer_handler(PointerHandler handler) {
        pointer_handler_ = std::move(handler);
    }
    void set_key_handler(KeyHandler handler) {
        key_handler_ = std::move(handler);
    }
    void set_text_handler(TextHandler handler) {
        text_handler_ = std::move(handler);
    }

    Signal<SceneView&, const SceneTransform&>& transform_changed() {
        return transform_changed_;
    }

    tc_ui_size measure(
        tc_ui_document_handle document,
        tc_ui_constraints constraints) override;
    void layout(
        tc_ui_document_handle document,
        tc_ui_rect rect) override;
    void paint(
        tc_ui_document_handle document,
        tc_ui_paint_context* context) override;
    tc_ui_event_result pointer_event(
        tc_ui_document_handle document,
        const tc_ui_pointer_event* event) override;
    tc_ui_event_result key_event(
        tc_ui_document_handle document,
        const tc_ui_key_event* event) override;
    tc_ui_event_result text_event(
        tc_ui_document_handle document,
        const tc_ui_text_event* event) override;
    tc_widget_handle hit_test(
        tc_ui_document_handle document,
        float x,
        float y) override;
    void on_destroy(tc_ui_document_handle document) override;

private:
    void reconcile_portals(tc_ui_document_handle document);
    void layout_portals(tc_ui_document_handle document);
    void paint_portals(
        tc_ui_document_handle document,
        tc_ui_paint_context* context);
    tc_widget_handle hit_test_portals(
        tc_ui_document_handle document,
        float x,
        float y) const;
    void emit_transform_changed();
};

} // namespace termin::gui_native
