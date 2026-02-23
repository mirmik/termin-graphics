#pragma once

#include <glad/glad.h>
#include <stdexcept>
#include <string>

#include "tgfx/handles.hpp"
#include "tgfx/types.hpp"
#include "tgfx/opengl/opengl_texture.hpp"
#include "tgfx/tgfx_log.hpp"

namespace termin {

// Texture formats for framebuffer color attachment
enum class FBOFormat {
    RGBA8,
    R8,
    R16F,
    R32F,
    RGBA16F,
    RGBA32F
};

inline FBOFormat parse_fbo_format(const std::string& format_str) {
    if (format_str == "r8") return FBOFormat::R8;
    if (format_str == "r16f") return FBOFormat::R16F;
    if (format_str == "r32f") return FBOFormat::R32F;
    if (format_str == "rgba16f") return FBOFormat::RGBA16F;
    if (format_str == "rgba32f") return FBOFormat::RGBA32F;
    return FBOFormat::RGBA8;
}

// Standard framebuffer with color and depth attachments.
// Supports MSAA (samples > 1) and various texture formats.
class OpenGLFramebufferHandle : public FramebufferHandle {
public:
    OpenGLFramebufferHandle(int width, int height, int samples = 1, FBOFormat format = FBOFormat::RGBA8, TextureFilter filter = TextureFilter::LINEAR)
        : fbo_(0), color_tex_(0), depth_rb_(0),
          width_(width), height_(height), samples_(samples), format_(format), filter_(filter),
          owns_attachments_(true), color_ref_(0) {
        create();
    }

    static std::unique_ptr<OpenGLFramebufferHandle> create_external(
        uint32_t fbo_id, int width, int height
    ) {
        auto handle = std::unique_ptr<OpenGLFramebufferHandle>(
            new OpenGLFramebufferHandle(fbo_id, width, height, true)
        );
        return handle;
    }

private:
    OpenGLFramebufferHandle(uint32_t fbo_id, int width, int height, bool /*external*/)
        : fbo_(fbo_id), color_tex_(0), depth_rb_(0),
          width_(width), height_(height), samples_(1), format_(FBOFormat::RGBA8),
          owns_attachments_(false), color_ref_(0) {}

public:
    ~OpenGLFramebufferHandle() override;

    void resize(int width, int height) override {
        if (width == width_ && height == height_ && fbo_ != 0) {
            return;
        }
        if (!owns_attachments_) {
            width_ = width;
            height_ = height;
            return;
        }
        release();
        width_ = width;
        height_ = height;
        create();
    }

    void release() override {
        if (!owns_attachments_) {
            fbo_ = 0;
            return;
        }
        if (fbo_ != 0) { glDeleteFramebuffers(1, &fbo_); fbo_ = 0; }
        if (color_tex_ != 0) { glDeleteTextures(1, &color_tex_); color_tex_ = 0; }
        if (depth_rb_ != 0) { glDeleteRenderbuffers(1, &depth_rb_); depth_rb_ = 0; }
    }

    void set_external_target(uint32_t fbo_id, int width, int height) override {
        release();
        owns_attachments_ = false;
        fbo_ = fbo_id;
        width_ = width;
        height_ = height;
        color_tex_ = 0;
        depth_rb_ = 0;
    }

    uint32_t get_fbo_id() const override { return fbo_; }
    int get_width() const override { return width_; }
    int get_height() const override { return height_; }
    int get_samples() const override { return samples_; }
    bool is_msaa() const override { return samples_ > 1; }

    std::string get_format() const override {
        switch (format_) {
            case FBOFormat::R8: return "r8";
            case FBOFormat::R16F: return "r16f";
            case FBOFormat::R32F: return "r32f";
            case FBOFormat::RGBA16F: return "rgba16f";
            case FBOFormat::RGBA32F: return "rgba32f";
            default: return "rgba8";
        }
    }

    std::string get_actual_gl_format() const override {
        if (color_tex_ == 0) return "no_texture";
        GLint internal_format = 0;
        GLenum target = samples_ > 1 ? GL_TEXTURE_2D_MULTISAMPLE : GL_TEXTURE_2D;
        glBindTexture(target, color_tex_);
        glGetTexLevelParameteriv(target, 0, GL_TEXTURE_INTERNAL_FORMAT, &internal_format);
        glBindTexture(target, 0);
        switch (internal_format) {
            case GL_R8: return "GL_R8";
            case GL_R16F: return "GL_R16F";
            case GL_R32F: return "GL_R32F";
            case GL_RGBA8: return "GL_RGBA8";
            case GL_RGBA16F: return "GL_RGBA16F";
            case GL_RGBA32F: return "GL_RGBA32F";
            case GL_RGB8: return "GL_RGB8";
            case GL_RGB16F: return "GL_RGB16F";
            case GL_DEPTH_COMPONENT16: return "GL_DEPTH16";
            case GL_DEPTH_COMPONENT24: return "GL_DEPTH24";
            case GL_DEPTH_COMPONENT32F: return "GL_DEPTH32F";
            default: return "GL_0x" + std::to_string(internal_format);
        }
    }

