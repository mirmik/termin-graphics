#pragma once

#include <cmath>
#include <string>
#include <vector>

#include "tgfx/graphics_backend.hpp"
#include "tgfx/handles.hpp"
#include <tcbase/tc_log.hpp>
#include <tgfx/tgfx_shader_handle.hpp>

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

struct FBOInfo {
    std::string type_name;
    int width = 0;
    int height = 0;
    int samples = 0;
    bool is_msaa = false;
    std::string format;
    uint32_t fbo_id = 0;
    std::string gl_format;
    int gl_width = 0;
    int gl_height = 0;
    int gl_samples = 0;
    std::string filter;
    std::string gl_filter;
};

class FrameGraphCapture {
private:
    FramebufferHandlePtr capture_fbo_;
    int fbo_w_ = 0;
    int fbo_h_ = 0;
    std::string fbo_format_;
    bool captured_ = false;
    CxxFramePass* target_pass_ = nullptr;

public:
    void set_target(CxxFramePass* pass) { target_pass_ = pass; }
    void clear_target() { target_pass_ = nullptr; }
    CxxFramePass* target() const { return target_pass_; }

    bool should_capture(CxxFramePass* caller) const {
        return caller && caller == target_pass_;
    }

    void capture(CxxFramePass* caller, FramebufferHandle* src, GraphicsBackend* graphics);
    void capture_direct(FramebufferHandle* src, GraphicsBackend* graphics);

    FramebufferHandle* capture_fbo() const { return capture_fbo_.get(); }
    bool has_capture() const { return captured_; }
    void reset_capture() { captured_ = false; }

private:
    void ensure_capture_fbo(FramebufferHandle* src, GraphicsBackend* graphics);
    void do_blit(FramebufferHandle* src, GraphicsBackend* graphics);
};

class FrameGraphPresenter {
private:
    TcShader shader_;
    bool shader_ready_ = false;

public:
    void render(
        GraphicsBackend* graphics,
        FramebufferHandle* capture_fbo,
        int dst_w,
        int dst_h,
        int channel_mode,
        bool highlight_hdr
    );

    HDRStats compute_hdr_stats(GraphicsBackend* graphics, FramebufferHandle* fbo);

    std::vector<uint8_t> read_depth_normalized(
        GraphicsBackend* graphics,
        FramebufferHandle* fbo,
        int* out_w,
        int* out_h
    );

    static FBOInfo get_fbo_info(FramebufferHandle* fbo);

private:
    void ensure_shader();
};

class FrameGraphDebuggerCore {
public:
    FrameGraphCapture capture;
    FrameGraphPresenter presenter;

    FramebufferHandle* capture_fbo() const { return capture.capture_fbo(); }
};

} // namespace termin
