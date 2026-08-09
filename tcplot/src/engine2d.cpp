// engine2d.cpp - 2D plot engine. Port of engine2d.py.
//
// Architectural change vs Python: we draw every line segment of a
// series into ONE flat vertex buffer and issue a single
// draw_immediate_lines(). The Python path issued one draw per segment
// which made 400-point sines visibly lag during pan. All other
// coordinate / bounds / pick math stays 1:1.

#include "tcplot/engine2d.hpp"

#include <algorithm>
#include <cmath>
#include <exception>
#include <vector>

#include <tgfx2/canvas2d_renderer.hpp>
#include <tgfx2/font_atlas.hpp>
#include <tgfx2/render_context.hpp>

#include "tcplot/plot_annotations2d.hpp"
#include "tcplot/plot_layout2d.hpp"
#include "tcplot/plot_series_item2d.hpp"

#include <tcbase/tc_log.hpp>

namespace tcplot {

    namespace {

        tgfx::CanvasSrgbColor canvas_color(const Color4& c) {
            return tgfx::CanvasSrgbColor{c.r, c.g, c.b, c.a};
        }

    } // namespace

    // ---------------------------------------------------------------------------
    // Construction / destruction
    // ---------------------------------------------------------------------------

    PlotEngine2D::PlotEngine2D()
        : canvas_(std::make_unique<tgfx::Canvas2DRenderer>()),
          annotations_(std::make_unique<PlotAnnotationLayer2D>()) {}

    PlotEngine2D::~PlotEngine2D() {
        release_gpu_resources();
    }

    void PlotEngine2D::set_viewport(float x, float y, float width, float height) {
        vx_ = x;
        vy_ = y;
        vw_ = width;
        vh_ = height;
    }

    void PlotEngine2D::set_fbo_height(float h) {
        fbo_height_ = (h > 0.0f) ? h : 0.0f;
    }

    void PlotEngine2D::set_pixel_scale(float scale) {
        if (!std::isfinite(scale) || scale <= 0.0f) {
            tc::Log::error("PlotEngine2D: pixel scale must be finite and positive, got %g", static_cast<double>(scale));
            return;
        }
        pixel_scale_ = scale;
    }

    // ---------------------------------------------------------------------------
    // Series
    // ---------------------------------------------------------------------------

    void PlotEngine2D::plot(std::vector<double> x, std::vector<double> y, LinePlotOptions options) {
        data.add_line(std::move(x), std::move(y), {}, options.color, options.thickness, std::move(options.label));
        line_renderers_.push_back(std::make_unique<PlotLineSeriesGpu2D>());
        if (!view_x_min_.has_value())
            fit();
    }

    void PlotEngine2D::plot_colormap(std::vector<double> x,
                                     std::vector<double> y,
                                     std::vector<double> scalar,
                                     LineColormapOptions options) {
        LineSeries& s =
            data.add_line(std::move(x), std::move(y), {}, std::nullopt, options.thickness, std::move(options.label));
        s.scalar = std::move(scalar);
        s.colormap = options.colormap;
        s.colormap_reversed = options.colormap_reversed;
        s.scalar_min = options.scalar_min;
        s.scalar_max = options.scalar_max;
        if (s.scalar_max <= s.scalar_min) {
            s.scalar_max = s.scalar_min + 1.0;
        }
        line_renderers_.push_back(std::make_unique<PlotLineSeriesGpu2D>());
        if (!view_x_min_.has_value())
            fit();
    }

    void PlotEngine2D::scatter(std::vector<double> x, std::vector<double> y, ScatterPlotOptions options) {
        data.add_scatter(std::move(x), std::move(y), {}, options.color, options.size, std::move(options.label));
        scatter_renderers_.push_back(std::make_unique<PlotScatterSeriesGpu2D>());
        if (!view_x_min_.has_value())
            fit();
    }

    void PlotEngine2D::clear() {
        data = PlotData{};
        view_x_min_.reset();
        view_x_max_.reset();
        view_y_min_.reset();
        view_y_max_.reset();

        line_renderers_.clear();
        scatter_renderers_.clear();
    }