    int get_actual_gl_width() const { if (color_tex_ == 0) return 0; GLint w = 0; GLenum target = samples_ > 1 ? GL_TEXTURE_2D_MULTISAMPLE : GL_TEXTURE_2D; glBindTexture(target, color_tex_); glGetTexLevelParameteriv(target, 0, GL_TEXTURE_WIDTH, &w); glBindTexture(target, 0); return w; }
    int get_actual_gl_height() const { if (color_tex_ == 0) return 0; GLint h = 0; GLenum target = samples_ > 1 ? GL_TEXTURE_2D_MULTISAMPLE : GL_TEXTURE_2D; glBindTexture(target, color_tex_); glGetTexLevelParameteriv(target, 0, GL_TEXTURE_HEIGHT, &h); glBindTexture(target, 0); return h; }
    int get_actual_gl_samples() const { if (color_tex_ == 0) return 0; if (samples_ <= 1) return 1; GLint s = 0; glBindTexture(GL_TEXTURE_2D_MULTISAMPLE, color_tex_); glGetTexLevelParameteriv(GL_TEXTURE_2D_MULTISAMPLE, 0, GL_TEXTURE_SAMPLES, &s); glBindTexture(GL_TEXTURE_2D_MULTISAMPLE, 0); return s; }

    std::string get_filter() const override {
        switch (filter_) {
            case TextureFilter::LINEAR: return "linear";
            case TextureFilter::NEAREST: return "nearest";
            default: return "unknown";
        }
    }

    std::string get_actual_gl_filter() const override {
        if (color_tex_ == 0) return "no_texture";
        if (samples_ > 1) return "msaa_no_filter";
        GLint min_filter = 0, mag_filter = 0;
        glBindTexture(GL_TEXTURE_2D, color_tex_);
        glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, &min_filter);
        glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, &mag_filter);
        glBindTexture(GL_TEXTURE_2D, 0);
        std::string result;
        switch (min_filter) {
            case GL_NEAREST: result = "GL_NEAREST"; break;
            case GL_LINEAR: result = "GL_LINEAR"; break;
            default: result = "GL_0x" + std::to_string(min_filter); break;
        }
        result += "/";
        switch (mag_filter) {
            case GL_NEAREST: result += "GL_NEAREST"; break;
            case GL_LINEAR: result += "GL_LINEAR"; break;
            default: result += "GL_0x" + std::to_string(mag_filter); break;
        }
        return result;
    }

    GPUTextureHandle* color_texture() override {
        color_ref_.set_tex_id(color_tex_);
        color_ref_.set_target(samples_ > 1 ? GL_TEXTURE_2D_MULTISAMPLE : GL_TEXTURE_2D);
        return &color_ref_;
    }

    GPUTextureHandle* depth_texture() override {
        return nullptr;
    }

