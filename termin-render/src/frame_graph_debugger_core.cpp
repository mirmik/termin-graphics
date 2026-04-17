#include <termin/render/frame_graph_debugger_core.hpp>
#include <termin/render/frame_pass.hpp>

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

namespace {

bool is_depth_format(tgfx::PixelFormat fmt) {
    return fmt == tgfx::PixelFormat::D24_UNorm
        || fmt == tgfx::PixelFormat::D24_UNorm_S8_UInt
        || fmt == tgfx::PixelFormat::D32F;
}

std::string pixel_format_name(tgfx::PixelFormat fmt) {
    switch (fmt) {
        case tgfx::PixelFormat::R8_UNorm:           return "r8";
        case tgfx::PixelFormat::RG8_UNorm:          return "rg8";
        case tgfx::PixelFormat::RGB8_UNorm:         return "rgb8";
        case tgfx::PixelFormat::RGBA8_UNorm:        return "rgba8";
        case tgfx::PixelFormat::BGRA8_UNorm:        return "bgra8";
        case tgfx::PixelFormat::R16F:               return "r16f";
        case tgfx::PixelFormat::RG16F:              return "rg16f";
        case tgfx::PixelFormat::RGBA16F:            return "rgba16f";
        case tgfx::PixelFormat::R32F:               return "r32f";
        case tgfx::PixelFormat::RG32F:              return "rg32f";
        case tgfx::PixelFormat::RGBA32F:            return "rgba32f";
        case tgfx::PixelFormat::D24_UNorm:          return "depth24";
        case tgfx::PixelFormat::D24_UNorm_S8_UInt:  return "depth24_stencil8";
        case tgfx::PixelFormat::D32F:               return "depth32f";
    }
    return "unknown";
}

} // namespace

FrameGraphCapture::~FrameGraphCapture() {
    release();
}

void FrameGraphCapture::release() {
    if (device_ && capture_tex_) {
        device_->destroy(capture_tex_);
    }
    capture_tex_ = {};
    device_ = nullptr;
    width_ = 0;
    height_ = 0;
    captured_ = false;
}

void FrameGraphCapture::ensure_capture_tex(
    tgfx::IRenderDevice& device, int w, int h, tgfx::PixelFormat fmt
) {
    if (device_ == &device && capture_tex_ &&
        width_ == w && height_ == h && format_ == fmt) {
        return;
    }
    // Size, format, or device changed — drop the old texture and
    // allocate afresh.
    if (device_ && capture_tex_) {
        device_->destroy(capture_tex_);
        capture_tex_ = {};
    }
    device_ = &device;
    width_ = w;
    height_ = h;
    format_ = fmt;

    tgfx::TextureDesc desc;
    desc.width = static_cast<uint32_t>(w);
    desc.height = static_cast<uint32_t>(h);
    desc.format = fmt;
    desc.sample_count = 1;
    desc.usage = tgfx::TextureUsage::Sampled |
                 tgfx::TextureUsage::ColorAttachment |
                 tgfx::TextureUsage::CopyDst;
    capture_tex_ = device.create_texture(desc);
}