    void PlotEngine2D::fit() {
        const auto bounds = data.data_bounds_2d();
        const auto fitted = fit_plot_range2d(PlotRange2D{bounds[0], bounds[1], bounds[2], bounds[3]});
        if (!fitted) {
            tc::Log::error("PlotEngine2D::fit failed to fit data bounds");
            return;
        }
        view_x_min_ = fitted->x_min();
        view_x_max_ = fitted->x_max();
        view_y_min_ = fitted->y_min();
        view_y_max_ = fitted->y_max();
    }

    void PlotEngine2D::set_view(double x_min, double x_max, double y_min, double y_max) {
        view_x_min_ = x_min;
        view_x_max_ = x_max;
        view_y_min_ = y_min;
        view_y_max_ = y_max;
    }

    // ---------------------------------------------------------------------------
    // Coord helpers
    // ---------------------------------------------------------------------------

    PlotFrame2D PlotEngine2D::plot_frame() {
        if (!view_x_min_.has_value())
            fit();
        const PlotRect2D viewport(vx_, vy_, vw_, vh_);
        const PlotRect2D plot_area(vx_ + margin_left,
                                   vy_ + margin_top,
                                   std::max(vw_ - margin_left - margin_right, 1.0f),
                                   std::max(vh_ - margin_top - margin_bottom, 1.0f));
        const PlotRange2D range(*view_x_min_, *view_x_max_, *view_y_min_, *view_y_max_);
        return PlotFrame2D(viewport, plot_area, range, plot_area, pixel_scale_);
    }

    PlotAnnotationLayer2D& PlotEngine2D::annotations() {
        return *annotations_;
    }

    const PlotAnnotationLayer2D& PlotEngine2D::annotations() const {
        return *annotations_;
    }

    void PlotEngine2D::release_gpu_resources() {
        for (auto& renderer : line_renderers_) {
            renderer->release_gpu_resources();
        }
        for (auto& renderer : scatter_renderers_) {
            renderer->release_gpu_resources();
        }

        if (canvas_)
            canvas_->release_gpu();
        if (annotations_)
            annotations_->release_gpu_resources();
    }

    // ---------------------------------------------------------------------------
    // Time-series / streaming API
    // ---------------------------------------------------------------------------

    void PlotEngine2D::append_to_line(size_t idx, const double* x, const double* y, size_t n) {
        if (idx >= data.lines.size() || n == 0 || !x || !y)
            return;
        LineSeries& s = data.lines[idx];
        s.x.reserve(s.x.size() + n);
        s.y.reserve(s.y.size() + n);
        for (size_t i = 0; i < n; ++i) {
            s.x.push_back(x[i]);
            s.y.push_back(y[i]);
        }
        if (idx < line_renderers_.size()) {
            line_renderers_[idx]->invalidate_data(true);
        }
    }

    bool PlotEngine2D::last_x_of_line(size_t idx, double& out_x) const {
        if (idx >= data.lines.size())
            return false;
        const LineSeries& s = data.lines[idx];
        if (s.x.empty())
            return false;
        out_x = s.x.back();
        return true;
    }

    bool PlotEngine2D::set_line_color(size_t idx, Color4 color) {
        if (idx >= data.lines.size())
            return false;
        data.lines[idx].color = color;
        return true;
    }

    bool PlotEngine2D::set_scatter_color(size_t idx, Color4 color) {
        if (idx >= data.scatters.size())
            return false;
        data.scatters[idx].color = color;
        return true;
    }

    bool PlotEngine2D::set_line_style(size_t idx, LineStyle style, float dash_px, float gap_px) {
        if (idx >= data.lines.size())
            return false;
        LineSeries& s = data.lines[idx];
        s.line_style = style;
        s.dash_px = std::max(1.0f, dash_px);
        s.gap_px = std::max(1.0f, gap_px);
        return true;
    }

