// plot_annotations2d.hpp - Retained semantic annotations for 2D plots.
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include <termin/geom/color.hpp>
#include <termin/geom/vec2.hpp>
#include <termin_visual_scene/graphic_item2d.hpp>
#include <termin_visual_scene/interaction2d.hpp>

#include "tcplot/plot_data.hpp"
#include "tcplot/plot_frame2d.hpp"
#include "tcplot/tcplot_api.h"

namespace tcplot {

    inline constexpr std::string_view kPlotDefaultFontResource2D = "tcplot://default-font";

    struct PlotAnnotationHandle {
        std::uint64_t layer_id = 0;
        std::uint32_t index = UINT32_MAX;
        std::uint32_t generation = 0;

        bool valid() const noexcept {
            return layer_id != 0 && index != UINT32_MAX && generation != 0;
        }
        friend bool operator==(const PlotAnnotationHandle&, const PlotAnnotationHandle&) = default;
    };

    struct DataAnchor2D {
        double x = 0.0;
        double y = 0.0;
    };

    enum class PlotSeriesKind2D : std::uint8_t {
        Line,
        Scatter,
    };

    struct SeriesPointRef2D {
        PlotSeriesKind2D series_kind = PlotSeriesKind2D::Line;
        std::size_t series_index = 0;
        std::size_t point_index = 0;
    };

    // X grows left-to-right. Y grows bottom-to-top, matching data coordinates.
    struct AxesFractionAnchor2D {
        double x = 0.0;
        double y = 0.0;
    };

    // Physical pixels relative to PlotFrame2D::viewport().
    struct ViewportPixelAnchor2D {
        float x = 0.0f;
        float y = 0.0f;
    };

    using PlotAnchor2D = std::variant<DataAnchor2D, SeriesPointRef2D, AxesFractionAnchor2D, ViewportPixelAnchor2D>;

    enum class PlotAnnotationPhase2D : std::uint8_t {
        Underlay,
        Overlay,
        Chrome,
    };

    enum class PlotAnnotationClip2D : std::uint8_t {
        PlotArea,
        Viewport,
    };

    struct PlotAnnotationVisual2D {
        std::unique_ptr<termin::visual::GraphicItem2D> item;
        termin::Vec2f pixel_offset{};
        PlotAnnotationPhase2D phase = PlotAnnotationPhase2D::Overlay;
        PlotAnnotationClip2D clip = PlotAnnotationClip2D::PlotArea;
        std::int64_t z_order = 0;
        bool visible = true;
        bool enabled = true;
        std::string action = "activate";
    };

    struct PlotAnnotation2D {
        PlotAnchor2D anchor = DataAnchor2D{};
        std::vector<PlotAnnotationVisual2D> visuals;

        PlotAnnotation2D() = default;
        PlotAnnotation2D(PlotAnchor2D anchor_value, PlotAnnotationVisual2D visual)
            : anchor(std::move(anchor_value)) {
            visuals.push_back(std::move(visual));
        }
    };

    struct ProjectedPlotGraphic2D {
        termin::visual::GraphicItemHandle item = tc_graphic_item_handle_invalid();
        std::size_t visual_index = 0;
        PlotAnnotationPhase2D phase = PlotAnnotationPhase2D::Overlay;
        PlotAnnotationClip2D clip = PlotAnnotationClip2D::PlotArea;
    };

    struct PlotAnnotationSnapshot2D {
        PlotAnnotationHandle handle;
        PlotAnchor2D anchor;
        std::optional<PlotPixelPoint2D> projected_anchor;
        std::vector<ProjectedPlotGraphic2D> projected_graphics;
    };

    struct PlotAnnotationAction2D {
        PlotAnnotationHandle annotation;
        std::size_t visual_index = 0;
        termin::visual::PointerId2D pointer = 0;
        std::string action;
    };