void FrameGraphCapture::capture_direct_via_ctx2(
    tgfx::RenderContext2* ctx2,
    tgfx::TextureHandle src_tex,
    int width,
    int height,
    tgfx::PixelFormat format
) {
    if (!ctx2 || !src_tex || width <= 0 || height <= 0) {
        return;
    }

    ensure_capture_tex(ctx2->device(), width, height, format);
    if (!capture_tex_) {
        return;
    }

    ctx2->blit(src_tex, capture_tex_);
    captured_ = true;
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

void FrameGraphPresenter::ensure_fs(tgfx::IRenderDevice& device) {
    if (fs2_ && device2_ == &device) return;
    if (device2_ && device2_ != &device) {
        release_tgfx2_resources();
    }
    device2_ = &device;

    tgfx::ShaderDesc desc;
    desc.stage = tgfx::ShaderStage::Fragment;
    desc.source = PRESENTER_FRAG_SRC;
    fs2_ = device.create_shader(desc);
    if (!fs2_) {
        tc::Log::error("FrameGraphPresenter: failed to create fs2");
    }
}

void FrameGraphPresenter::render(
    tgfx::RenderContext2* ctx2,
    tgfx::TextureHandle capture_tex,
    tgfx::TextureHandle target_tex,
    int dst_x,
    int dst_y,
    int dst_w,
    int dst_h,
    int channel_mode,
    bool highlight_hdr
) {
    if (!ctx2 || !capture_tex || !target_tex) {
        return;
    }

    ensure_fs(ctx2->device());
    if (!fs2_) {
        return;
    }

    ctx2->begin_pass(target_tex, tgfx::TextureHandle{}, nullptr, 1.0f, false);
    ctx2->set_viewport(dst_x, dst_y, dst_w, dst_h);
    // The debugger widget may be called with an active tcgui clip
    // rect. Make sure our fullscreen-quad draw isn't trimmed.
    ctx2->clear_scissor();
    ctx2->set_depth_test(false);
    ctx2->set_depth_write(false);
    ctx2->set_blend(false);
    ctx2->set_cull(tgfx::CullMode::None);
    ctx2->set_color_format(tgfx::PixelFormat::RGBA8_UNorm);

    ctx2->bind_shader(ctx2->fsq_vertex_shader(), fs2_);

    ctx2->bind_sampled_texture(0, capture_tex);
    ctx2->set_uniform_int("u_tex", 0);
    ctx2->set_uniform_int("u_channel", channel_mode);
    ctx2->set_uniform_int("u_highlight_hdr", highlight_hdr ? 1 : 0);

    ctx2->draw_fullscreen_quad();
    ctx2->end_pass();
}

HDRStats FrameGraphPresenter::compute_hdr_stats(
    tgfx::IRenderDevice* device,
    tgfx::TextureHandle tex
) {
    HDRStats stats{};

    auto* gl_dev = dynamic_cast<tgfx::OpenGLRenderDevice*>(device);
    if (!gl_dev || !tex) {
        return stats;
    }
    auto desc = gl_dev->texture_desc(tex);
    int w = static_cast<int>(desc.width);
    int h = static_cast<int>(desc.height);
    int total = w * h;
    if (total <= 0) {
        return stats;
    }

    std::vector<float> pixels(total * 4);
    if (!gl_dev->read_texture_rgba_float(tex, pixels.data())) {
        tc::Log::error("FrameGraphPresenter: read_texture_rgba_float failed");
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
    tgfx::IRenderDevice* device,
    tgfx::TextureHandle tex,
    int* out_w,
    int* out_h
) {
    if (out_w) *out_w = 0;
    if (out_h) *out_h = 0;

    auto* gl_dev = dynamic_cast<tgfx::OpenGLRenderDevice*>(device);
    if (!gl_dev || !tex) {
        return {};
    }

    auto desc = gl_dev->texture_desc(tex);
    int w = static_cast<int>(desc.width);
    int h = static_cast<int>(desc.height);
    if (out_w) *out_w = w;
    if (out_h) *out_h = h;
    if (w <= 0 || h <= 0) {
        return {};
    }

    std::vector<float> depth(w * h);
    if (!gl_dev->read_texture_depth_float(tex, depth.data())) {
        tc::Log::error("FrameGraphPresenter: read_texture_depth_float failed");
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

TextureInfo FrameGraphPresenter::get_texture_info(
    tgfx::IRenderDevice* device,
    tgfx::TextureHandle tex
) {
    TextureInfo info;
    if (!device || !tex) {
        return info;
    }
    auto desc = device->texture_desc(tex);
    info.width = static_cast<int>(desc.width);
    info.height = static_cast<int>(desc.height);
    info.samples = static_cast<int>(desc.sample_count);
    info.is_msaa = desc.sample_count > 1;
    info.format = desc.format;
    info.format_name = pixel_format_name(desc.format);
    return info;
}

} // namespace termin
