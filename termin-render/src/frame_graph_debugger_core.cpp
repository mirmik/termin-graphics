#include <termin/render/frame_graph_debugger_core.hpp>
#include <termin/render/frame_pass.hpp>
#include <termin/render/tgfx2_bridge.hpp>

#include <tgfx2/render_context.hpp>
#include <tgfx2/descriptors.hpp>
#include <tgfx2/enums.hpp>
#include <tgfx2/i_render_device.hpp>
#include <tgfx2/opengl/opengl_render_device.hpp>

extern "C" {
#include <tcbase/tc_log.h>
}

#include <algorithm>
#include <cmath>

namespace termin {

void FrameGraphCapture::capture(CxxFramePass* caller, FramebufferHandle* src, GraphicsBackend* graphics) {
    if (!should_capture(caller)) {
        return;
    }
    capture_direct(src, graphics);
}

void FrameGraphCapture::capture_direct(FramebufferHandle* src, GraphicsBackend* graphics) {
    if (!src || !graphics) {
        return;
    }
    ensure_capture_fbo(src, graphics);
    if (!capture_fbo_) {
        return;
    }
    do_blit(src, graphics);
    captured_ = true;
}

void FrameGraphCapture::ensure_capture_fbo(FramebufferHandle* src, GraphicsBackend* graphics) {
    int w = src->get_width();
    int h = src->get_height();
    std::string fmt = src->get_format();
    ensure_capture_fbo_raw(graphics, w, h, fmt);
}

void FrameGraphCapture::ensure_capture_fbo_raw(
    GraphicsBackend* graphics, int w, int h, const std::string& fmt
) {
    if (capture_fbo_ && fbo_w_ == w && fbo_h_ == h && fbo_format_ == fmt) {
        return;
    }

    capture_fbo_ = graphics->create_framebuffer(w, h, 1, fmt);
    fbo_w_ = w;
    fbo_h_ = h;
    fbo_format_ = fmt;
}

void FrameGraphCapture::capture_direct_via_ctx2(
    tgfx2::RenderContext2* ctx2,
    tgfx2::TextureHandle src_tex,
    GraphicsBackend* graphics,
    int width,
    int height,
    const std::string& format
) {
    if (!ctx2 || !src_tex || !graphics || width <= 0 || height <= 0) {
        return;
    }

    ensure_capture_fbo_raw(graphics, width, height, format);
    if (!capture_fbo_) {
        return;
    }

    auto* gl_dev = dynamic_cast<tgfx2::OpenGLRenderDevice*>(&ctx2->device());
    if (!gl_dev) {
        return;
    }

    tgfx2::TextureHandle dst = wrap_fbo_color_as_tgfx2(*gl_dev, capture_fbo_.get());
    if (!dst) {
        return;
    }

    ctx2->blit(src_tex, dst);
    gl_dev->destroy(dst);

    captured_ = true;
}

void FrameGraphCapture::do_blit(FramebufferHandle* src, GraphicsBackend* graphics) {
    int w = src->get_width();
    int h = src->get_height();

    if (src->is_msaa()) {
        graphics->blit_framebuffer(src, capture_fbo_.get(), 0, 0, w, h, 0, 0, w, h, true, false);
        graphics->blit_framebuffer(src, capture_fbo_.get(), 0, 0, w, h, 0, 0, w, h, false, true);
    } else {
        graphics->blit_framebuffer(src, capture_fbo_.get(), 0, 0, w, h, 0, 0, w, h, true, true);
    }

    graphics->bind_framebuffer(src);
}

static const char* PRESENTER_FRAG_SRC = R"(
#version 330 core
in vec2 vUV;
uniform sampler2D u_tex;
uniform int u_channel;
uniform int u_highlight_hdr;
out vec4 FragColor;
void main() {
    vec4 c = texture(u_tex, vUV);
    vec3 result;
    if (u_channel == 1)      result = vec3(c.r);
    else if (u_channel == 2) result = vec3(c.g);
    else if (u_channel == 3) result = vec3(c.b);
    else if (u_channel == 4) result = vec3(c.a);
    else                     result = c.rgb;
    if (u_highlight_hdr == 1) {
        float max_val = max(max(c.r, c.g), c.b);
        if (max_val > 1.0) {
            float intensity = clamp((max_val - 1.0) / 2.0, 0.0, 1.0);
            result = mix(result, vec3(1.0, 0.0, 1.0), 0.5 + intensity * 0.5);
        }
    }
    FragColor = vec4(result, 1.0);
}
)";

