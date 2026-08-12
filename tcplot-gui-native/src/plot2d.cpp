#include <tcplot/gui_native/plot2d.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

#include <tcbase/tc_log.hpp>
#include <tcplot/plot_grid_item2d.hpp>
#include <tcplot/plot_annotations2d.hpp>
#include <tcplot/plot_data.hpp>
#include <tcplot/plot_layout2d.hpp>
#include <tcplot/styles.hpp>
#include <termin/gui_native/draw_list2d_bridge.hpp>
#include <termin/gui_native/tc_document.hpp>
#include <termin_visual_scene/scene_render2d.hpp>

namespace tcplot::gui_native {
    namespace {

        constexpr float preferred_width = 520.0f;
        constexpr float preferred_height = 260.0f;
        constexpr float tick_font_size = 10.0f;
        constexpr float label_font_size = 11.0f;
        constexpr float title_font_size = 13.0f;

        tc_ui_srgb_color ui_color(termin::SrgbColor color) {
            return {color.r, color.g, color.b, color.a};
        }

        termin::SrgbColor visual_color(termin::SrgbColor color) {
            return color;
        }

        tc_ui_size clamp_size(tc_ui_size size, tc_ui_constraints constraints) {
            return {
                std::clamp(size.width, constraints.min_size.width, constraints.max_size.width),
                std::clamp(size.height, constraints.min_size.height, constraints.max_size.height),
            };
        }