    struct PlotDataMarker2D {
        PlotPoint2D data_position{};
        std::string text;
        termin::Vec2f callout_offset{54.0f, -46.0f};
        float callout_width = 150.0f;
        float callout_height = 44.0f;
        float anchor_radius = 6.0f;
        float text_size = 14.0f;
        bool close_button = true;
        termin::SrgbColor anchor_color{0.95f, 0.55f, 0.15f, 1.0f};
        termin::SrgbColor hover_color{1.0f, 0.72f, 0.25f, 1.0f};
        termin::SrgbColor callout_color{0.10f, 0.13f, 0.18f, 0.96f};
        termin::SrgbColor border_color{0.78f, 0.82f, 0.90f, 1.0f};
        termin::SrgbColor text_color{0.96f, 0.97f, 1.0f, 1.0f};
    };

    struct PlotDataMarkerSnapshot2D {
        PlotAnnotationHandle annotation;
        PlotDataMarker2D marker;
        bool hovered = false;
        bool dragging = false;
    };

    // Detached, value-only forms used by foreign-language bindings.
    struct PlotDataMarkerBindingSnapshot2D {
        bool available = false;
        PlotAnnotationHandle annotation;
        double x = 0.0;
        double y = 0.0;
        std::string text;
        bool hovered = false;
        bool dragging = false;
    };

    struct PlotAnnotationActionPoll2D {
        bool available = false;
        PlotAnnotationHandle annotation;
        std::string action;
    };

    class TCPLOT_API PlotAnnotationLayer2D final {
    public:
        using SnapHook = std::function<PlotPoint2D(const PlotPoint2D&)>;
        using ActionHandler = std::function<void(const PlotAnnotationAction2D&)>;

        PlotAnnotationLayer2D();
        ~PlotAnnotationLayer2D();

        PlotAnnotationLayer2D(const PlotAnnotationLayer2D&) = delete;
        PlotAnnotationLayer2D& operator=(const PlotAnnotationLayer2D&) = delete;

        std::optional<PlotAnnotationHandle> create(PlotAnnotation2D annotation);
        bool update(PlotAnnotationHandle handle, PlotAnnotation2D annotation);
        bool destroy(PlotAnnotationHandle handle);
        void clear();

        std::optional<PlotAnnotationSnapshot2D> snapshot(PlotAnnotationHandle handle) const;
        std::vector<PlotAnnotationSnapshot2D> snapshots() const;
        std::size_t size() const;

        bool set_snap_hook(PlotAnnotationHandle handle, SnapHook hook);
        std::optional<PlotPoint2D> snap_data(PlotAnnotationHandle handle, PlotPoint2D candidate) const;
        bool set_action_handler(PlotAnnotationHandle handle, ActionHandler handler);
        std::optional<PlotAnnotationAction2D> take_action();

        std::optional<PlotAnnotationHandle> create_data_marker(PlotDataMarker2D marker);
        bool update_data_marker(PlotAnnotationHandle handle, PlotDataMarker2D marker);
        std::optional<PlotDataMarkerSnapshot2D> data_marker_snapshot(PlotAnnotationHandle handle) const;

        // Reprojects live items in place. Invalid SeriesPointRef anchors hide the
        // existing items while preserving both semantic and graphic handles.
        void project(const PlotFrame2D& frame, const PlotData& data);

        // Routes through the annotation scenes front-to-back. Call before plot
        // pan/zoom fallback. Plot-area clipped visuals are not hittable outside
        // frame.plot_area().
        bool route_pointer(const PlotFrame2D& frame, const termin::visual::PointerEvent2D& event);
        bool hit_test(const PlotFrame2D& frame, float x, float y) const;

        void render_phase(PlotRenderPhase2D phase,
                          const PlotFrame2D& frame,
                          tgfx::RenderContext2& context,
                          tgfx::FontAtlas* font);
        void release_gpu_resources();

        const termin::visual::TcVisualScene& visual_scene(PlotAnnotationPhase2D phase, PlotAnnotationClip2D clip) const;

    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };

} // namespace tcplot
