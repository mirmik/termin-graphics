#pragma once

#include <cmath>
#include <string>
#include <vector>

#include <tcbase/tc_log.hpp>
#include "tgfx2/enums.hpp"
#include "tgfx2/handles.hpp"
#include <termin/render/render_export.hpp>

namespace tgfx2 {
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
    tgfx2::PixelFormat format = tgfx2::PixelFormat::RGBA8_UNorm;
    std::string format_name;
};

// Owned tgfx2 color texture the debugger captures each frame; the
// presenter samples it for its channel/HDR-highlight overlay and for
// read_pixels-based stats. Created lazily through the same device
// the host RenderContext2 draws with.
class RENDER_API FrameGraphCapture {
private:
    tgfx2::IRenderDevice* device_ = nullptr;
    tgfx2::TextureHandle capture_tex_;
    int width_ = 0;
    int height_ = 0;
    tgfx2::PixelFormat format_ = tgfx2::PixelFormat::RGBA8_UNorm;
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
        tgfx2::RenderContext2* ctx2,
        tgfx2::TextureHandle src_tex,
        int width,
        int height,
        tgfx2::PixelFormat format = tgfx2::PixelFormat::RGBA8_UNorm
    );

    tgfx2::TextureHandle capture_tex() const { return capture_tex_; }
    int width() const { return width_; }
    int height() const { return height_; }
    tgfx2::PixelFormat format() const { return format_; }
    bool has_capture() const { return captured_; }
    void reset_capture() { captured_ = false; }

private:
    void release();
    void ensure_capture_tex(
        tgfx2::IRenderDevice& device,
        int w, int h, tgfx2::PixelFormat fmt
    );
};

// Draws a captured tgfx2 texture into a target texture with a
// channel-picker / HDR-highlight fragment shader. Target is a
// tgfx2::TextureHandle — either a native pool entry or an external
// wrap of the debug window's default framebuffer.
class RENDER_API FrameGraphPresenter {
private:
    tgfx2::IRenderDevice* device2_ = nullptr;
    tgfx2::ShaderHandle fs2_;

public:
    ~FrameGraphPresenter();

    void render(
        tgfx2::RenderContext2* ctx2,
        tgfx2::TextureHandle capture_tex,
        tgfx2::TextureHandle target_tex,
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
        tgfx2::IRenderDevice* device,
        tgfx2::TextureHandle tex
    );

    std::vector<uint8_t> read_depth_normalized(
        tgfx2::IRenderDevice* device,
        tgfx2::TextureHandle tex,
        int* out_w,
        int* out_h
    );

    static TextureInfo get_texture_info(
        tgfx2::IRenderDevice* device,
        tgfx2::TextureHandle tex
    );

private:
    void ensure_fs(tgfx2::IRenderDevice& device);
    void release_tgfx2_resources();
};

class RENDER_API FrameGraphDebuggerCore {
public:
    FrameGraphCapture capture;
    FrameGraphPresenter presenter;

    tgfx2::TextureHandle capture_tex() const { return capture.capture_tex(); }
};

} // namespace termin
