// plot_view2d.cpp - see plot_view3d.cpp for the architectural note.
// Same pattern, minus depth attachment.

#include "tcplot/plot_view2d.hpp"

#include <cmath>
#include <optional>

#include <tcbase/input_enums.hpp>
#include <tgfx2/descriptors.hpp>
#include <tgfx2/enums.hpp>
#include <tgfx2/font_atlas.hpp>
#include <tgfx2/handles.hpp>
#include <tgfx2/i_render_device.hpp>
#include <tgfx2/pipeline_cache.hpp>
#include <tgfx2/render_context.hpp>

#include "tcplot/engine2d.hpp"
#include "tcplot/gpu_host.hpp"

namespace tcplot {

    namespace {

        std::optional<SrgbColor> plot2d_opt_color(SrgbColor color) {
            if (std::isnan(color.r) || std::isnan(color.g) || std::isnan(color.b) || std::isnan(color.a)) {
                return std::nullopt;
            }
            return color;
        }

        std::vector<double> plot2d_copy_array(const double* src, size_t n) {
            if (!src || n == 0)
                return {};
            return std::vector<double>(src, src + n);
        }

    } // namespace

    PlotView2D::PlotView2D(tgfx::IRenderDevice& device,
                           tgfx::PipelineCache& cache,
                           tgfx::RenderContext2& ctx,
                           tgfx::FontAtlas& font)
        : device_(&device),
          cache_(&cache),
          ctx_(&ctx),
          font_(&font),
          engine_(std::make_unique<PlotEngine2D>()) {}

    PlotView2D::PlotView2D(GpuHost& host)
        : PlotView2D(host.device(), host.cache(), host.ctx(), host.font()) {}

    PlotView2D::~PlotView2D() {
        release_gpu();
    }

    void PlotView2D::ensure_offscreen_(int w, int h) {
        if (offscreen_w_ == w && offscreen_h_ == h && offscreen_color_.id != 0) {
            return;
        }
        if (offscreen_color_.id != 0)
            device_->destroy(offscreen_color_);
        offscreen_color_ = tgfx::TextureHandle{};

        tgfx::TextureDesc desc;
        desc.width = static_cast<uint32_t>(w);
        desc.height = static_cast<uint32_t>(h);
        desc.format = tgfx::PixelFormat::RGBA8_UNorm;
        desc.usage = tgfx::TextureUsage::Sampled | tgfx::TextureUsage::ColorAttachment | tgfx::TextureUsage::CopySrc;
        desc.sample_count = static_cast<uint32_t>(msaa_samples_);
        offscreen_color_ = device_->create_texture(desc);

        offscreen_w_ = w;
        offscreen_h_ = h;
    }

    void PlotView2D::plot(SeriesData2DView series, LinePlotOptions options) {
        if (options.color.has_value()) {
            options.color = plot2d_opt_color(*options.color);
        }
        engine_->plot(
            plot2d_copy_array(series.x, series.count), plot2d_copy_array(series.y, series.count), std::move(options));
    }

    void PlotView2D::plot_colormap(SeriesData2DView series, const double* scalar, LineColormapOptions options) {
        engine_->plot_colormap(plot2d_copy_array(series.x, series.count),
                               plot2d_copy_array(series.y, series.count),
                               plot2d_copy_array(scalar, series.count),
                               std::move(options));
    }

    void PlotView2D::scatter(SeriesData2DView series, ScatterPlotOptions options) {
        if (options.color.has_value()) {
            options.color = plot2d_opt_color(*options.color);
        }
        engine_->scatter(
            plot2d_copy_array(series.x, series.count), plot2d_copy_array(series.y, series.count), std::move(options));
    }

    void PlotView2D::clear() {
        engine_->clear();
    }
    void PlotView2D::fit() {
        engine_->fit();
    }

    void PlotView2D::set_msaa_samples(int samples) {
        if (samples < 1)
            samples = 1;
        if (samples == msaa_samples_)
            return;
        msaa_samples_ = samples;
        if (device_ && offscreen_color_.id != 0) {
            device_->destroy(offscreen_color_);
        }
        offscreen_color_ = tgfx::TextureHandle{};
        offscreen_w_ = 0;
        offscreen_h_ = 0;
    }

    void PlotView2D::set_view(double x_min, double x_max, double y_min, double y_max) {
        engine_->set_view(x_min, x_max, y_min, y_max);
    }

    PlotAnnotationLayer2D& PlotView2D::annotations() {
        return engine_->annotations();
    }

    const PlotAnnotationLayer2D& PlotView2D::annotations() const {
        return engine_->annotations();
    }