    bool PlotEngine2D::set_line_colormap_reversed(size_t idx, bool reversed) {
        if (idx >= data.lines.size())
            return false;
        data.lines[idx].colormap_reversed = reversed;
        return true;
    }

    void PlotEngine2D::get_view(double& x_min, double& x_max, double& y_min, double& y_max) {
        const PlotFrame2D frame = plot_frame();
        const PlotRange2D& v = frame.range();
        x_min = v.x_min();
        x_max = v.x_max();
        y_min = v.y_min();
        y_max = v.y_max();
    }

    void PlotEngine2D::set_view_x(double x_min, double x_max) {
        view_x_min_ = x_min;
        view_x_max_ = x_max;
        // Y preserved as-is (auto-fitted on first use if still unset).
    }

    void PlotEngine2D::set_view_y(double y_min, double y_max) {
        view_y_min_ = y_min;
        view_y_max_ = y_max;
    }

    // ---------------------------------------------------------------------------
    // Rendering
    // ---------------------------------------------------------------------------

    void PlotEngine2D::begin_render_phase_(PlotRenderPhase2D phase,
                                           const PlotFrame2D& frame,
                                           tgfx::RenderContext2& context,
                                           tgfx::FontAtlas* font) {
        const bool plot_clipped = phase == PlotRenderPhase2D::AnnotationUnderlay ||
                                  phase == PlotRenderPhase2D::Series || phase == PlotRenderPhase2D::AnnotationOverlay;
        const PlotRect2D& clip = plot_clipped ? frame.clip_rect() : frame.viewport();
        const auto restore_phase_state = [&]() {
            context.set_viewport(static_cast<int>(frame.viewport().x()),
                                 static_cast<int>(frame.viewport().y()),
                                 static_cast<int>(frame.viewport().width()),
                                 static_cast<int>(frame.viewport().height()));
            context.set_scissor(static_cast<int>(clip.x()),
                                static_cast<int>(clip.y()),
                                static_cast<int>(clip.width()),
                                static_cast<int>(clip.height()));
        };
        try {
            if (annotations_) {
                annotations_->render_phase(phase, frame, context, font);
            }
            if (render_phase_sink_) {
                restore_phase_state();
                render_phase_sink_->begin_plot_phase(phase, frame, context, font);
            }
        } catch (const std::exception& error) {
            tc::Log::error(
                "PlotEngine2D: render phase sink failed in phase %d: %s", static_cast<int>(phase), error.what());
            throw;
        } catch (...) {
            tc::Log::error("PlotEngine2D: render phase sink failed in phase %d", static_cast<int>(phase));
            throw;
        }
    }