        class UiSceneResources final : public termin::visual::SceneRenderResourceResolver2D {
        public:
            std::optional<tgfx::FontHandle> resolve_font(std::string_view uri) override {
                if (uri == "ui://default-font" || uri == kPlotDefaultFontResource2D) {
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

        float text_width(tc_ui_document_handle document, const std::string& text, float font_size) {
            tc_ui_text_metrics metrics{};
            if (tc_ui_document_measure_text(document, text.c_str(), text.size(), font_size, &metrics)) {
                return metrics.width;
            }
            return static_cast<float>(text.size()) * font_size * 0.55f;
        }

    } // namespace

    struct Plot2D::Impl {
        termin::visual::TcVisualScene scene;
        PlotProjection2D projection;
        termin::visual::GraphicItemHandle grid_handle = tc_graphic_item_handle_invalid();
        std::vector<termin::visual::GraphicItemHandle> line_handles;
        std::vector<termin::visual::GraphicItemHandle> scatter_handles;
        PlotData data;
        PlotAnnotationLayer2D annotations;
        PlotFrame2D frame;
        PlotTicks2D ticks;
        PlotRange2D range{0.0, 1.0, 0.0, 1.0};
        std::string title;
        std::string x_label;
        std::string y_label;
        bool auto_fit = true;

        Impl()
            : scene(tc_visual_scene_create()) {
            if (!scene.valid()) {
                throw std::runtime_error("Plot2D failed to create its visual scene");
            }
            frame = PlotFrame2D{
                PlotRect2D{0.0f, 0.0f, preferred_width, preferred_height},
                PlotRect2D{52.0f, 28.0f, 450.0f, 194.0f},
                range,
                PlotRect2D{52.0f, 28.0f, 450.0f, 194.0f},
                1.0f,
            };
            const auto created_projection = create_plot_projection2d(scene, frame);
            if (!created_projection) {
                tc_visual_scene_destroy(scene.handle());
                scene = {};
                throw std::runtime_error("Plot2D failed to create its projection");
            }
            projection = *created_projection;
            const auto grid = adopt_plot_grid_item2d(
                scene, projection, {}, {}, PlotGridStyle2D{visual_color(styles::grid_color()), 1.0f});
            if (!grid) {
                destroy_plot_projection2d(projection);
                tc_visual_scene_destroy(scene.handle());
                projection = {};
                scene = {};
                throw std::runtime_error("Plot2D failed to create its grid");
            }
            grid_handle = *grid;
            if (auto* item = resolve_plot_grid_item2d(scene, grid_handle)) {
                item->set_z_order(0);
            }
        }

        ~Impl() {
            if (scene.valid()) {
                scene.clear();
            }
            if (projection.valid()) {
                destroy_plot_projection2d(projection);
            }
            if (scene.valid()) {
                tc_visual_scene_destroy(scene.handle());
            }
        }
    };

    Plot2D::Plot2D()
        : NativeWidget("Plot2D"),
          impl_(std::make_unique<Impl>()) {
        set_style_role(TC_UI_STYLE_PANEL);
        set_preferred_size({preferred_width, preferred_height});
        set_mouse_transparent(false);
        update_chart_layout();
    }

    Plot2D::~Plot2D() = default;

    const std::string& Plot2D::title() const noexcept {
        return impl_->title;
    }

    void Plot2D::set_title(std::string title) {
        if (impl_->title == title) {
            return;
        }
        impl_->title = std::move(title);
        update_chart_layout();
        invalidate_chart();
    }

    const std::string& Plot2D::x_label() const noexcept {
        return impl_->x_label;
    }

    void Plot2D::set_x_label(std::string label) {
        if (impl_->x_label == label) {
            return;
        }
        impl_->x_label = std::move(label);
        update_chart_layout();
        invalidate_chart();
    }

    const std::string& Plot2D::y_label() const noexcept {
        return impl_->y_label;
    }

    void Plot2D::set_y_label(std::string label) {
        if (impl_->y_label == label) {
            return;
        }
        impl_->y_label = std::move(label);
        update_chart_layout();
        invalidate_chart();
    }

    bool Plot2D::auto_fit() const noexcept {
        return impl_->auto_fit;
    }

    void Plot2D::set_auto_fit(bool enabled) {
        if (impl_->auto_fit == enabled) {
            return;
        }
        impl_->auto_fit = enabled;
        if (enabled) {
            update_auto_range();
        }
        update_chart_layout();
        invalidate_chart();
    }

    void Plot2D::set_view(double x_min, double x_max, double y_min, double y_max) {
        if (!std::isfinite(x_min) || !std::isfinite(x_max) || !std::isfinite(y_min) || !std::isfinite(y_max) ||
            x_max <= x_min || y_max <= y_min) {
            tc::Log::error("[Plot2D] rejected invalid view range");
            return;
        }
        impl_->auto_fit = false;
        impl_->range = PlotRange2D{x_min, x_max, y_min, y_max};
        update_chart_layout();
        invalidate_chart();
    }

    std::size_t Plot2D::line_count() const noexcept {
        return impl_->line_handles.size();
    }

    std::size_t Plot2D::add_line() {
        PlotLineSeriesStyle2D style;
        style.color = styles::cycle_color(static_cast<std::uint32_t>(impl_->line_handles.size()));
        return add_line(style);
    }

    std::size_t Plot2D::add_line(PlotLineSeriesStyle2D style) {
        const auto handle = adopt_plot_line_series_item2d(impl_->scene, impl_->projection, {}, {}, {}, style);
        if (!handle) {
            tc::Log::error("[Plot2D] failed to add line series");
            return std::numeric_limits<std::size_t>::max();
        }
        impl_->line_handles.push_back(*handle);
        impl_->data.add_line({}, {}, {}, style.color, style.thickness_px);
        if (auto* line = resolve_plot_line_series_item2d(impl_->scene, *handle)) {
            line->set_z_order(10 + static_cast<std::int64_t>(impl_->line_handles.size()));
        }
        invalidate_chart();
        return impl_->line_handles.size() - 1;
    }

    bool Plot2D::set_line_data(std::size_t index, std::span<const double> x, std::span<const double> y) {
        if (index >= impl_->line_handles.size()) {
            tc::Log::error("[Plot2D] line index is out of range");
            return false;
        }
        auto* line = resolve_plot_line_series_item2d(impl_->scene, impl_->line_handles[index]);
        if (line == nullptr ||
            !line->set_data(std::vector<double>(x.begin(), x.end()), std::vector<double>(y.begin(), y.end()))) {
            return false;
        }
        impl_->data.lines[index].x.assign(x.begin(), x.end());
        impl_->data.lines[index].y.assign(y.begin(), y.end());
        if (impl_->auto_fit) {
            update_auto_range();
        }
        update_chart_layout();
        invalidate_chart();
        return true;
    }

    bool Plot2D::append_line_data(std::size_t index, std::span<const double> x, std::span<const double> y) {
        if (index >= impl_->line_handles.size()) {
            tc::Log::error("[Plot2D] line index is out of range");
            return false;
        }
        auto* line = resolve_plot_line_series_item2d(impl_->scene, impl_->line_handles[index]);
        if (line == nullptr || !line->append(x, y)) {
            return false;
        }
        impl_->data.lines[index].x.insert(impl_->data.lines[index].x.end(), x.begin(), x.end());
        impl_->data.lines[index].y.insert(impl_->data.lines[index].y.end(), y.begin(), y.end());
        if (impl_->auto_fit) {
            update_auto_range();
        }
        update_chart_layout();
        invalidate_chart();
        return true;
    }

    void Plot2D::clear_lines() {
        for (const auto handle : impl_->line_handles) {
            impl_->scene.destroy(handle);
        }
        impl_->line_handles.clear();
        impl_->data.lines.clear();
        if (impl_->auto_fit) {
            update_auto_range();
        }
        update_chart_layout();
        invalidate_chart();
    }

    std::size_t Plot2D::scatter_count() const noexcept {
        return impl_->scatter_handles.size();
    }

    std::size_t Plot2D::add_scatter(PlotScatterSeriesStyle2D style) {
        const auto handle = adopt_plot_scatter_series_item2d(impl_->scene, impl_->projection, {}, {}, style);
        if (!handle) {
            tc::Log::error("[Plot2D] failed to add scatter series");
            return std::numeric_limits<std::size_t>::max();
        }
        impl_->scatter_handles.push_back(*handle);
        impl_->data.add_scatter({}, {}, {}, style.color, style.diameter_px);
        if (auto* scatter = resolve_plot_scatter_series_item2d(impl_->scene, *handle)) {
            scatter->set_z_order(100 + static_cast<std::int64_t>(impl_->scatter_handles.size()));
        }
        invalidate_chart();
        return impl_->scatter_handles.size() - 1;
    }

    bool Plot2D::set_scatter_data(std::size_t index, std::span<const double> x, std::span<const double> y) {
        if (index >= impl_->scatter_handles.size()) {
            tc::Log::error("[Plot2D] scatter index is out of range");
            return false;
        }
        auto* scatter = resolve_plot_scatter_series_item2d(impl_->scene, impl_->scatter_handles[index]);
        if (scatter == nullptr ||
            !scatter->set_data(std::vector<double>(x.begin(), x.end()), std::vector<double>(y.begin(), y.end()))) {
            return false;
        }
        impl_->data.scatters[index].x.assign(x.begin(), x.end());
        impl_->data.scatters[index].y.assign(y.begin(), y.end());
        if (impl_->auto_fit) {
            update_auto_range();
        }
        update_chart_layout();
        invalidate_chart();
        return true;
    }

    void Plot2D::clear_scatters() {
        for (const auto handle : impl_->scatter_handles) {
            impl_->scene.destroy(handle);
        }
        impl_->scatter_handles.clear();
        impl_->data.scatters.clear();
        if (impl_->auto_fit) {
            update_auto_range();
        }
        update_chart_layout();
        invalidate_chart();
    }

    PlotAnnotationHandle Plot2D::create_data_marker(double x,
                                                    double y,
                                                    std::string text,
                                                    std::size_t snap_line_index) {
        PlotDataMarker2D marker;
        marker.data_position = {x, y};
        marker.text = std::move(text);
        const auto handle = impl_->annotations.create_data_marker(std::move(marker));
        if (!handle) {
            tc::Log::error("[Plot2D] failed to create data marker");
            return {};
        }
        if (snap_line_index < impl_->data.lines.size()) {
            impl_->annotations.set_snap_hook(*handle, [this, snap_line_index](const PlotPoint2D& candidate) {
                const auto& line = impl_->data.lines[snap_line_index];
                if (line.x.empty()) {
                    return candidate;
                }
                std::size_t nearest = 0;
                double distance = std::numeric_limits<double>::infinity();
                for (std::size_t index = 0; index < line.x.size(); ++index) {
                    const double dx = line.x[index] - candidate.x;
                    const double dy = line.y[index] - candidate.y;
                    const double squared = dx * dx + dy * dy;
                    if (squared < distance) {
                        distance = squared;
                        nearest = index;
                    }
                }
                return PlotPoint2D{line.x[nearest], line.y[nearest]};
            });
        }
        impl_->annotations.project(impl_->frame, impl_->data);
        invalidate_chart();
        return *handle;
    }

    tc_ui_size Plot2D::measure(tc_ui_document_handle, tc_ui_constraints constraints) {
        return clamp_size(preferred_size(), constraints);
    }

    void Plot2D::layout(tc_ui_document_handle document, tc_ui_rect rect) {
        NativeWidget::layout(document, rect);
        update_chart_layout();
    }

    void Plot2D::paint(tc_ui_document_handle document, tc_ui_paint_context* context) {
        if (context == nullptr || !impl_->scene.valid()) {
            return;
        }
        const tc_ui_rect widget_bounds = bounds();
        const PlotRect2D& area = impl_->frame.plot_area();
        const tc_ui_rect global_area{
            widget_bounds.x + area.x(),
            widget_bounds.y + area.y(),
            area.width(),
            area.height(),
        };

        tc_ui_painter_fill_rect(context, widget_bounds, ui_color(styles::bg_color()));
        tc_ui_painter_fill_rect(context, global_area, ui_color(styles::plot_area_bg()));
        tc_ui_painter_push_clip(context, widget_bounds);

        UiSceneResources resources;
        tgfx::DrawList2DBuilder builder;
        const termin::Affine2f translation{
            1.0f,
            0.0f,
            0.0f,
            1.0f,
            widget_bounds.x,
            widget_bounds.y,
        };
        const bool built =
            builder.push_transform(translation) && impl_->scene.paint(builder, resources) && builder.pop_transform();
        auto draw_list = built ? builder.freeze() : std::nullopt;
        if (!draw_list || !termin::gui_native::append_draw_list2d(context, std::move(*draw_list))) {
            tc::Log::error("[Plot2D] failed to paint retained chart items");
        }

        impl_->annotations.project(impl_->frame, impl_->data);
        for (const auto phase : {PlotAnnotationPhase2D::Underlay,
                                 PlotAnnotationPhase2D::Overlay,
                                 PlotAnnotationPhase2D::Chrome}) {
            for (const auto clip : {PlotAnnotationClip2D::PlotArea, PlotAnnotationClip2D::Viewport}) {
                tgfx::DrawList2DBuilder annotation_builder;
                const auto& annotation_scene = impl_->annotations.visual_scene(phase, clip);
                const bool annotation_built = annotation_builder.push_transform(translation) &&
                                              annotation_scene.paint(annotation_builder, resources) &&
                                              annotation_builder.pop_transform();
                auto annotation_list = annotation_built ? annotation_builder.freeze() : std::nullopt;
                if (annotation_list &&
                    !termin::gui_native::append_draw_list2d(context, std::move(*annotation_list))) {
                    tc::Log::error("[Plot2D] failed to paint retained annotations");
                }
            }
        }

        const tc_ui_srgb_color axis = ui_color(styles::axis_color());
        const tc_ui_srgb_color label = ui_color(styles::label_color());
        tc_ui_painter_draw_line(context,
                                {global_area.x, global_area.y + global_area.height},
                                {global_area.x + global_area.width, global_area.y + global_area.height},
                                axis,
                                1.0f);
        tc_ui_painter_draw_line(
            context, {global_area.x, global_area.y}, {global_area.x, global_area.y + global_area.height}, axis, 1.0f);

        for (std::size_t index = 0; index < impl_->ticks.x.values.size(); ++index) {
            const float local_x = impl_->frame.data_to_pixel(impl_->ticks.x.values[index], impl_->range.y_min()).x;
            const float x = widget_bounds.x + local_x;
            tc_ui_painter_draw_line(context,
                                    {x, global_area.y + global_area.height},
                                    {x, global_area.y + global_area.height + 4.0f},
                                    axis,
                                    1.0f);
            const std::string& text = impl_->ticks.x.labels[index];
            const float width = text_width(document, text, tick_font_size);
            tc_ui_painter_draw_text(context,
                                    text.c_str(),
                                    {x - width * 0.5f, global_area.y + global_area.height + 15.0f},
                                    tick_font_size,
                                    label);
        }

        for (std::size_t index = 0; index < impl_->ticks.y.values.size(); ++index) {
            const float local_y = impl_->frame.data_to_pixel(impl_->range.x_min(), impl_->ticks.y.values[index]).y;
            const float y = widget_bounds.y + local_y;
            tc_ui_painter_draw_line(context, {global_area.x - 4.0f, y}, {global_area.x, y}, axis, 1.0f);
            const std::string& text = impl_->ticks.y.labels[index];
            const float width = text_width(document, text, tick_font_size);
            tc_ui_painter_draw_text(
                context, text.c_str(), {global_area.x - width - 7.0f, y + 3.0f}, tick_font_size, label);
        }

        if (!impl_->title.empty()) {
            const float width = text_width(document, impl_->title, title_font_size);
            tc_ui_painter_draw_text(context,
                                    impl_->title.c_str(),
                                    {widget_bounds.x + (widget_bounds.width - width) * 0.5f, widget_bounds.y + 17.0f},
                                    title_font_size,
                                    label);
        }
        if (!impl_->x_label.empty()) {
            const float width = text_width(document, impl_->x_label, label_font_size);
            tc_ui_painter_draw_text(
                context,
                impl_->x_label.c_str(),
                {widget_bounds.x + (widget_bounds.width - width) * 0.5f, widget_bounds.y + widget_bounds.height - 4.0f},
                label_font_size,
                label);
        }
        if (!impl_->y_label.empty()) {
            tc_ui_painter_draw_text(context,
                                    impl_->y_label.c_str(),
                                    {widget_bounds.x + 6.0f, global_area.y - 5.0f},
                                    label_font_size,
                                    label);
        }

        tc_ui_painter_pop_clip(context);
    }

    tc_ui_event_result Plot2D::pointer_event(tc_ui_document_handle document, const tc_ui_pointer_event* event) {
        if (event == nullptr) {
            return TC_UI_EVENT_IGNORED;
        }
        termin::visual::PointerEventKind2D kind;
        switch (event->type) {
        case TC_UI_POINTER_MOVE:
            kind = termin::visual::PointerEventKind2D::Move;
            break;
        case TC_UI_POINTER_DOWN:
            kind = termin::visual::PointerEventKind2D::Down;
            break;
        case TC_UI_POINTER_UP:
            kind = termin::visual::PointerEventKind2D::Up;
            break;
        case TC_UI_POINTER_CANCEL:
            kind = termin::visual::PointerEventKind2D::Cancel;
            break;
        default:
            return TC_UI_EVENT_IGNORED;
        }
        const tc_ui_rect widget_bounds = bounds();
        impl_->annotations.project(impl_->frame, impl_->data);
        const bool handled = impl_->annotations.route_pointer(
            impl_->frame,
            termin::visual::PointerEvent2D{
                1,
                kind,
                {event->x - widget_bounds.x, event->y - widget_bounds.y},
                static_cast<std::uint32_t>(std::max(event->button, 0)),
            });
        if (!handled) {
            return TC_UI_EVENT_IGNORED;
        }
        if (event->type == TC_UI_POINTER_DOWN) {
            termin::gui_native::TcDocument(document).set_pointer_capture(*this);
        } else if (event->type == TC_UI_POINTER_UP || event->type == TC_UI_POINTER_CANCEL) {
            termin::gui_native::TcDocument(document).release_pointer_capture(*this);
        }
        invalidate_chart();
        return TC_UI_EVENT_HANDLED;
    }

    void Plot2D::update_chart_layout() {
        if (!impl_ || !impl_->projection.valid()) {
            return;
        }
        const tc_ui_rect widget_bounds = bounds();
        const float width = widget_bounds.width > 0.0f ? widget_bounds.width : preferred_width;
        const float height = widget_bounds.height > 0.0f ? widget_bounds.height : preferred_height;
        const float left = impl_->y_label.empty() ? 48.0f : 58.0f;
        const float top = impl_->title.empty() ? 12.0f : 28.0f;
        const float right = 12.0f;
        const float bottom = impl_->x_label.empty() ? 28.0f : 42.0f;
        const float plot_width = std::max(1.0f, width - left - right);
        const float plot_height = std::max(1.0f, height - top - bottom);
        const PlotRect2D viewport{0.0f, 0.0f, width, height};
        const PlotRect2D plot_area{left, top, plot_width, plot_height};
        impl_->frame = PlotFrame2D{viewport, plot_area, impl_->range, plot_area, 1.0f};
        if (!impl_->projection.update(impl_->frame)) {
            tc::Log::error("[Plot2D] failed to update projection");
            return;
        }
        const auto ticks = make_plot_ticks2d(impl_->frame);
        if (ticks) {
            impl_->ticks = *ticks;
            if (auto* grid = resolve_plot_grid_item2d(impl_->scene, impl_->grid_handle)) {
                grid->set_ticks(impl_->ticks.x.values, impl_->ticks.y.values);
            }
        }
    }

    void Plot2D::update_auto_range() {
        double x_min = std::numeric_limits<double>::infinity();
        double x_max = -std::numeric_limits<double>::infinity();
        double y_min = std::numeric_limits<double>::infinity();
        double y_max = -std::numeric_limits<double>::infinity();
        bool has_data = false;
        for (const auto handle : impl_->line_handles) {
            const auto* line = resolve_plot_line_series_item2d(impl_->scene, handle);
            if (line == nullptr) {
                continue;
            }
            const auto x = line->x();
            const auto y = line->y();
            for (std::size_t index = 0; index < x.size(); ++index) {
                if (!std::isfinite(x[index]) || !std::isfinite(y[index])) {
                    continue;
                }
                x_min = std::min(x_min, x[index]);
                x_max = std::max(x_max, x[index]);
                y_min = std::min(y_min, y[index]);
                y_max = std::max(y_max, y[index]);
                has_data = true;
            }
        }
        for (const auto handle : impl_->scatter_handles) {
            const auto* scatter = resolve_plot_scatter_series_item2d(impl_->scene, handle);
            if (scatter == nullptr) {
                continue;
            }
            const auto x = scatter->x();
            const auto y = scatter->y();
            for (std::size_t index = 0; index < x.size(); ++index) {
                if (!std::isfinite(x[index]) || !std::isfinite(y[index])) {
                    continue;
                }
                x_min = std::min(x_min, x[index]);
                x_max = std::max(x_max, x[index]);
                y_min = std::min(y_min, y[index]);
                y_max = std::max(y_max, y[index]);
                has_data = true;
            }
        }
        const auto fitted = fit_optional_plot_range2d(
            has_data ? std::optional<PlotRange2D>{PlotRange2D{x_min, x_max, y_min, y_max}} : std::nullopt, 0.05);
        if (fitted) {
            impl_->range = *fitted;
        }
    }

    void Plot2D::invalidate_chart() {
        mark_dirty(TC_WIDGET_DIRTY_PAINT | TC_WIDGET_DIRTY_STATE);
    }

} // namespace tcplot::gui_native