    PlotAnnotationHandle PlotView2D::create_data_marker(double x, double y, const char* text) {
        PlotDataMarker2D marker;
        marker.data_position = {x, y};
        marker.text = text ? text : "";
        const auto handle = engine_->annotations().create_data_marker(std::move(marker));
        if (!handle)
            return {};
        engine_->annotations().project(engine_->plot_frame(), engine_->data);
        return *handle;
    }

    bool PlotView2D::update_data_marker(PlotAnnotationHandle handle, double x, double y, const char* text) {
        const auto current = engine_->annotations().data_marker_snapshot(handle);
        if (!current)
            return false;
        PlotDataMarker2D marker = current->marker;
        marker.data_position = {x, y};
        marker.text = text ? text : "";
        if (!engine_->annotations().update_data_marker(handle, std::move(marker))) {
            return false;
        }
        engine_->annotations().project(engine_->plot_frame(), engine_->data);
        return true;
    }

    PlotDataMarkerBindingSnapshot2D PlotView2D::data_marker_binding_snapshot(PlotAnnotationHandle handle) const {
        const auto snapshot = engine_->annotations().data_marker_snapshot(handle);
        if (!snapshot)
            return {};
        return {
            true,
            snapshot->annotation,
            snapshot->marker.data_position.x,
            snapshot->marker.data_position.y,
            snapshot->marker.text,
            snapshot->hovered,
            snapshot->dragging,
        };
    }

    bool PlotView2D::destroy_annotation(PlotAnnotationHandle handle) {
        return engine_->annotations().destroy(handle);
    }

    PlotAnnotationActionPoll2D PlotView2D::take_annotation_action() {
        const auto action = engine_->annotations().take_action();
        if (!action)
            return {};
        return {true, action->annotation, action->action};
    }

    void PlotView2D::set_title(const char* title) {
        engine_->data.title = title ? title : "";
    }
    void PlotView2D::set_x_label(const char* label) {
        engine_->data.x_label = label ? label : "";
    }
    void PlotView2D::set_y_label(const char* label) {
        engine_->data.y_label = label ? label : "";
    }

    bool PlotView2D::set_line_color(int idx, SrgbColor color) {
        if (idx < 0)
            return false;
        return engine_->set_line_color(static_cast<size_t>(idx), color);
    }

    bool PlotView2D::set_scatter_color(int idx, SrgbColor color) {
        if (idx < 0)
            return false;
        return engine_->set_scatter_color(static_cast<size_t>(idx), color);
    }

    bool PlotView2D::set_line_style(int idx, LineStyle style, float dash_px, float gap_px) {
        if (idx < 0)
            return false;
        return engine_->set_line_style(static_cast<size_t>(idx), style, dash_px, gap_px);
    }

    bool PlotView2D::set_line_colormap_reversed(int idx, bool reversed) {
        if (idx < 0)
            return false;
        return engine_->set_line_colormap_reversed(static_cast<size_t>(idx), reversed);
    }

    bool PlotView2D::on_mouse_down(float x, float y, int button) {
        return engine_->on_mouse_down(x, y, static_cast<tcbase::MouseButton>(button));
    }
    void PlotView2D::on_mouse_move(float x, float y) {
        engine_->on_mouse_move(x, y);
    }
    void PlotView2D::on_mouse_up(float x, float y, int button) {
        engine_->on_mouse_up(x, y, static_cast<tcbase::MouseButton>(button));
    }
    bool PlotView2D::on_mouse_wheel(float x, float y, float dy) {
        return engine_->on_mouse_wheel(x, y, dy);
    }

    tgfx::TextureHandle PlotView2D::render_to_texture(int width, int height) {
        if (width <= 0 || height <= 0)
            return tgfx::TextureHandle{};

        ensure_offscreen_(width, height);
        engine_->set_viewport(0, 0, (float)width, (float)height);

        ctx_->begin_frame();

        const SrgbColor bg = styles::bg_color();
        const termin::LinearColor clear_col = termin::srgb_to_linear(bg);
        // 2D pass: no depth attachment, no depth clear.
        ctx_->begin_pass(offscreen_color_, tgfx::TextureHandle{}, &clear_col, 1.0f, false);

        engine_->render(ctx_, font_);

        ctx_->end_pass();
        ctx_->end_frame();

        return offscreen_color_;
    }

    uint32_t PlotView2D::render_to_texture_id(int width, int height) {
        return render_to_texture(width, height).id;
    }

    void PlotView2D::release_gpu() {
        if (engine_)
            engine_->release_gpu_resources();

        if (device_ && offscreen_color_.id != 0) {
            device_->destroy(offscreen_color_);
        }
        offscreen_color_ = tgfx::TextureHandle{};
        offscreen_w_ = 0;
        offscreen_h_ = 0;
    }

} // namespace tcplot
