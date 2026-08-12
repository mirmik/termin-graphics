#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

#include <geom/tc_affine2.h>
#include <termin/gui_native/tc_ui_document.h>
#include <termin_visual_scene/tc_graphic_item.h>

namespace termin::gui_native {

    enum class WidgetSceneProjectionOrder {
        AfterSourceScene,
    };

    enum class WidgetSceneProjectionClip {
        HostBounds,
    };

    enum class WidgetSceneProjectionInput {
        PortalFirst,
    };

    struct WidgetSceneProjectionPolicy {
        WidgetSceneProjectionOrder order = WidgetSceneProjectionOrder::AfterSourceScene;
        WidgetSceneProjectionClip clip = WidgetSceneProjectionClip::HostBounds;
        WidgetSceneProjectionInput input = WidgetSceneProjectionInput::PortalFirst;
    };

    struct WidgetSceneProjectionSource {
        tc_bounds2f local_bounds{};
        tc_affine2f local_to_world = tc_affine2f_identity();
        std::int64_t z_order = 0;
        std::uint64_t stable_order = 0;
        bool visible = true;
        bool enabled = true;
    };

    // Non-owning bridge between generation-checked GraphicItem and Widget
    // handles. The source and target owners retain their normal lifetimes.
    class WidgetSceneProjectionBridge {
    public:
        using SourceResolver =
            std::function<std::optional<WidgetSceneProjectionSource>(tc_graphic_item_handle)>;

        explicit WidgetSceneProjectionBridge(SourceResolver resolver = {});
        ~WidgetSceneProjectionBridge();

        WidgetSceneProjectionBridge(const WidgetSceneProjectionBridge&) = delete;
        WidgetSceneProjectionBridge& operator=(const WidgetSceneProjectionBridge&) = delete;

        void set_source_resolver(SourceResolver resolver);
        void bind_target(tc_ui_document_handle document, tc_widget_handle host);
        void set_camera(tc_affine2f world_to_parent);
        void set_policy(WidgetSceneProjectionPolicy policy);
        WidgetSceneProjectionPolicy policy() const;

        bool set_projection(tc_graphic_item_handle source, tc_widget_handle target);
        bool clear_projection(tc_graphic_item_handle source);
        void clear();
        std::size_t size() const;

        void reconcile();
        void layout();
        void paint(tc_ui_paint_context* context);
        tc_widget_handle hit_test(float parent_x, float parent_y) const;
        void detach_all();

    private:
        struct Entry {
            tc_graphic_item_handle source = tc_graphic_item_handle_invalid();
            tc_widget_handle target = tc_widget_handle_invalid();
        };

        SourceResolver resolver_;
        tc_ui_document_handle document_ = tc_ui_document_handle_invalid();
        tc_widget_handle host_ = tc_widget_handle_invalid();
        tc_affine2f camera_ = tc_affine2f_identity();
        WidgetSceneProjectionPolicy policy_{};
        std::vector<Entry> entries_;

        void detach_target(tc_widget_handle target) const;
    };

} // namespace termin::gui_native
