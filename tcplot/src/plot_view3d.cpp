// plot_view3d.cpp - tcplot PlotView3D implementation.
//
// All GL-state is confined to tgfx2. Offscreen color + depth are
// tgfx::TextureHandle owned by the device; begin_pass internally
// manages the FBO via OpenGLRenderDevice::get_or_create_fbo. The
// single OpenGL-backend-specific call is blit_to_external_fbo at
// end-of-frame, which composites our offscreen color into the host's
// GL FBO. When Vulkan arrives, that last call dispatches to
// VulkanRenderDevice::present_to_swapchain_image; no code above that
// line is backend-aware.

#include "tcplot/plot_view3d.hpp"

#include <cmath>
#include <utility>

#include <tcbase/input_enums.hpp>
#include <tgfx2/descriptors.hpp>
#include <tgfx2/enums.hpp>
#include <tgfx2/font_atlas.hpp>
#include <tgfx2/handles.hpp>
#include <tgfx2/opengl/opengl_render_device.hpp>
#include <tgfx2/pipeline_cache.hpp>
#include <tgfx2/render_context.hpp>

#include "tcplot/engine3d.hpp"

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

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

PlotView3D::PlotView3D(const std::string& ttf_path)
    : ttf_path_(ttf_path) {
    device_  = std::make_unique<tgfx::OpenGLRenderDevice>();
    cache_   = std::make_unique<tgfx::PipelineCache>(*device_);
    ctx_     = std::make_unique<tgfx::RenderContext2>(*device_, *cache_);
    font_    = std::make_unique<tgfx::FontAtlas>(ttf_path);
    engine_  = std::make_unique<PlotEngine3D>();
}

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

void PlotView3D::plot(const double* x, const double* y, const double* z,
                       size_t n,
                       float cr, float cg, float cb, float ca,
                       double thickness,
                       const char* label) {
    engine_->plot(copy_array(x, n), copy_array(y, n), copy_array(z, n),
                  opt_color(cr, cg, cb, ca),
                  thickness,
                  label ? std::string(label) : std::string());
}

void PlotView3D::scatter(const double* x, const double* y, const double* z,
                          size_t n,
                          float cr, float cg, float cb, float ca,
                          double size,
                          const char* label) {
    engine_->scatter(copy_array(x, n), copy_array(y, n), copy_array(z, n),
                     opt_color(cr, cg, cb, ca),
                     size,
                     label ? std::string(label) : std::string());
}

void PlotView3D::surface(const double* X, const double* Y, const double* Z,
                          uint32_t rows, uint32_t cols,
                          float cr, float cg, float cb, float ca,
                          bool wireframe,
                          const char* label) {
    const size_t n = static_cast<size_t>(rows) * cols;
    engine_->surface(copy_array(X, n), copy_array(Y, n), copy_array(Z, n),
                     rows, cols,
                     opt_color(cr, cg, cb, ca),
                     wireframe,
                     label ? std::string(label) : std::string());
}

void PlotView3D::clear()               { engine_->clear(); }
void PlotView3D::toggle_wireframe()    { engine_->toggle_wireframe(); }
void PlotView3D::toggle_marker_mode()  { engine_->toggle_marker_mode(); }
void PlotView3D::set_z_scale(float s)  { engine_->z_scale = s; }
float PlotView3D::get_z_scale() const  { return engine_->z_scale; }

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
    const float lo_f[3] = {(float)lo[0], (float)lo[1], (float)lo[2]};
    const float hi_f[3] = {(float)hi[0], (float)hi[1], (float)hi[2]};
    engine_->camera.fit_bounds(lo_f, hi_f);
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

void PlotView3D::render(int width, int height, uint32_t dst_gl_fbo) {
    if (width <= 0 || height <= 0) return;

    ensure_offscreen_(width, height);
    engine_->set_viewport(0, 0, (float)width, (float)height);

    ctx_->begin_frame();

    const Color4 bg = styles::bg_color();
    const float clear_col[4] = {bg.r, bg.g, bg.b, bg.a};
    ctx_->begin_pass(offscreen_color_, offscreen_depth_,
                     clear_col, 1.0f, true);
    engine_->render(ctx_.get(), font_.get());
    ctx_->end_pass();
    ctx_->end_frame();

    // Present through the backend-neutral interface. On OpenGL this
    // is a glBlitFramebuffer; Vulkan will implement the analogous
    // swapchain-image blit. Host still passes a GL FBO id via
    // dst_gl_fbo for now — uintptr_t cast is a no-op identity.
    device_->blit_to_external_target(
        static_cast<uintptr_t>(dst_gl_fbo), offscreen_color_,
        0, 0, width, height,
        0, 0, width, height);
}

void PlotView3D::release_gpu() {
    if (engine_) engine_->release_gpu_resources();
    if (font_)   font_->release_gpu();

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