private:
    void get_format_params(GLenum& internal_format, GLenum& pixel_format, GLenum& pixel_type) const {
        switch (format_) {
            case FBOFormat::R8: internal_format = GL_R8; pixel_format = GL_RED; pixel_type = GL_UNSIGNED_BYTE; break;
            case FBOFormat::R16F: internal_format = GL_R16F; pixel_format = GL_RED; pixel_type = GL_FLOAT; break;
            case FBOFormat::R32F: internal_format = GL_R32F; pixel_format = GL_RED; pixel_type = GL_FLOAT; break;
            case FBOFormat::RGBA16F: internal_format = GL_RGBA16F; pixel_format = GL_RGBA; pixel_type = GL_FLOAT; break;
            case FBOFormat::RGBA32F: internal_format = GL_RGBA32F; pixel_format = GL_RGBA; pixel_type = GL_FLOAT; break;
            default: internal_format = GL_RGBA8; pixel_format = GL_RGBA; pixel_type = GL_UNSIGNED_BYTE; break;
        }
    }

    void create() {
        glGenFramebuffers(1, &fbo_);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo_);

        GLenum internal_format, pixel_format, pixel_type;
        get_format_params(internal_format, pixel_format, pixel_type);

        if (samples_ > 1) {
            glGenTextures(1, &color_tex_);
            glBindTexture(GL_TEXTURE_2D_MULTISAMPLE, color_tex_);
            glTexImage2DMultisample(GL_TEXTURE_2D_MULTISAMPLE, samples_, internal_format, width_, height_, GL_TRUE);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D_MULTISAMPLE, color_tex_, 0);

            glGenRenderbuffers(1, &depth_rb_);
            glBindRenderbuffer(GL_RENDERBUFFER, depth_rb_);
            glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples_, GL_DEPTH_COMPONENT24, width_, height_);
            glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depth_rb_);
        } else {
            glGenTextures(1, &color_tex_);
            glBindTexture(GL_TEXTURE_2D, color_tex_);
            glTexImage2D(GL_TEXTURE_2D, 0, internal_format, width_, height_, 0, pixel_format, pixel_type, nullptr);

            GLenum gl_filter = (filter_ == TextureFilter::NEAREST) ? GL_NEAREST : GL_LINEAR;
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, gl_filter);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, gl_filter);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, color_tex_, 0);

            glGenRenderbuffers(1, &depth_rb_);
            glBindRenderbuffer(GL_RENDERBUFFER, depth_rb_);
            glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, width_, height_);
            glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depth_rb_);
        }

        GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
        if (status != GL_FRAMEBUFFER_COMPLETE) {
            tgfx::Log::error("[OpenGLFramebuffer] Framebuffer incomplete!");
            tgfx::Log::error("  Status: 0x%x", status);
            tgfx::Log::error("  Size: %dx%d", width_, height_);
            tgfx::Log::error("  Samples: %d", samples_);
            tgfx::Log::error("  Format: %s (internal=0x%x)", get_format().c_str(), internal_format);
            tgfx::Log::error("  FBO: %u, Color tex: %u, Depth RB: %u", fbo_, color_tex_, depth_rb_);
            GLenum err = glGetError();
            if (err != GL_NO_ERROR) {
                tgfx::Log::error("  OpenGL error: 0x%x", err);
            }
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            throw std::runtime_error("Framebuffer incomplete: 0x" + std::to_string(status));
        }

        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    GLuint fbo_;
    GLuint color_tex_;
    GLuint depth_rb_;
    int width_;
    int height_;
    int samples_;
    FBOFormat format_;
    TextureFilter filter_;
    bool owns_attachments_;
    OpenGLTextureRef color_ref_;
};

// Shadow framebuffer with depth texture for shadow mapping.
class OpenGLShadowFramebufferHandle : public FramebufferHandle {
public:
    OpenGLShadowFramebufferHandle(int width, int height)
        : fbo_(0), depth_tex_(0), width_(width), height_(height),
          owns_attachments_(true), depth_ref_(0) {
        create();
    }

    ~OpenGLShadowFramebufferHandle() override;

    void resize(int width, int height) override {
        if (width == width_ && height == height_ && fbo_ != 0) return;
        if (!owns_attachments_) { width_ = width; height_ = height; return; }
        release();
        width_ = width;
        height_ = height;
        create();
    }

    void release() override {
        if (!owns_attachments_) { fbo_ = 0; return; }
        if (fbo_ != 0) { glDeleteFramebuffers(1, &fbo_); fbo_ = 0; }
        if (depth_tex_ != 0) { glDeleteTextures(1, &depth_tex_); depth_tex_ = 0; }
    }

    void set_external_target(uint32_t fbo_id, int width, int height) override {
        release();
        owns_attachments_ = false;
        fbo_ = fbo_id;
        width_ = width;
        height_ = height;
        depth_tex_ = 0;
    }

    uint32_t get_fbo_id() const override { return fbo_; }
    int get_width() const override { return width_; }
    int get_height() const override { return height_; }
    int get_samples() const override { return 1; }
    bool is_msaa() const override { return false; }
    std::string get_format() const override { return "depth24"; }

    GPUTextureHandle* color_texture() override { return depth_texture(); }
    GPUTextureHandle* depth_texture() override { depth_ref_.set_tex_id(depth_tex_); return &depth_ref_; }

private:
    void create() {
        glGenFramebuffers(1, &fbo_);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo_);

        glGenTextures(1, &depth_tex_);
        glBindTexture(GL_TEXTURE_2D, depth_tex_);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, width_, height_, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);

        float border[] = {1.0f, 1.0f, 1.0f, 1.0f};
        glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, border);

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);

        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, depth_tex_, 0);

        glDrawBuffer(GL_NONE);
        glReadBuffer(GL_NONE);

        GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
        if (status != GL_FRAMEBUFFER_COMPLETE) {
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            throw std::runtime_error("Shadow framebuffer incomplete: 0x" + std::to_string(status));
        }

        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    GLuint fbo_;
    GLuint depth_tex_;
    int width_;
    int height_;
    bool owns_attachments_;
    OpenGLTextureRef depth_ref_;
};

} // namespace termin
