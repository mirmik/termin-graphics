#include "tgfx2/opengl/opengl_render_device.hpp"
#include "gl_web_compat.hpp"

#include "tgfx2/pixel_format_utils.hpp"

#include <cstring>
#include <vector>

#include <tcbase/tc_log.h>

namespace tgfx {

    static void flip_rows_in_place(float* data, uint32_t width, uint32_t height, uint32_t channels) {
        if (!data || width == 0 || height < 2 || channels == 0)
            return;
        const size_t row_values = static_cast<size_t>(width) * channels;
        std::vector<float> tmp(row_values);
        for (uint32_t y = 0; y < height / 2; ++y) {
            float* top = data + static_cast<size_t>(y) * row_values;
            float* bottom = data + static_cast<size_t>(height - y - 1) * row_values;
            std::memcpy(tmp.data(), top, row_values * sizeof(float));
            std::memcpy(top, bottom, row_values * sizeof(float));
            std::memcpy(bottom, tmp.data(), row_values * sizeof(float));
        }
    }
    bool OpenGLRenderDevice::read_texture_rgba_float(TextureHandle tex, float* out) {
        auto* t = get_texture(tex);
        if (!t || !out)
            return false;

        GLint prev_read_fbo = 0;
        glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &prev_read_fbo);

        GLuint fbo = 0;
        glGenFramebuffers(1, &fbo);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo);
        glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, t->target, t->gl_id, 0);

        bool ok = glCheckFramebufferStatus(GL_READ_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
        if (ok) {
            glReadBuffer(GL_COLOR_ATTACHMENT0);
            glReadPixels(0,
                         0,
                         static_cast<GLsizei>(t->desc.width),
                         static_cast<GLsizei>(t->desc.height),
                         GL_RGBA,
                         GL_FLOAT,
                         out);
            flip_rows_in_place(out, t->desc.width, t->desc.height, 4);
        }

        glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(prev_read_fbo));
        glDeleteFramebuffers(1, &fbo);
        return ok;
    }

    bool OpenGLRenderDevice::read_texture_depth_float(TextureHandle tex, float* out) {
        auto* t = get_texture(tex);
        if (!t || !out)
            return false;

        GLint prev_read_fbo = 0;
        glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &prev_read_fbo);

        GLuint fbo = 0;
        glGenFramebuffers(1, &fbo);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo);
        glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, t->target, t->gl_id, 0);
        // Depth-only FBO — disable color read/draw explicitly, otherwise
        // some drivers report incomplete.
        gl_web_compat::set_draw_buffer(GL_NONE);
        glReadBuffer(GL_NONE);

        bool ok = glCheckFramebufferStatus(GL_READ_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
        if (ok) {
            glReadPixels(0,
                         0,
                         static_cast<GLsizei>(t->desc.width),
                         static_cast<GLsizei>(t->desc.height),
                         GL_DEPTH_COMPONENT,
                         GL_FLOAT,
                         out);
            flip_rows_in_place(out, t->desc.width, t->desc.height, 1);
        }

        glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(prev_read_fbo));
        glDeleteFramebuffers(1, &fbo);
        return ok;
    }

    bool OpenGLRenderDevice::read_pixel_rgba8(TextureHandle tex, int x, int y, float out_rgba[4]) {
        auto* t = get_texture(tex);
        if (!t || !out_rgba)
            return false;

        GLint prev_read_fbo = 0;
        glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &prev_read_fbo);

        GLuint fbo = 0;
        glGenFramebuffers(1, &fbo);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo);
        glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, t->target, t->gl_id, 0);

        bool ok = glCheckFramebufferStatus(GL_READ_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
        if (ok) {
            // IRenderDevice::read_pixel_rgba8 takes top-down pixel coords
            // (window-space convention). glReadPixels wants bottom-up, so
            // flip Y here — stays a backend-local detail instead of leaking
            // into the caller.
            const int gl_y = gl_native_readback_y(gl_coordinates_, static_cast<int>(t->desc.height), y);
            uint8_t pixel[4] = {0, 0, 0, 0};
            glReadBuffer(GL_COLOR_ATTACHMENT0);
            glReadPixels(x, gl_y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
            out_rgba[0] = pixel[0] / 255.0f;
            out_rgba[1] = pixel[1] / 255.0f;
            out_rgba[2] = pixel[2] / 255.0f;
            out_rgba[3] = pixel[3] / 255.0f;
        }

        glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(prev_read_fbo));
        glDeleteFramebuffers(1, &fbo);
        return ok;
    }

    bool OpenGLRenderDevice::read_pixel_depth_float(TextureHandle tex, int x, int y, float* out_depth) {
        auto* t = get_texture(tex);
        if (!t || !out_depth)
            return false;

        GLint prev_read_fbo = 0;
        glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &prev_read_fbo);

        GLuint fbo = 0;
        glGenFramebuffers(1, &fbo);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo);
        glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, t->target, t->gl_id, 0);
        gl_web_compat::set_draw_buffer(GL_NONE);
        glReadBuffer(GL_NONE);

        bool ok = glCheckFramebufferStatus(GL_READ_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
        if (ok) {
            const int gl_y = gl_native_readback_y(gl_coordinates_, static_cast<int>(t->desc.height), y);
            glReadPixels(x, gl_y, 1, 1, GL_DEPTH_COMPONENT, GL_FLOAT, out_depth);
        }

        glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(prev_read_fbo));
        glDeleteFramebuffers(1, &fbo);
        return ok;
    }

    uint64_t OpenGLRenderDevice::request_pixel_rgba8(TextureHandle texture, int x, int y) {
        return request_pixel_readback(texture, x, y, PixelReadbackKind::Rgba8);
    }

    bool OpenGLRenderDevice::poll_pixel_rgba8(uint64_t request_id, float out_rgba[4]) {
        if (request_id == 0 || !out_rgba)
            return false;
        PixelReadbackSlot* slot = find_pixel_readback_slot(request_id);
        if (!slot || !pixel_readback_ready(*slot, true))
            return false;
        if (slot->kind != PixelReadbackKind::Rgba8) {
            tc_log_error("OpenGLRenderDevice::poll_pixel_rgba8: request %llu has the wrong kind",
                         static_cast<unsigned long long>(request_id));
            release_pixel_readback_slot(*slot);
            return false;
        }

        GLint previous_pbo = 0;
        glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING, &previous_pbo);
        glBindBuffer(GL_PIXEL_PACK_BUFFER, slot->pbo);
        const void* mapped = glMapBufferRange(GL_PIXEL_PACK_BUFFER, 0, 4, GL_MAP_READ_BIT);
        if (!mapped) {
            tc_log_error("OpenGLRenderDevice::poll_pixel_rgba8: failed to map request %llu",
                         static_cast<unsigned long long>(request_id));
            glBindBuffer(GL_PIXEL_PACK_BUFFER, static_cast<GLuint>(previous_pbo));
            release_pixel_readback_slot(*slot);
            return false;
        }

        const auto* pixel = static_cast<const uint8_t*>(mapped);
        out_rgba[0] = pixel[0] / 255.0f;
        out_rgba[1] = pixel[1] / 255.0f;
        out_rgba[2] = pixel[2] / 255.0f;
        out_rgba[3] = pixel[3] / 255.0f;
        const GLboolean unmap_ok = glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
        glBindBuffer(GL_PIXEL_PACK_BUFFER, static_cast<GLuint>(previous_pbo));
        release_pixel_readback_slot(*slot);
        if (unmap_ok != GL_TRUE) {
            tc_log_error("OpenGLRenderDevice::poll_pixel_rgba8: PBO data became invalid for request %llu",
                         static_cast<unsigned long long>(request_id));
            return false;
        }
        return true;
    }

    uint64_t OpenGLRenderDevice::request_pixel_depth_float(TextureHandle texture, int x, int y) {
        return request_pixel_readback(texture, x, y, PixelReadbackKind::DepthF32);
    }

    bool OpenGLRenderDevice::poll_pixel_depth_float(uint64_t request_id, float* out_depth) {
        if (request_id == 0 || !out_depth)
            return false;
        PixelReadbackSlot* slot = find_pixel_readback_slot(request_id);
        if (!slot || !pixel_readback_ready(*slot, true))
            return false;
        if (slot->kind != PixelReadbackKind::DepthF32) {
            tc_log_error("OpenGLRenderDevice::poll_pixel_depth_float: request %llu has the wrong kind",
                         static_cast<unsigned long long>(request_id));
            release_pixel_readback_slot(*slot);
            return false;
        }

        GLint previous_pbo = 0;
        glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING, &previous_pbo);
        glBindBuffer(GL_PIXEL_PACK_BUFFER, slot->pbo);
        const void* mapped = glMapBufferRange(GL_PIXEL_PACK_BUFFER, 0, sizeof(float), GL_MAP_READ_BIT);
        if (!mapped) {
            tc_log_error("OpenGLRenderDevice::poll_pixel_depth_float: failed to map request %llu",
                         static_cast<unsigned long long>(request_id));
            glBindBuffer(GL_PIXEL_PACK_BUFFER, static_cast<GLuint>(previous_pbo));
            release_pixel_readback_slot(*slot);
            return false;
        }

        std::memcpy(out_depth, mapped, sizeof(float));
        const GLboolean unmap_ok = glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
        glBindBuffer(GL_PIXEL_PACK_BUFFER, static_cast<GLuint>(previous_pbo));
        release_pixel_readback_slot(*slot);
        if (unmap_ok != GL_TRUE) {
            tc_log_error("OpenGLRenderDevice::poll_pixel_depth_float: PBO data became invalid for request %llu",
                         static_cast<unsigned long long>(request_id));
            return false;
        }
        return true;
    }

    uint64_t OpenGLRenderDevice::request_pixel_readback(TextureHandle handle, int x, int y, PixelReadbackKind kind) {
        GLTexture* texture = get_texture(handle);
        if (!texture || !texture->gl_id)
            return 0;
        if (x < 0 || y < 0 || static_cast<uint32_t>(x) >= texture->desc.width ||
            static_cast<uint32_t>(y) >= texture->desc.height) {
            tc_log_error(
                "OpenGLRenderDevice::request_pixel_readback: coordinates (%d,%d) are outside texture %u (%ux%u)",
                x,
                y,
                handle.id,
                texture->desc.width,
                texture->desc.height);
            return 0;
        }
        if (texture->desc.sample_count != 1) {
            tc_log_error("OpenGLRenderDevice::request_pixel_readback: multisampled texture %u is unsupported",
                         handle.id);
            return 0;
        }
        if (kind == PixelReadbackKind::Rgba8 && !is_rgba8_family(texture->desc.format)) {
            tc_log_error("OpenGLRenderDevice::request_pixel_readback: texture %u is not RGBA8/BGRA8", handle.id);
            return 0;
        }
        if (kind == PixelReadbackKind::DepthF32 && texture->desc.format != PixelFormat::D32F) {
            tc_log_error("OpenGLRenderDevice::request_pixel_readback: texture %u is not D32F", handle.id);
            return 0;
        }

        PixelReadbackSlot* slot = acquire_pixel_readback_slot(kind);
        if (!slot) {
            tc_log_error("OpenGLRenderDevice::request_pixel_readback: all %zu slots are pending",
                         kPixelReadbackSlotCount);
            return 0;
        }
        if (!slot->pbo) {
            glGenBuffers(1, &slot->pbo);
            if (!slot->pbo) {
                tc_log_error("OpenGLRenderDevice::request_pixel_readback: PBO creation failed");
                return 0;
            }
            GLint previous_pbo = 0;
            glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING, &previous_pbo);
            glBindBuffer(GL_PIXEL_PACK_BUFFER, slot->pbo);
            glBufferData(GL_PIXEL_PACK_BUFFER, 4, nullptr, GL_STREAM_READ);
            glBindBuffer(GL_PIXEL_PACK_BUFFER, static_cast<GLuint>(previous_pbo));
        }
        if (!pixel_readback_fbo_) {
            glGenFramebuffers(1, &pixel_readback_fbo_);
            if (!pixel_readback_fbo_) {
                tc_log_error("OpenGLRenderDevice::request_pixel_readback: framebuffer creation failed");
                return 0;
            }
        }

        GLint previous_read_fbo = 0;
        GLint previous_draw_fbo = 0;
        GLint previous_pbo = 0;
        GLint previous_pack_alignment = 4;
        glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &previous_read_fbo);
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &previous_draw_fbo);
        glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING, &previous_pbo);
        glGetIntegerv(GL_PACK_ALIGNMENT, &previous_pack_alignment);

        glBindFramebuffer(GL_FRAMEBUFFER, pixel_readback_fbo_);
        GLenum read_format = GL_RGBA;
        GLenum read_type = GL_UNSIGNED_BYTE;
        if (kind == PixelReadbackKind::Rgba8) {
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, texture->target, 0, 0);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, texture->target, texture->gl_id, 0);
            gl_web_compat::set_draw_buffer(GL_COLOR_ATTACHMENT0);
            glReadBuffer(GL_COLOR_ATTACHMENT0);
        } else {
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, texture->target, 0, 0);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, texture->target, texture->gl_id, 0);
            gl_web_compat::set_draw_buffer(GL_NONE);
            glReadBuffer(GL_NONE);
            read_format = GL_DEPTH_COMPONENT;
            read_type = GL_FLOAT;
        }

        const GLenum framebuffer_status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
        if (framebuffer_status != GL_FRAMEBUFFER_COMPLETE) {
            tc_log_error("OpenGLRenderDevice::request_pixel_readback: framebuffer incomplete for texture %u: 0x%04x",
                         handle.id,
                         static_cast<unsigned>(framebuffer_status));
            glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(previous_read_fbo));
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(previous_draw_fbo));
            return 0;
        }

        glBindBuffer(GL_PIXEL_PACK_BUFFER, slot->pbo);
        glPixelStorei(GL_PACK_ALIGNMENT, 4);
        // IRenderDevice coordinates are top-down; OpenGL readback coordinates
        // remain bottom-up even with the upper-left clip-control contract.
        const int gl_y = gl_native_readback_y(gl_coordinates_, static_cast<int>(texture->desc.height), y);
        glReadPixels(x, gl_y, 1, 1, read_format, read_type, nullptr);
        slot->fence = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);

        glPixelStorei(GL_PACK_ALIGNMENT, previous_pack_alignment);
        glBindBuffer(GL_PIXEL_PACK_BUFFER, static_cast<GLuint>(previous_pbo));
        glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(previous_read_fbo));
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(previous_draw_fbo));

        const GLenum error = glGetError();
        if (error != GL_NO_ERROR || !slot->fence) {
            tc_log_error(
                "OpenGLRenderDevice::request_pixel_readback: read/fence failed for texture %u: GL error 0x%04x",
                handle.id,
                static_cast<unsigned>(error));
            release_pixel_readback_slot(*slot);
            return 0;
        }

        slot->request_id = next_pixel_readback_id_++;
        if (slot->request_id == 0)
            slot->request_id = next_pixel_readback_id_++;
        slot->issue_sequence = pixel_readback_issue_sequence_++;
        slot->kind = kind;
        slot->active = true;
        return slot->request_id;
    }

    OpenGLRenderDevice::PixelReadbackSlot* OpenGLRenderDevice::acquire_pixel_readback_slot(PixelReadbackKind kind) {
        (void)kind;
        for (PixelReadbackSlot& slot : pixel_readback_slots_) {
            if (!slot.active)
                return &slot;
        }

        PixelReadbackSlot* oldest_completed = nullptr;
        for (PixelReadbackSlot& slot : pixel_readback_slots_) {
            if (pixel_readback_ready(slot, true) &&
                (!oldest_completed || slot.issue_sequence < oldest_completed->issue_sequence)) {
                oldest_completed = &slot;
            }
        }
        if (!oldest_completed)
            return nullptr;

        tc_log_warn("OpenGLRenderDevice::request_pixel_readback: reclaiming unpolled completed request %llu",
                    static_cast<unsigned long long>(oldest_completed->request_id));
        release_pixel_readback_slot(*oldest_completed);
        return oldest_completed;
    }

    OpenGLRenderDevice::PixelReadbackSlot* OpenGLRenderDevice::find_pixel_readback_slot(uint64_t request_id) {
        for (PixelReadbackSlot& slot : pixel_readback_slots_) {
            if (slot.active && slot.request_id == request_id)
                return &slot;
        }
        return nullptr;
    }

    bool OpenGLRenderDevice::pixel_readback_ready(PixelReadbackSlot& slot, bool log_failure) {
        if (!slot.active || !slot.fence)
            return false;
        const GLenum status = glClientWaitSync(slot.fence, 0, 0);
        if (status == GL_ALREADY_SIGNALED || status == GL_CONDITION_SATISFIED) {
            return true;
        }
        if (status == GL_TIMEOUT_EXPIRED)
            return false;
        if (log_failure) {
            tc_log_error("OpenGLRenderDevice::pixel_readback_ready: fence wait failed for request %llu",
                         static_cast<unsigned long long>(slot.request_id));
        }
        release_pixel_readback_slot(slot);
        return false;
    }

    void OpenGLRenderDevice::release_pixel_readback_slot(PixelReadbackSlot& slot) {
        if (slot.fence) {
            glDeleteSync(slot.fence);
            slot.fence = nullptr;
        }
        slot.active = false;
        slot.request_id = 0;
        slot.issue_sequence = 0;
    }

    void OpenGLRenderDevice::destroy_pixel_readback_resources() {
        for (PixelReadbackSlot& slot : pixel_readback_slots_) {
            release_pixel_readback_slot(slot);
            if (slot.pbo) {
                glDeleteBuffers(1, &slot.pbo);
                slot.pbo = 0;
            }
        }
        if (pixel_readback_fbo_) {
            glDeleteFramebuffers(1, &pixel_readback_fbo_);
            pixel_readback_fbo_ = 0;
        }
    }

} // namespace tgfx