    void PlotEngine2D::render(tgfx::RenderContext2* ctx, tgfx::FontAtlas* font) {
        if (!ctx || vw_ <= 0 || vh_ <= 0)
            return;

        const int canvas_w = static_cast<int>(std::ceil(std::max(vw_, vx_ + vw_)));
        const float target_h = fbo_height_ > 0.0f ? fbo_height_ : (vy_ + vh_);
        const int canvas_h = static_cast<int>(std::ceil(std::max(vh_, target_h)));

        const PlotFrame2D frame = plot_frame();
        const PlotRect2D& pa = frame.plot_area();

        annotations_->project(frame, data);
        begin_render_phase_(PlotRenderPhase2D::Background, frame, *ctx, font);
        canvas_->set_default_font(font);
        canvas_->begin(*ctx, canvas_w, canvas_h);
        canvas_->draw_rect(vx_, vy_, vw_, vh_, canvas_color(bg_color));
        canvas_->draw_rect(pa.x(), pa.y(), pa.width(), pa.height(), canvas_color(plot_bg_color));

        const auto layout_ticks = make_plot_ticks2d(frame);
        if (!layout_ticks) {
            tc::Log::error("PlotEngine2D::render failed to compute ticks");
            return;
        }
        const std::vector<double>& x_ticks = layout_ticks->x.values;
        const std::vector<double>& y_ticks = layout_ticks->y.values;

        canvas_->begin_clip(pa.x(), pa.y(), pa.width(), pa.height());
        if (show_grid) {
            for (double tx : x_ticks) {
                const PlotPixelPoint2D p = frame.data_to_pixel(tx, 0.0);
                canvas_->draw_line(p.x, pa.y(), p.x, pa.bottom(), canvas_color(grid_color));
            }
            for (double ty : y_ticks) {
                const PlotPixelPoint2D p = frame.data_to_pixel(0.0, ty);
                canvas_->draw_line(pa.x(), p.y, pa.right(), p.y, canvas_color(grid_color));
            }
        }
        canvas_->end_clip();
        canvas_->end();

        begin_render_phase_(PlotRenderPhase2D::AnnotationUnderlay, frame, *ctx, font);
        begin_render_phase_(PlotRenderPhase2D::Series, frame, *ctx, font);

        while (line_renderers_.size() < data.lines.size()) {
            line_renderers_.push_back(std::make_unique<PlotLineSeriesGpu2D>());
        }
        line_renderers_.resize(data.lines.size());
        while (scatter_renderers_.size() < data.scatters.size()) {
            scatter_renderers_.push_back(std::make_unique<PlotScatterSeriesGpu2D>());
        }
        scatter_renderers_.resize(data.scatters.size());

        const termin::Rect2f series_clip{pa.x(), pa.y(), pa.width(), pa.height()};
        for (std::size_t index = 0; index < data.lines.size(); ++index) {
            const auto& series = data.lines[index];
            const Color4 color = series.color.value_or(styles::cycle_color(static_cast<std::uint32_t>(index)));
            const PlotLineSeriesStyle2D style{
                color,
                static_cast<float>(std::max(1.0, series.thickness)),
                series.line_style,
                std::max(1.0f, series.dash_px),
                std::max(1.0f, series.gap_px),
                series.scalar.empty() ? SurfaceColorMap::Solid : series.colormap,
                series.colormap_reversed,
                series.scalar_min,
                std::max(series.scalar_max, series.scalar_min + 1.0e-12),
            };
            if (!line_renderers_[index]->render(*ctx,
                                                frame,
                                                termin::Affine2f::identity(),
                                                1.0f,
                                                series_clip,
                                                series.x,
                                                series.y,
                                                series.scalar,
                                                style)) {
                tc::Log::error("PlotEngine2D failed to render line series %zu", index);
                return;
            }
        }
        for (std::size_t index = 0; index < data.scatters.size(); ++index) {
            const auto& series = data.scatters[index];
            const Color4 color =
                series.color.value_or(styles::cycle_color(static_cast<std::uint32_t>(data.lines.size() + index)));
            if (!scatter_renderers_[index]->render(*ctx,
                                                   frame,
                                                   termin::Affine2f::identity(),
                                                   1.0f,
                                                   series_clip,
                                                   series.x,
                                                   series.y,
                                                   {color, static_cast<float>(series.size)})) {
                tc::Log::error("PlotEngine2D failed to render scatter series %zu", index);
                return;
            }
        }

        begin_render_phase_(PlotRenderPhase2D::AnnotationOverlay, frame, *ctx, font);
        begin_render_phase_(PlotRenderPhase2D::UnclippedChrome, frame, *ctx, font);

        canvas_->begin(*ctx, canvas_w, canvas_h);
        canvas_->draw_rect_outline(pa.x(), pa.y(), pa.width(), pa.height(), canvas_color(axis_color));

        // --- Text: tick labels, title, axis labels ---
        if (font) {
            // Ticks a notch smaller than axis labels — keeps the axis-label
            // / tick-label hierarchy visible even at similar sizes.
            const float tick_sz = font_size - 2.0f;

            for (std::size_t index = 0; index < x_ticks.size(); ++index) {
                const double tx = x_ticks[index];
                const PlotPixelPoint2D p = frame.data_to_pixel(tx, 0.0);
                canvas_->draw_text(layout_ticks->x.labels[index],
                                   p.x,
                                   pa.bottom() + 14.0f,
                                   tick_sz,
                                   canvas_color(label_color),
                                   font,
                                   tgfx::Text2DRenderer::Anchor::Center);
            }
            for (std::size_t index = 0; index < y_ticks.size(); ++index) {
                const double ty = y_ticks[index];
                const PlotPixelPoint2D p = frame.data_to_pixel(0.0, ty);
                const std::string& label = layout_ticks->y.labels[index];
                const auto metrics = measure_plot_text2d(*font, label, tick_sz);
                if (!metrics) {
                    tc::Log::error("PlotEngine2D::render failed to measure Y tick");
                    return;
                }
                canvas_->draw_text(label,
                                   pa.x() - metrics->width - 6.0f,
                                   p.y + 4.0f,
                                   tick_sz,
                                   canvas_color(label_color),
                                   font,
                                   tgfx::Text2DRenderer::Anchor::Left);
            }

            if (!data.title.empty()) {
                // Title: left-anchored, bottom sitting just above the plot
                // area with a small gap. Earlier revisions placed it at
                // `margin_top * 0.5f` (vertical centre of the margin),
                // which left only `margin_top - title_font_size` px of
                // clearance — for Segoe UI's ~1.3× line-height ratio and
                // title_font_size=22 the title bottom landed ~6 px INSIDE
                // the plot area. Anchoring the bottom to pa.y instead
                // keeps the gap visually stable regardless of the font's
                // metric ratio.
                //
                // Colour: `title_color` if set, else `label_color`. Earlier
                // revisions auto-picked the first line series' colour —
                // removed in favour of an explicit override because the
                // implicit coupling made theme switching and per-panel
                // recolouring unpredictable.
                const Color4 tc = title_color.value_or(label_color);
                const auto title_metrics = measure_plot_text2d(*font, data.title, title_font_size);
                if (!title_metrics) {
                    tc::Log::error("PlotEngine2D::render failed to measure title");
                    return;
                }
                // Clamp to viewport top so a title bigger than margin_top
                // gets cut off at the top instead of drawing at a negative
                // Y — caller's cue to bump margin_top.
                const float title_top_y =
                    std::max(static_cast<float>(vy_), pa.y() - title_metrics->line_height - title_pad);
                canvas_->draw_text(data.title,
                                   pa.x(),
                                   title_top_y,
                                   title_font_size,
                                   canvas_color(tc),
                                   font,
                                   tgfx::Text2DRenderer::Anchor::Left);
            }
            if (!data.x_label.empty()) {
                canvas_->draw_text(data.x_label,
                                   pa.x() + pa.width() * 0.5f,
                                   pa.bottom() + margin_bottom - 4.0f,
                                   font_size,
                                   canvas_color(label_color),
                                   font,
                                   tgfx::Text2DRenderer::Anchor::Center);
            }
            if (!data.y_label.empty()) {
                canvas_->draw_text(data.y_label,
                                   vx_ + margin_left * 0.5f,
                                   pa.y() + pa.height() * 0.5f,
                                   font_size,
                                   canvas_color(label_color),
                                   font,
                                   tgfx::Text2DRenderer::Anchor::Center);
            }
        }

        canvas_->end();
    }

