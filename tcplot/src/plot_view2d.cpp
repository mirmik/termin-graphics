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
#include <tgfx2/opengl/opengl_render_device.hpp>
#include <tgfx2/pipeline_cache.hpp>
#include <tgfx2/render_context.hpp>

#include "tcplot/engine2d.hpp"

namespace tcplot {

namespace {

std::optional<Color4> opt_color(float r, float g, float b, float a) {
    if (std::isnan(r) || std::isnan(g) || std::isnan(b) || std::isnan(a)) {
        return std::nullopt;
    }
    return Color4{r, g, b, a};
}

std::vector<double> copy_array(const double* src, size_t n) {
    if (!src || n == 0) return {};
    return std::vector<double>(src, src + n);
}

}  // namespace

PlotView2D::PlotView2D(const std::string& ttf_path)
    : ttf_path_(ttf_path) {
    device_ = std::make_unique<tgfx2::OpenGLRenderDevice>();
    cache_  = std::make_unique<tgfx2::PipelineCache>(*device_);
    ctx_    = std::make_unique<tgfx2::RenderContext2>(*device_, *cache_);
    font_   = std::make_unique<tgfx2::FontAtlas>(ttf_path);
    engine_ = std::make_unique<PlotEngine2D>();
}

PlotView2D::~PlotView2D() {
    release_gpu();
}

void PlotView2D::ensure_offscreen_(int w, int h) {
    if (offscreen_w_ == w && offscreen_h_ == h && offscreen_color_.id != 0) {
        return;
    }
    if (offscreen_color_.id != 0) device_->destroy(offscreen_color_);
    offscreen_color_ = tgfx2::TextureHandle{};

    tgfx2::TextureDesc desc;
    desc.width = static_cast<uint32_t>(w);
    desc.height = static_cast<uint32_t>(h);
    desc.format = tgfx2::PixelFormat::RGBA8_UNorm;
    desc.usage = tgfx2::TextureUsage::Sampled
               | tgfx2::TextureUsage::ColorAttachment
               | tgfx2::TextureUsage::CopySrc;
    desc.sample_count = static_cast<uint32_t>(msaa_samples_);
    offscreen_color_ = device_->create_texture(desc);

    offscreen_w_ = w;
    offscreen_h_ = h;
}

void PlotView2D::plot(const double* x, const double* y, size_t n,
                       float cr, float cg, float cb, float ca,
                       double thickness,
                       const char* label) {
    engine_->plot(copy_array(x, n), copy_array(y, n),
                  opt_color(cr, cg, cb, ca), thickness,
                  label ? std::string(label) : std::string());
}

void PlotView2D::scatter(const double* x, const double* y, size_t n,
                          float cr, float cg, float cb, float ca,
                          double size,
                          const char* label) {
    engine_->scatter(copy_array(x, n), copy_array(y, n),
                     opt_color(cr, cg, cb, ca), size,
                     label ? std::string(label) : std::string());
}

void PlotView2D::clear() { engine_->clear(); }
void PlotView2D::fit()   { engine_->fit(); }

void PlotView2D::set_msaa_samples(int samples) {
    if (samples < 1) samples = 1;
    if (samples == msaa_samples_) return;
    msaa_samples_ = samples;
    if (device_ && offscreen_color_.id != 0) {
        device_->destroy(offscreen_color_);
    }
    offscreen_color_ = tgfx2::TextureHandle{};
    offscreen_w_ = 0;
    offscreen_h_ = 0;
}

void PlotView2D::set_view(double x_min, double x_max,
                            double y_min, double y_max) {
    engine_->set_view(x_min, x_max, y_min, y_max);
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

void PlotView2D::render(int width, int height, uint32_t dst_gl_fbo) {
    if (width <= 0 || height <= 0) return;

    ensure_offscreen_(width, height);
    engine_->set_viewport(0, 0, (float)width, (float)height);

    ctx_->begin_frame();

    // Pipelines need to match target FBO sample count (see PlotView3D).
    ctx_->set_sample_count(static_cast<uint32_t>(msaa_samples_));

    const Color4 bg = styles::bg_color();
    const float clear_col[4] = {bg.r, bg.g, bg.b, bg.a};
    // 2D pass: no depth attachment, no depth clear.
    ctx_->begin_pass(offscreen_color_, tgfx2::TextureHandle{},
                     clear_col, 1.0f, false);

    engine_->render(ctx_.get(), font_.get());

    ctx_->end_pass();
    ctx_->end_frame();

    auto* gl_dev = static_cast<tgfx2::OpenGLRenderDevice*>(device_.get());
    gl_dev->blit_to_external_fbo(
        dst_gl_fbo, offscreen_color_,
        0, 0, width, height,
        0, 0, width, height);
}

void PlotView2D::release_gpu() {
    if (engine_) engine_->release_gpu_resources();
    if (font_)   font_->release_gpu();

    if (device_ && offscreen_color_.id != 0) {
        device_->destroy(offscreen_color_);
    }
    offscreen_color_ = tgfx2::TextureHandle{};
    offscreen_w_ = 0;
    offscreen_h_ = 0;
}

}  // namespace tcplot
