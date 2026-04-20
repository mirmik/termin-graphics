#pragma once

#include <cmath>
#include <string>
#include <vector>

#include <tcbase/tc_log.hpp>
#include "tgfx2/enums.hpp"
#include "tgfx2/handles.hpp"
#include <termin/render/render_export.hpp>
extern "C" {
#include <tgfx/resources/tc_shader_registry.h>
}

namespace tgfx {
class RenderContext2;
class IRenderDevice;
}

namespace termin {

class CxxFramePass;

struct HDRStats {
    float min_r = 0;
    float max_r = 0;
    float avg_r = 0;
    float min_g = 0;
    float max_g = 0;
    float avg_g = 0;
    float min_b = 0;
    float max_b = 0;
    float avg_b = 0;
    int hdr_pixel_count = 0;
    int total_pixels = 0;
    float hdr_percent = 0;
    float max_value = 0;
};

// Lightweight descriptor shown in the debugger UI for a captured
// tgfx2 texture. Fields match what the old FBOInfo exposed minus
// anything that was FBO-specific (gl fbo id, filter state).
struct TextureInfo {
    int width = 0;
    int height = 0;
    int samples = 0;
    bool is_msaa = false;
    tgfx::PixelFormat format = tgfx::PixelFormat::RGBA8_UNorm;
    std::string format_name;
};

// Owned tgfx2 color texture the debugger captures each frame; the
// presenter samples it for its channel/HDR-highlight overlay and for
// read_pixels-based stats. Created lazily through the same device
// the host RenderContext2 draws with.
class RENDER_API FrameGraphCapture {
private:
    tgfx::IRenderDevice* device_ = nullptr;
    tgfx::TextureHandle capture_tex_;
    int width_ = 0;
    int height_ = 0;
    tgfx::PixelFormat format_ = tgfx::PixelFormat::RGBA8_UNorm;
    bool captured_ = false;
    CxxFramePass* target_pass_ = nullptr;

public:
    ~FrameGraphCapture();

    void set_target(CxxFramePass* pass) { target_pass_ = pass; }
    void clear_target() { target_pass_ = nullptr; }
    CxxFramePass* target() const { return target_pass_; }

    bool should_capture(CxxFramePass* caller) const {
        return caller && caller == target_pass_;
    }

    // Capture `src_tex` into an internal owned tgfx2 texture sized
    // `width x height`. Reallocates on size / format mismatch, re-uses
    // the texture otherwise. `ctx2->blit` performs the copy.
    void capture_direct_via_ctx2(
        tgfx::RenderContext2* ctx2,
        tgfx::TextureHandle src_tex,
        int width,
        int height,
        tgfx::PixelFormat format = tgfx::PixelFormat::RGBA8_UNorm
    );

    tgfx::TextureHandle capture_tex() const { return capture_tex_; }
    int width() const { return width_; }
    int height() const { return height_; }
    tgfx::PixelFormat format() const { return format_; }
    bool has_capture() const { return captured_; }
    void reset_capture() { captured_ = false; }

private:
    void release();
    void ensure_capture_tex(
        tgfx::IRenderDevice& device,
        int w, int h, tgfx::PixelFormat fmt
    );
};

// Draws a captured tgfx2 texture into a target texture with a
// channel-picker / HDR-highlight fragment shader. Target is a
// tgfx::TextureHandle — either a native pool entry or an external
// wrap of the debug window's default framebuffer.
class RENDER_API FrameGraphPresenter {
private:
    tgfx::IRenderDevice* device2_ = nullptr;
    // FS-only shader via tc_shader registry (VS comes from ctx2->fsq_vertex_shader()).
    // Hash-based dedup keeps the VkShaderModule alive across presenter
    // re-creations on Play/Stop — see GrayscalePass for the same pattern.
    tc_shader_handle shader_handle_ = tc_shader_handle_invalid();

public:
    ~FrameGraphPresenter();

    void render(
        tgfx::RenderContext2* ctx2,
        tgfx::TextureHandle capture_tex,
        tgfx::TextureHandle target_tex,
        int dst_x,
        int dst_y,
        int dst_w,
        int dst_h,
        int channel_mode,
        bool highlight_hdr
    );

    // HDR / depth readback helpers take a native tgfx2 texture and
    // pull pixels through the device's read_texture_* primitives.
    HDRStats compute_hdr_stats(
        tgfx::IRenderDevice* device,
        tgfx::TextureHandle tex
    );

    std::vector<uint8_t> read_depth_normalized(
        tgfx::IRenderDevice* device,
        tgfx::TextureHandle tex,
        int* out_w,
        int* out_h
    );

    static TextureInfo get_texture_info(
        tgfx::IRenderDevice* device,
        tgfx::TextureHandle tex
    );

private:
    void ensure_fs(tgfx::IRenderDevice& device);
    void release_tgfx2_resources();
};

class RENDER_API FrameGraphDebuggerCore {
public:
    FrameGraphCapture capture;
    FrameGraphPresenter presenter;

    tgfx::TextureHandle capture_tex() const { return capture.capture_tex(); }
};

} // namespace termin