FrameGraphPresenter::~FrameGraphPresenter() {
    release_tgfx2_resources();
}

void FrameGraphPresenter::release_tgfx2_resources() {
    if (device2_ && fs2_) {
        device2_->destroy(fs2_);
        fs2_ = {};
    }
    device2_ = nullptr;
}

void FrameGraphPresenter::ensure_fs(tgfx2::IRenderDevice& device) {
    if (fs2_ && device2_ == &device) return;
    if (device2_ && device2_ != &device) {
        release_tgfx2_resources();
    }
    device2_ = &device;

    tgfx2::ShaderDesc desc;
    desc.stage = tgfx2::ShaderStage::Fragment;
    desc.source = PRESENTER_FRAG_SRC;
    fs2_ = device.create_shader(desc);
    if (!fs2_) {
        tc::Log::error("FrameGraphPresenter: failed to create fs2");
    }
}

void FrameGraphPresenter::render(
    tgfx2::RenderContext2* ctx2,
    FramebufferHandle* capture_fbo,
    FramebufferHandle* target_fbo,
    int dst_x,
    int dst_y,
    int dst_w,
    int dst_h,
    int channel_mode,
    bool highlight_hdr
) {
    if (!ctx2 || !capture_fbo || !target_fbo) {
        return;
    }

    auto* gl_dev = dynamic_cast<tgfx2::OpenGLRenderDevice*>(&ctx2->device());
    if (!gl_dev) {
        tc::Log::error("FrameGraphPresenter/tgfx2: device is not OpenGLRenderDevice");
        return;
    }

    ensure_fs(ctx2->device());
    if (!fs2_) {
        return;
    }

    tgfx2::TextureHandle source_tex = wrap_fbo_color_as_tgfx2(*gl_dev, capture_fbo);
    if (!source_tex) {
        return;
    }
    tgfx2::TextureHandle target_tex = wrap_fbo_color_as_tgfx2(*gl_dev, target_fbo);
    if (!target_tex) {
        gl_dev->destroy(source_tex);
        return;
    }

    ctx2->begin_pass(target_tex, tgfx2::TextureHandle{}, nullptr, 1.0f, false);
    ctx2->set_viewport(dst_x, dst_y, dst_w, dst_h);
    // tcgui clip rect may be active when the debugger widget calls into
    // us; the FSQ draw must cover the full sub-region, not whatever
    // parent clip was last set.
    ctx2->clear_scissor();
    ctx2->set_depth_test(false);
    ctx2->set_depth_write(false);
    ctx2->set_blend(false);
    ctx2->set_cull(tgfx2::CullMode::None);
    ctx2->set_color_format(tgfx2::PixelFormat::RGBA8_UNorm);

    ctx2->bind_shader(ctx2->fsq_vertex_shader(), fs2_);

    ctx2->bind_sampled_texture(0, source_tex);
    ctx2->set_uniform_int("u_tex", 0);
    ctx2->set_uniform_int("u_channel", channel_mode);
    ctx2->set_uniform_int("u_highlight_hdr", highlight_hdr ? 1 : 0);

    ctx2->draw_fullscreen_quad();
    ctx2->end_pass();

    gl_dev->destroy(source_tex);
    gl_dev->destroy(target_tex);
}

