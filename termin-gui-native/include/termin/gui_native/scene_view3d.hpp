#pragma once

#include <functional>
#include <memory>
#include <optional>

#include <termin/geom/color.hpp>
#include <termin/gui_native/native_widget.hpp>
#include <termin/gui_native/render_prepared_widget.hpp>
#include <termin/gui_native/render_surface_size.hpp>
#include <termin_visual_scene/interaction3d.hpp>
#include <termin_visual_scene/paint3d.hpp>

namespace termin::gui_native {

    struct SceneView3DCamera {
        tc_mat44 view_matrix{};
        tc_mat44 projection_matrix{};
        tc_vec3 world_position{};
    };

    class SceneView3D final : public NativeWidget, public RenderPreparedWidget {
    public:
        using CameraProvider = std::function<std::optional<SceneView3DCamera>(ViewportSurfaceSize)>;
        using FallbackPointerHandler =
            std::function<bool(SceneView3D&, const tc_ui_pointer_event&, const std::optional<termin::Ray3>&)>;

        // The scene is borrowed. The caller must keep it alive until set_scene({})
        // or widget destruction; SceneView3D never destroys the scene.
        explicit SceneView3D(termin::visual::TcVisualScene3D scene = {});
        ~SceneView3D() override;

        termin::visual::TcVisualScene3D scene() const;
        void set_scene(termin::visual::TcVisualScene3D scene);
        void invalidate_scene();

        void set_camera(SceneView3DCamera camera);
        const SceneView3DCamera& camera() const;
        void set_camera_provider(CameraProvider provider);
        void invalidate_view();

        ViewportSurfaceSize framebuffer_size() const;
        uint32_t texture_id() const;
        std::optional<termin::Ray3> world_ray(float widget_x, float widget_y) const;

        termin::visual::SceneInteraction3D& interaction();
        const termin::visual::SceneInteraction3D& interaction() const;
        void set_fallback_pointer_handler(FallbackPointerHandler handler);
        void set_clear_color(termin::LinearColor color);

        tc_ui_size measure(tc_ui_document_handle document, tc_ui_constraints constraints) override;
        void layout(tc_ui_document_handle document, tc_ui_rect rect) override;
        void paint(tc_ui_document_handle document, tc_ui_paint_context* context) override;
        tc_ui_event_result pointer_event(tc_ui_document_handle document, const tc_ui_pointer_event* event) override;
        void on_destroy(tc_ui_document_handle document) override;

        void prepare_render(tgfx::RenderContext2& context, tgfx::FontAtlas& default_font, float density_scale) override;
        void release_render_resources() override;

    private:
        struct RenderState;

        bool sync_framebuffer_size();
        bool update_camera_from_provider();
        bool call_fallback(const tc_ui_pointer_event& event, const std::optional<termin::Ray3>& ray);
        void cancel_pointer(tc_ui_document_handle document, const tc_ui_pointer_event& event);

        tc_visual_scene3d_handle scene_ = tc_visual_scene3d_handle_invalid();
        SceneView3DCamera camera_{};
        CameraProvider camera_provider_;
        FallbackPointerHandler fallback_pointer_handler_;
        termin::visual::SceneInteraction3D interaction_;
        std::unique_ptr<RenderState> render_state_;
        ViewportSurfaceSize requested_size_{};
        termin::LinearColor clear_color_{0.06f, 0.07f, 0.09f, 1.0f};
        bool render_dirty_ = true;
        bool scene_pointer_active_ = false;
        bool fallback_pointer_active_ = false;
        int32_t active_button_ = 0;
    };

} // namespace termin::gui_native
