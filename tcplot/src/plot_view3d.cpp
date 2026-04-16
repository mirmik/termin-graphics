// plot_view3d.cpp - implementation.

#include "tcplot/plot_view3d.hpp"

#include <cmath>
#include <utility>

#include <glad/glad.h>

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
    // A NaN anywhere means "unset — use palette cycle". Matches the
    // sentinel documented in plot_view3d.hpp.
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
    device_  = std::make_unique<tgfx2::OpenGLRenderDevice>();
    cache_   = std::make_unique<tgfx2::PipelineCache>(*device_);
    ctx_     = std::make_unique<tgfx2::RenderContext2>(*device_, *cache_);
    font_    = std::make_unique<tgfx2::FontAtlas>(ttf_path);
    engine_  = std::make_unique<PlotEngine3D>();
}

PlotView3D::~PlotView3D() {
    release_gpu();
}

// ---------------------------------------------------------------------------
// Offscreen FBO management
// ---------------------------------------------------------------------------

void PlotView3D::ensure_offscreen_(int w, int h) {
    if (offscreen_fbo_ != 0 && offscreen_w_ == w && offscreen_h_ == h) return;

    if (offscreen_fbo_ != 0) {
        glDeleteFramebuffers(1, &offscreen_fbo_);
        glDeleteTextures(1, &offscreen_color_tex_);
        glDeleteTextures(1, &offscreen_depth_tex_);
        offscreen_fbo_ = 0;
        offscreen_color_tex_ = 0;
        offscreen_depth_tex_ = 0;
    }

    // Preserve caller's currently-bound FBO so we restore it on exit.
    GLint prev_fbo = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prev_fbo);

    glGenFramebuffers(1, &offscreen_fbo_);
    glBindFramebuffer(GL_FRAMEBUFFER, offscreen_fbo_);

    glGenTextures(1, &offscreen_color_tex_);
    glBindTexture(GL_TEXTURE_2D, offscreen_color_tex_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, offscreen_color_tex_, 0);

    glGenTextures(1, &offscreen_depth_tex_);
    glBindTexture(GL_TEXTURE_2D, offscreen_depth_tex_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT32F, w, h, 0,
                 GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                           GL_TEXTURE_2D, offscreen_depth_tex_, 0);

    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(prev_fbo));

    offscreen_w_ = w;
    offscreen_h_ = h;
}

void PlotView3D::blit_to_dst_(int w, int h, uint32_t dst_gl_fbo) {
    glBindFramebuffer(GL_READ_FRAMEBUFFER, offscreen_fbo_);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, dst_gl_fbo);
    glBlitFramebuffer(0, 0, w, h,
                      0, 0, w, h,
                      GL_COLOR_BUFFER_BIT, GL_NEAREST);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
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

    // Wrap our raw-GL offscreen textures as non-owning tgfx2 handles.
    // defer_destroy queues them for removal at end_frame() — tgfx2
    // releases the wrapper but leaves the GL objects alone because
    // register_external_texture marked them `external`.
    tgfx2::TextureDesc color_desc;
    color_desc.width = (uint32_t)width;
    color_desc.height = (uint32_t)height;
    color_desc.format = tgfx2::PixelFormat::RGBA8_UNorm;
    color_desc.usage = tgfx2::TextureUsage::Sampled
                     | tgfx2::TextureUsage::ColorAttachment
                     | tgfx2::TextureUsage::CopySrc;
    tgfx2::TextureHandle color_h =
        device_->register_external_texture(offscreen_color_tex_, color_desc);

    tgfx2::TextureDesc depth_desc;
    depth_desc.width = (uint32_t)width;
    depth_desc.height = (uint32_t)height;
    depth_desc.format = tgfx2::PixelFormat::D32F;
    depth_desc.usage = tgfx2::TextureUsage::DepthStencilAttachment;
    tgfx2::TextureHandle depth_h =
        device_->register_external_texture(offscreen_depth_tex_, depth_desc);

    const Color4 bg = styles::bg_color();
    const float clear_col[4] = {bg.r, bg.g, bg.b, bg.a};
    ctx_->begin_pass(color_h, depth_h, clear_col, 1.0f, true);

    engine_->render(ctx_.get(), font_.get());

    ctx_->end_pass();

    // Release external wrappers at end of frame.
    ctx_->defer_destroy(color_h);
    ctx_->defer_destroy(depth_h);

    ctx_->end_frame();

    // Composite to the caller's framebuffer.
    blit_to_dst_(width, height, dst_gl_fbo);
}

void PlotView3D::release_gpu() {
    if (engine_) engine_->release_gpu_resources();
    if (font_)   font_->release_gpu();

    if (offscreen_fbo_ != 0) {
        glDeleteFramebuffers(1, &offscreen_fbo_);
        glDeleteTextures(1, &offscreen_color_tex_);
        glDeleteTextures(1, &offscreen_depth_tex_);
        offscreen_fbo_ = 0;
        offscreen_color_tex_ = 0;
        offscreen_depth_tex_ = 0;
        offscreen_w_ = 0;
        offscreen_h_ = 0;
    }
}

}  // namespace tcplot