    // ---------------------------------------------------------------------------
    // Input
    // ---------------------------------------------------------------------------

    bool PlotEngine2D::on_mouse_down(float x, float y, tcbase::MouseButton button) {
        if (button == tcbase::MouseButton::LEFT) {
            const PlotFrame2D frame = plot_frame();
            if (annotations_->route_pointer(frame,
                                            {
                                                0,
                                                termin::visual::PointerEventKind2D::Down,
                                                {x, y},
                                                static_cast<std::uint32_t>(button),
                                            })) {
                return true;
            }
        }
        if (button == tcbase::MouseButton::MIDDLE) {
            panning_ = true;
            pan_start_mx_ = x;
            pan_start_my_ = y;
            const PlotFrame2D frame = plot_frame();
            const PlotRange2D& v = frame.range();
            pan_start_view_[0] = v.x_min();
            pan_start_view_[1] = v.x_max();
            pan_start_view_[2] = v.y_min();
            pan_start_view_[3] = v.y_max();
            return true;
        }
        return false;
    }

    void PlotEngine2D::on_mouse_move(float x, float y) {
        const PlotFrame2D frame = plot_frame();
        if (!panning_ && annotations_->route_pointer(frame,
                                                     {
                                                         0,
                                                         termin::visual::PointerEventKind2D::Move,
                                                         {x, y},
                                                         0,
                                                     })) {
            return;
        }
        if (!panning_)
            return;
        const PlotRect2D& pa = frame.plot_area();
        const double vx0 = pan_start_view_[0];
        const double vx1 = pan_start_view_[1];
        const double vy0 = pan_start_view_[2];
        const double vy1 = pan_start_view_[3];
        const double dx_px = x - pan_start_mx_;
        const double dy_px = y - pan_start_my_;
        // Pan follows the cursor: the data point under the mouse stays
        // glued to it. pixel_x = pa_x + (x - x_min)/span_x * pa_w, so
        // dx_data = -dx_px/pa.w * span_x keeps the point fixed on X.
        // pixel_y = pa_y + (y_max - y)/span_y * pa_h grows downward as
        // data y shrinks — the opposite direction, so the Y sign is
        // positive here.
        const double dx_data = -dx_px / pa.width() * (vx1 - vx0);
        const double dy_data = dy_px / pa.height() * (vy1 - vy0);
        view_x_min_ = vx0 + dx_data;
        view_x_max_ = vx1 + dx_data;
        view_y_min_ = vy0 + dy_data;
        view_y_max_ = vy1 + dy_data;
    }

