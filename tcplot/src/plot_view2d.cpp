// plot_view2d.cpp

#include "tcplot/plot_view2d.hpp"

#include <cmath>
#include <optional>
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
    if (offscreen_fbo_ != 0 && offscreen_w_ == w && offscreen_h_ == h) return;
    if (offscreen_fbo_ != 0) {
        glDeleteFramebuffers(1, &offscreen_fbo_);
        glDeleteTextures(1, &offscreen_color_tex_);
        offscreen_fbo_ = 0;
        offscreen_color_tex_ = 0;
    }

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

    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(prev_fbo));
    offscreen_w_ = w;
    offscreen_h_ = h;
}

void PlotView2D::blit_to_dst_(int w, int h, uint32_t dst_gl_fbo) {
    glBindFramebuffer(GL_READ_FRAMEBUFFER, offscreen_fbo_);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, dst_gl_fbo);
    glBlitFramebuffer(0, 0, w, h,
                      0, 0, w, h,
                      GL_COLOR_BUFFER_BIT, GL_NEAREST);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
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

    tgfx2::TextureDesc color_desc;
    color_desc.width = (uint32_t)width;
    color_desc.height = (uint32_t)height;
    color_desc.format = tgfx2::PixelFormat::RGBA8_UNorm;
    color_desc.usage = tgfx2::TextureUsage::Sampled
                     | tgfx2::TextureUsage::ColorAttachment
                     | tgfx2::TextureUsage::CopySrc;
    tgfx2::TextureHandle color_h =
        device_->register_external_texture(offscreen_color_tex_, color_desc);

    const Color4 bg = styles::bg_color();
    const float clear_col[4] = {bg.r, bg.g, bg.b, bg.a};
    // No depth attachment: engine2d doesn't use depth.
    ctx_->begin_pass(color_h, {}, clear_col, 1.0f, false);

    engine_->render(ctx_.get(), font_.get());

    ctx_->end_pass();

    ctx_->defer_destroy(color_h);
    ctx_->end_frame();

    blit_to_dst_(width, height, dst_gl_fbo);
}

void PlotView2D::release_gpu() {
    if (engine_) engine_->release_gpu_resources();
    if (font_)   font_->release_gpu();

    if (offscreen_fbo_ != 0) {
        glDeleteFramebuffers(1, &offscreen_fbo_);
        glDeleteTextures(1, &offscreen_color_tex_);
        offscreen_fbo_ = 0;
        offscreen_color_tex_ = 0;
        offscreen_w_ = 0;
        offscreen_h_ = 0;
    }
}

}  // namespace tcplot
