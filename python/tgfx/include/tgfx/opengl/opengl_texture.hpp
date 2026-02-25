#pragma once

#include <glad/glad.h>
#include <cstdint>

#include "tgfx/handles.hpp"

namespace termin {

class OpenGLTextureHandle : public GPUTextureHandle {
public:
    OpenGLTextureHandle(
        const uint8_t* data,
        int width,
        int height,
        int channels = 4,
        bool mipmap = true,
        bool clamp = false
    ) : handle_(0), width_(width), height_(height), channels_(channels) {
        upload(data, mipmap, clamp);
    }

    ~OpenGLTextureHandle() override {
        release();
    }

    void bind(int unit) override {
        glActiveTexture(GL_TEXTURE0 + unit);
        glBindTexture(GL_TEXTURE_2D, handle_);
    }

    void release() override {
        if (handle_ != 0) {
            glDeleteTextures(1, &handle_);
            handle_ = 0;
        }
    }

    uint32_t get_id() const override { return handle_; }
    int get_width() const override { return width_; }
    int get_height() const override { return height_; }

    bool is_valid() const override {
        return handle_ != 0 && glIsTexture(handle_) == GL_TRUE;
    }

    void update_data(const uint8_t* data, int width, int height, int channels) {
        if (handle_ == 0) return;
        GLenum format = (channels == 1) ? GL_RED : GL_RGBA;
        glBindTexture(GL_TEXTURE_2D, handle_);
        if (width == width_ && height == height_ && channels == channels_) {
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height,
                            format, GL_UNSIGNED_BYTE, data);
        } else {
            GLenum internal_format = format;
            glTexImage2D(GL_TEXTURE_2D, 0, internal_format,
                         width, height, 0, format, GL_UNSIGNED_BYTE, data);
            width_ = width;
            height_ = height;
            channels_ = channels;
        }
        glBindTexture(GL_TEXTURE_2D, 0);
    }

    void update_data_region(const uint8_t* data, int full_width,
                            int x, int y, int region_w, int region_h,
                            int channels) {
        if (handle_ == 0) return;
        GLenum format = (channels == 1) ? GL_RED : GL_RGBA;
        glBindTexture(GL_TEXTURE_2D, handle_);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, full_width);
        const uint8_t* region_ptr = data + ((size_t)y * full_width + x) * channels;
        glTexSubImage2D(GL_TEXTURE_2D, 0, x, y, region_w, region_h,
                        format, GL_UNSIGNED_BYTE, region_ptr);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
        glBindTexture(GL_TEXTURE_2D, 0);
    }

private:
    void upload(const uint8_t* data, bool mipmap, bool clamp) {
        glGenTextures(1, &handle_);
        glBindTexture(GL_TEXTURE_2D, handle_);

        GLenum internal_format = (channels_ == 1) ? GL_RED : GL_RGBA;
        GLenum format = internal_format;

        glTexImage2D(
            GL_TEXTURE_2D, 0, internal_format,
            width_, height_, 0,
            format, GL_UNSIGNED_BYTE, data
        );

        if (mipmap) {
            glGenerateMipmap(GL_TEXTURE_2D);
        }

        GLenum min_filter = mipmap ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR;
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, min_filter);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        GLenum wrap_mode = clamp ? GL_CLAMP_TO_EDGE : GL_REPEAT;
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap_mode);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap_mode);

        // For single channel, swizzle to show as grayscale
        if (channels_ == 1) {
            GLint swizzle[] = {GL_RED, GL_RED, GL_RED, GL_RED};
            glTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_RGBA, swizzle);
        }

        glBindTexture(GL_TEXTURE_2D, 0);
    }

    GLuint handle_;
    int width_;
    int height_;
    int channels_;
};

// Lightweight wrapper over existing GL texture.
// Does not own the texture - release() is a no-op.
class OpenGLTextureRef : public GPUTextureHandle {
public:
    GLuint tex_id_;
    GLenum target_;
    int width_;
    int height_;

    explicit OpenGLTextureRef(GLuint tex_id, int width = 0, int height = 0, GLenum target = GL_TEXTURE_2D)
        : tex_id_(tex_id), target_(target), width_(width), height_(height) {}

    void bind(int unit) override {
        glActiveTexture(GL_TEXTURE0 + unit);
        glBindTexture(target_, tex_id_);
    }

    void release() override {
        // Does not own the texture
    }

    uint32_t get_id() const override { return tex_id_; }
    int get_width() const override { return width_; }
    int get_height() const override { return height_; }

    bool is_valid() const override {
        return tex_id_ != 0 && glIsTexture(tex_id_) == GL_TRUE;
    }

    void set_tex_id(GLuint id) { tex_id_ = id; }
    void set_target(GLenum target) { target_ = target; }
};

} // namespace termin
