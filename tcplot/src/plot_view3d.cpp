// plot_view3d.cpp - tcplot PlotView3D implementation.
//
// All backend state is confined to tgfx2. Plot views render into
// tgfx::TextureHandle targets; hosts are expected to present or compose
// those textures through the backend-neutral texture pipeline.

#include "tcplot/plot_view3d.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

#include <tcbase/input_enums.hpp>
#include <tgfx2/descriptors.hpp>
#include <tgfx2/enums.hpp>
#include <tgfx2/font_atlas.hpp>
#include <tgfx2/handles.hpp>
#include <tgfx2/i_render_device.hpp>
#include <tgfx2/pipeline_cache.hpp>
#include <tgfx2/render_context.hpp>

#include "tcplot/engine3d.hpp"
#include "tcplot/gpu_host.hpp"

namespace tcplot {

namespace {

std::optional<Color4> plot3d_opt_color(Color4 color) {
    if (std::isnan(color.r) || std::isnan(color.g)
        || std::isnan(color.b) || std::isnan(color.a)) {
        return std::nullopt;
    }
    return color;
}

std::vector<double> plot3d_copy_array(const double* src, size_t n) {
    if (!src || n == 0) return {};
    return std::vector<double>(src, src + n);
}

}  // namespace

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

PlotView3D::PlotView3D(tgfx::IRenderDevice& device,
                       tgfx::PipelineCache& cache,
                       tgfx::RenderContext2& ctx,
                       tgfx::FontAtlas& font)
    : device_(&device),
      cache_(&cache),
      ctx_(&ctx),
      font_(&font),
      engine_(std::make_unique<PlotEngine3D>()) {}

PlotView3D::PlotView3D(GpuHost& host)
    : PlotView3D(host.device(), host.cache(), host.ctx(), host.font()) {}

PlotView3D::~PlotView3D() {
    release_gpu();
}

// ---------------------------------------------------------------------------
// Offscreen attachments via tgfx2
// ---------------------------------------------------------------------------

void PlotView3D::ensure_offscreen_(int w, int h) {
    if (offscreen_w_ == w && offscreen_h_ == h &&
        offscreen_color_.id != 0 && offscreen_depth_.id != 0) {
        return;
    }
    // Drop prior attachments — device.destroy is safe on zero ids.
    if (offscreen_color_.id != 0) device_->destroy(offscreen_color_);
    if (offscreen_depth_.id != 0) device_->destroy(offscreen_depth_);
    offscreen_color_ = tgfx::TextureHandle{};
    offscreen_depth_ = tgfx::TextureHandle{};

    tgfx::TextureDesc color_desc;
    color_desc.width = static_cast<uint32_t>(w);
    color_desc.height = static_cast<uint32_t>(h);
    color_desc.format = tgfx::PixelFormat::RGBA8_UNorm;
    color_desc.usage = tgfx::TextureUsage::Sampled
                     | tgfx::TextureUsage::ColorAttachment
                     | tgfx::TextureUsage::CopySrc;
    color_desc.sample_count = static_cast<uint32_t>(msaa_samples_);
    offscreen_color_ = device_->create_texture(color_desc);

    // D24 (GL_DEPTH_COMPONENT24) is the most portable multisample
    // depth format — every GL 3.3 driver supports it. D32F +
    // multisample is valid per-spec but some drivers reject or
    // silently fail the glTexImage2DMultisample call, which surfaces
    // as the dreaded "white screen + silent crash" path WPF can't
    // trace. Stick with D24 for MSAA attachments.
    tgfx::TextureDesc depth_desc;
    depth_desc.width = static_cast<uint32_t>(w);
    depth_desc.height = static_cast<uint32_t>(h);
    depth_desc.format = tgfx::PixelFormat::D24_UNorm;
    depth_desc.usage = tgfx::TextureUsage::DepthStencilAttachment;
    depth_desc.sample_count = static_cast<uint32_t>(msaa_samples_);
    offscreen_depth_ = device_->create_texture(depth_desc);

    offscreen_w_ = w;
    offscreen_h_ = h;
}

// ---------------------------------------------------------------------------
// Series
// ---------------------------------------------------------------------------

void PlotView3D::plot(SeriesData3DView series, LinePlotOptions options) {
    if (options.color.has_value()) {
        options.color = plot3d_opt_color(*options.color);
    }
    engine_->plot(
        plot3d_copy_array(series.x, series.count),
        plot3d_copy_array(series.y, series.count),
        plot3d_copy_array(series.z, series.count),
        std::move(options));
}

void PlotView3D::scatter(SeriesData3DView series, ScatterPlotOptions options) {
    if (options.color.has_value()) {
        options.color = plot3d_opt_color(*options.color);
    }
    engine_->scatter(
        plot3d_copy_array(series.x, series.count),
        plot3d_copy_array(series.y, series.count),
        plot3d_copy_array(series.z, series.count),
        std::move(options));
}

void PlotView3D::surface(SurfaceDataView surface, SurfacePlotOptions options) {
    options.colormap = SurfaceColorMap::Jet;
    surface_colormap(surface, std::move(options));
}

void PlotView3D::surface_colormap(SurfaceDataView surface, SurfacePlotOptions options) {
    if (options.color.has_value()) {
        options.color = plot3d_opt_color(*options.color);
    }
    const size_t n = surface.count();
    engine_->surface(
        plot3d_copy_array(surface.x, n),
        plot3d_copy_array(surface.y, n),
        plot3d_copy_array(surface.z, n),
        surface.rows,
        surface.cols,
        std::move(options));
}

void PlotView3D::clear()               { engine_->clear(); }
void PlotView3D::set_title(const char* title) {
    engine_->data.title = title ? title : "";
}
void PlotView3D::set_x_label(const char* label) {
    engine_->data.x_label = label ? label : "";
}
void PlotView3D::set_y_label(const char* label) {
    engine_->data.y_label = label ? label : "";
}
void PlotView3D::set_z_label(const char* label) {
    engine_->data.z_label = label ? label : "";
}
void PlotView3D::set_axis_labels(const char* x_label,
                                 const char* y_label,
                                 const char* z_label) {
    set_x_label(x_label);
    set_y_label(y_label);
    set_z_label(z_label);
}
bool PlotView3D::set_surface_colormap(int surface_idx, SurfaceColorMap colormap) {
    if (surface_idx < 0) return false;
    return engine_->set_surface_colormap(static_cast<size_t>(surface_idx), colormap);
}

bool PlotView3D::set_surface_colormap_reversed(int surface_idx, bool reversed) {
    if (surface_idx < 0) return false;
    return engine_->set_surface_colormap_reversed(
        static_cast<size_t>(surface_idx), reversed);
}
bool PlotView3D::set_surface_color(int surface_idx, float r, float g, float b, float a) {
    if (surface_idx < 0) return false;
    return engine_->set_surface_color(static_cast<size_t>(surface_idx), Color4{r, g, b, a});
}
bool PlotView3D::set_surface_grid(int surface_idx, SurfaceGridOptions options) {
    if (surface_idx < 0) return false;
    return engine_->set_surface_grid(static_cast<size_t>(surface_idx), options);
}
void PlotView3D::toggle_wireframe()    { engine_->toggle_wireframe(); }
void PlotView3D::toggle_marker_mode()  { engine_->toggle_marker_mode(); }
void PlotView3D::set_z_scale(float s)  { engine_->z_scale = s; }
float PlotView3D::get_z_scale() const  { return engine_->z_scale; }
void PlotView3D::set_axis_scale(float x, float y, float z) {
    engine_->x_scale = x;
    engine_->y_scale = y;
    engine_->z_scale = z;
}
float PlotView3D::get_x_scale() const  { return engine_->x_scale; }
float PlotView3D::get_y_scale() const  { return engine_->y_scale; }
void PlotView3D::set_surface_shading(bool enabled, float strength) {
    engine_->set_surface_shading(enabled, strength);
}
void PlotView3D::set_surface_light_dir(float x, float y, float z) {
    engine_->set_surface_light_dir(x, y, z);
}

void PlotView3D::set_msaa_samples(int samples) {
    if (samples < 1) samples = 1;
    if (samples == msaa_samples_) return;
    msaa_samples_ = samples;
    // Drop the current attachments; next render will re-allocate with
    // the new sample count via ensure_offscreen_.
    if (device_) {
        if (offscreen_color_.id != 0) device_->destroy(offscreen_color_);
        if (offscreen_depth_.id != 0) device_->destroy(offscreen_depth_);
    }
    offscreen_color_ = tgfx::TextureHandle{};
    offscreen_depth_ = tgfx::TextureHandle{};
    offscreen_w_ = 0;
    offscreen_h_ = 0;
}

OrbitCamera& PlotView3D::camera() { return engine_->camera; }

void PlotView3D::fit_camera() {
    double lo[3], hi[3];
    engine_->data.data_bounds_3d(lo, hi);

    const termin::Vec3f scaled_lo{
        static_cast<float>(lo[0] * engine_->x_scale),
        static_cast<float>(lo[1] * engine_->y_scale),
        static_cast<float>(lo[2] * engine_->z_scale),
    };
    const termin::Vec3f scaled_hi{
        static_cast<float>(hi[0] * engine_->x_scale),
        static_cast<float>(hi[1] * engine_->y_scale),
        static_cast<float>(hi[2] * engine_->z_scale),
    };
    const termin::Vec3f bounds_min{
        std::min(scaled_lo.x, scaled_hi.x),
        std::min(scaled_lo.y, scaled_hi.y),
        std::min(scaled_lo.z, scaled_hi.z),
    };
    const termin::Vec3f bounds_max{
        std::max(scaled_lo.x, scaled_hi.x),
        std::max(scaled_lo.y, scaled_hi.y),
        std::max(scaled_lo.z, scaled_hi.z),
    };
    engine_->camera.fit_bounds(bounds_min, bounds_max);
}

// ---------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------

bool PlotView3D::on_mouse_down(float x, float y, int button) {
    return engine_->on_mouse_down(x, y, static_cast<tcbase::MouseButton>(button));
}

void PlotView3D::on_mouse_move(float x, float y) {
    engine_->on_mouse_move(x, y);
}

void PlotView3D::on_mouse_up(float x, float y, int button) {
    engine_->on_mouse_up(x, y, static_cast<tcbase::MouseButton>(button));
}

bool PlotView3D::on_mouse_wheel(float x, float y, float dy) {
    return engine_->on_mouse_wheel(x, y, dy);
}

bool PlotView3D::pick(float mx, float my,
                        double* out_x, double* out_y, double* out_z,
                        double* out_screen_dist_px) {
    auto r = engine_->pick(mx, my);
    if (!r.has_value()) {
        if (out_x) *out_x = 0;
        if (out_y) *out_y = 0;
        if (out_z) *out_z = 0;
        if (out_screen_dist_px) *out_screen_dist_px = 0;
        return false;
    }
    if (out_x) *out_x = r->x;
    if (out_y) *out_y = r->y;
    if (out_z) *out_z = r->z;
    if (out_screen_dist_px) *out_screen_dist_px = r->screen_dist_px;
    return true;
}

// ---------------------------------------------------------------------------
// Render
// ---------------------------------------------------------------------------

tgfx::TextureHandle PlotView3D::render_to_texture(int width, int height) {
    if (width <= 0 || height <= 0) return tgfx::TextureHandle{};

    ensure_offscreen_(width, height);
    engine_->set_viewport(0, 0, (float)width, (float)height);

    ctx_->begin_frame();

    const Color4 bg = styles::bg_color();
    const float clear_col[4] = {bg.r, bg.g, bg.b, bg.a};
    ctx_->begin_pass(offscreen_color_, offscreen_depth_,
                     clear_col, 1.0f, true);
    engine_->render(ctx_, font_);
    ctx_->end_pass();
    ctx_->end_frame();

    return offscreen_color_;
}

uint32_t PlotView3D::render_to_texture_id(int width, int height) {
    return render_to_texture(width, height).id;
}

void PlotView3D::release_gpu() {
    if (engine_) engine_->release_gpu_resources();

    if (device_) {
        if (offscreen_color_.id != 0) device_->destroy(offscreen_color_);
        if (offscreen_depth_.id != 0) device_->destroy(offscreen_depth_);
    }
    offscreen_color_ = tgfx::TextureHandle{};
    offscreen_depth_ = tgfx::TextureHandle{};
    offscreen_w_ = 0;
    offscreen_h_ = 0;
}

}  // namespace tcplot