    void PlotEngine2D::on_mouse_up(float x, float y, tcbase::MouseButton button) {
        if (button == tcbase::MouseButton::LEFT) {
            annotations_->route_pointer(plot_frame(),
                                        {
                                            0,
                                            termin::visual::PointerEventKind2D::Up,
                                            {x, y},
                                            static_cast<std::uint32_t>(button),
                                        });
        }
        panning_ = false;
    }

    bool PlotEngine2D::on_mouse_wheel_x(float x, float y, float dy) {
        return on_mouse_wheel_x_result(x, y, dy) != PlotInputResult2D::Unhandled;
    }

    bool PlotEngine2D::on_mouse_wheel(float x, float y, float dy) {
        return on_mouse_wheel_result(x, y, dy) != PlotInputResult2D::Unhandled;
    }

    PlotInputResult2D PlotEngine2D::on_mouse_wheel_x_result(float x, float y, float dy) {
        return on_mouse_wheel_impl_(x, y, dy, true);
    }

    PlotInputResult2D PlotEngine2D::on_mouse_wheel_result(float x, float y, float dy) {
        return on_mouse_wheel_impl_(x, y, dy, false);
    }

    PlotInputResult2D PlotEngine2D::on_mouse_wheel_impl_(float x, float y, float dy, bool x_only) {
        const PlotFrame2D frame = plot_frame();
        if (annotations_->hit_test(frame, x, y)) {
            return PlotInputResult2D::Annotation;
        }
        if (!frame.contains_plot_pixel(x, y)) {
            return PlotInputResult2D::Unhandled;
        }

        const float factor = (dy > 0) ? 0.85f : 1.0f / 0.85f;

        const PlotPoint2D center = frame.pixel_to_data(x, y);
        const PlotRange2D& v = frame.range();

        view_x_min_ = center.x + (v.x_min() - center.x) * factor;
        view_x_max_ = center.x + (v.x_max() - center.x) * factor;
        if (!x_only) {
            view_y_min_ = center.y + (v.y_min() - center.y) * factor;
            view_y_max_ = center.y + (v.y_max() - center.y) * factor;
        }
        return PlotInputResult2D::Navigation;
    }

} // namespace tcplot