HDRStats FrameGraphPresenter::compute_hdr_stats(GraphicsBackend* graphics, FramebufferHandle* fbo) {
    HDRStats stats{};

    if (!graphics || !fbo) {
        return stats;
    }

    int w = fbo->get_width();
    int h = fbo->get_height();
    int total = w * h;
    if (total <= 0) {
        return stats;
    }

    std::vector<float> pixels(total * 4);
    if (!graphics->read_color_buffer_float(fbo, pixels.data())) {
        tc::Log::error("FrameGraphPresenter: read_color_buffer_float failed");
        return stats;
    }

    stats.total_pixels = total;

    float r0 = pixels[0];
    float g0 = pixels[1];
    float b0 = pixels[2];
    stats.min_r = r0;
    stats.max_r = r0;
    stats.min_g = g0;
    stats.max_g = g0;
    stats.min_b = b0;
    stats.max_b = b0;

    double sum_r = 0;
    double sum_g = 0;
    double sum_b = 0;
    int hdr_count = 0;
    float max_val = 0;

    for (int i = 0; i < total; ++i) {
        float r = pixels[i * 4 + 0];
        float g = pixels[i * 4 + 1];
        float b = pixels[i * 4 + 2];

        if (r < stats.min_r) stats.min_r = r;
        if (r > stats.max_r) stats.max_r = r;
        if (g < stats.min_g) stats.min_g = g;
        if (g > stats.max_g) stats.max_g = g;
        if (b < stats.min_b) stats.min_b = b;
        if (b > stats.max_b) stats.max_b = b;

        sum_r += r;
        sum_g += g;
        sum_b += b;

        float mx = std::max({r, g, b});
        if (mx > max_val) max_val = mx;
        if (mx > 1.0f) ++hdr_count;
    }

    stats.avg_r = static_cast<float>(sum_r / total);
    stats.avg_g = static_cast<float>(sum_g / total);
    stats.avg_b = static_cast<float>(sum_b / total);
    stats.hdr_pixel_count = hdr_count;
    stats.hdr_percent = total > 0 ? (static_cast<float>(hdr_count) / total * 100.0f) : 0.0f;
    stats.max_value = max_val;

    return stats;
}

std::vector<uint8_t> FrameGraphPresenter::read_depth_normalized(
    GraphicsBackend* graphics,
    FramebufferHandle* fbo,
    int* out_w,
    int* out_h
) {
    if (out_w) *out_w = 0;
    if (out_h) *out_h = 0;

    if (!graphics || !fbo) {
        return {};
    }

    int w = fbo->get_width();
    int h = fbo->get_height();
    if (out_w) *out_w = w;
    if (out_h) *out_h = h;
    if (w <= 0 || h <= 0) {
        return {};
    }

    std::vector<float> depth(w * h);
    if (!graphics->read_depth_buffer(fbo, depth.data())) {
        tc::Log::error("FrameGraphPresenter: read_depth_buffer failed");
        return {};
    }

    float min_d = depth[0];
    float max_d = depth[0];
    for (float d : depth) {
        if (d < min_d) min_d = d;
        if (d > max_d) max_d = d;
    }

    float range = max_d - min_d;
    if (range < 1e-8f) range = 1.0f;

    std::vector<uint8_t> out(w * h);
    for (int i = 0; i < w * h; ++i) {
        float v = (depth[i] - min_d) / range;
        v = std::clamp(v, 0.0f, 1.0f);
        out[i] = static_cast<uint8_t>(std::round(v * 255.0f));
    }

    return out;
}

FBOInfo FrameGraphPresenter::get_fbo_info(FramebufferHandle* fbo) {
    FBOInfo info;
    if (!fbo) {
        return info;
    }

    info.type_name = fbo->resource_type();
    info.width = fbo->get_width();
    info.height = fbo->get_height();
    info.samples = fbo->get_samples();
    info.is_msaa = fbo->is_msaa();
    info.format = fbo->get_format();
    info.fbo_id = fbo->get_fbo_id();
    info.gl_width = info.width;
    info.gl_height = info.height;
    info.gl_samples = info.samples;
    info.gl_format = info.format;
    info.filter = fbo->is_msaa() ? "n/a" : "linear";
    info.gl_filter = info.filter;
    return info;
}

} // namespace termin
