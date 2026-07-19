#include <termin/render/frame_graph_debugger_core.hpp>
#include <termin/render/frame_pass.hpp>

#include <tgfx2/render_context.hpp>
#include <tgfx2/descriptors.hpp>
#include <tgfx2/enums.hpp>
#include <tgfx2/i_render_device.hpp>
#include <tgfx2/pixel_format_utils.hpp>
#include <tgfx2/tc_shader_bridge.hpp>

extern "C" {
#include <tgfx/resources/tc_shader.h>
}

extern "C" {
#include <tcbase/tc_log.h>
}

#include <algorithm>
#include <cmath>
#include <cstring>

namespace termin {

namespace {

struct PresenterPushData {
    int channel_mode;
    int highlight_hdr;
    int _pad0;
    int _pad1;
};
static_assert(sizeof(PresenterPushData) == 16,
              "PresenterPushData must match the shader push block");

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
    // Color vs depth-stencil attachment is mutually exclusive on Vulkan;
    // picking the right bit lets blit targets for shadow maps succeed.
    // CopySrc/CopyDst stay on both paths — Frame Debugger eventually
    // reads the capture out and/or re-blits it.
    if (tgfx::is_depth_format(fmt)) {
        desc.usage = tgfx::TextureUsage::Sampled |
                     tgfx::TextureUsage::DepthStencilAttachment |
                     tgfx::TextureUsage::CopySrc |
                     tgfx::TextureUsage::CopyDst;
    } else {
        desc.usage = tgfx::TextureUsage::Sampled |
                     tgfx::TextureUsage::ColorAttachment |
                     tgfx::TextureUsage::CopySrc |
                     tgfx::TextureUsage::CopyDst;
    }
    capture_tex_ = device.create_texture(desc);
}

bool FrameGraphCapture::is_depth() const {
    return tgfx::is_depth_format(format_);
}

void FrameGraphCapture::capture_direct_via_ctx2(
    tgfx::RenderContext2* ctx2,
    tgfx::TextureHandle src_tex,
    int width,
    int height,
    tgfx::PixelFormat format
) {
    if (!ctx2 || !src_tex) {
        return;
    }

    // The caller-supplied `format` is only a legacy hint. Capture must
    // use the real source format: Vulkan cannot resolve MSAA while also
    // converting formats, and depth resources cannot be copied into a
    // color capture texture.
    auto src_desc = ctx2->device().texture_desc(src_tex);
    tgfx::PixelFormat effective = src_desc.format;
    (void)format;
    if (ctx2->device().backend_type() == tgfx::BackendType::D3D11 &&
        tgfx::is_depth_format(effective) &&
        src_desc.sample_count > 1) {
        static bool logged_d3d11_msaa_depth_skip = false;
        if (!logged_d3d11_msaa_depth_skip) {
            tc_log(TC_LOG_WARN,
                "[FrameGraphCapture] D3D11 MSAA depth capture is not supported; "
                "skipping depth sidecar for texture %u (%ux%u samples=%u)",
                src_tex.id,
                src_desc.width,
                src_desc.height,
                src_desc.sample_count);
            logged_d3d11_msaa_depth_skip = true;
        }
        reset_capture();
        return;
    }
    if (width <= 0 || height <= 0) {
        width = static_cast<int>(src_desc.width);
        height = static_cast<int>(src_desc.height);
    }
    if (width <= 0 || height <= 0) {
        tc_log(TC_LOG_ERROR,
            "[FrameGraphCapture] invalid capture size for texture %u: requested=%dx%d source=%ux%u",
            src_tex.id,
            width,
            height,
            src_desc.width,
            src_desc.height);
        return;
    }

    ensure_capture_tex(ctx2->device(), width, height, effective);
    if (!capture_tex_) {
        return;
    }

    ctx2->blit(src_tex, capture_tex_);
    captured_ = true;
}

static const char* PRESENTER_FRAG_SRC = R"(
#version 450 core
layout(location = 0) in vec2 v_uv;
layout(binding = 0) uniform sampler2D u_tex;
struct PresenterPushData {
    int channel_mode;
    int highlight_hdr;
    int _pad0;
    int _pad1;
};
#ifdef VULKAN
layout(push_constant) uniform PresenterPushBlock { PresenterPushData pc; };
#else
layout(std140, binding = 14) uniform PresenterPushBlock { PresenterPushData pc; };
#endif
layout(location = 0) out vec4 FragColor;
void main() {
    vec4 c = texture(u_tex, v_uv);
    vec3 result;
    if (pc.channel_mode == 5)      result = vec3(pow(clamp(1.0 - c.r, 0.0, 1.0), 0.25));
    else if (pc.channel_mode == 1) result = vec3(c.r);
    else if (pc.channel_mode == 2) result = vec3(c.g);
    else if (pc.channel_mode == 3) result = vec3(c.b);
    else if (pc.channel_mode == 4) result = vec3(c.a);
    else                           result = c.rgb;
    if (pc.highlight_hdr == 1) {
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
    // FS handle lives on the tc_shader registry; not owned here.
    device2_ = nullptr;
}

void FrameGraphPresenter::ensure_fs(tgfx::IRenderDevice& device) {
    device2_ = &device;
    if (tc_shader_handle_is_invalid(shader_handle_)) {
        shader_handle_ = tc_shader_register_static_uuid_ex(
            nullptr,
            PRESENTER_FRAG_SRC,
            nullptr,
            "FrameGraphPresenterFS",
            "termin-frame-graph-presenter",
            TC_SHADER_LANGUAGE_GLSL,
            TC_SHADER_ARTIFACT_OPTIONAL);
        tc_shader* shader = tc_shader_get(shader_handle_);
        if (shader) {
            tc_shader_resource_binding u_tex{};
            std::strncpy(u_tex.name, "u_tex", TC_SHADER_RESOURCE_NAME_MAX - 1);
            u_tex.name[TC_SHADER_RESOURCE_NAME_MAX - 1] = '\0';
            u_tex.kind = TC_SHADER_RESOURCE_TEXTURE;
            u_tex.scope = TC_SHADER_RESOURCE_SCOPE_TRANSIENT;
            u_tex.set = TC_SHADER_RESOURCE_SET_DEFAULT;
            u_tex.binding = 0;
            u_tex.stage_mask = TC_SHADER_STAGE_FRAGMENT;
            tc_shader_set_resource_layout(shader, &u_tex, 1);
        }
    }
}

void FrameGraphPresenter::render_in_current_pass(
    tgfx::RenderContext2* ctx2,
    const FrameGraphPresenterDraw& draw
) {
    const Rect2i& dst = draw.dst_rect;
    if (!ctx2 || !draw.capture_tex || dst.width <= 0 || dst.height <= 0) {
        return;
    }

    ensure_fs(ctx2->device());
    tgfx::ShaderHandle fs;
    {
        tc_shader* raw = tc_shader_get(shader_handle_);
        if (!raw || !tc_shader_ensure_tgfx2(raw, device2_, nullptr, &fs)) {
            return;
        }
    }

    ctx2->set_viewport(dst.x, dst.y, dst.width, dst.height);
    // The debugger widget may be called with an active tcgui clip
    // rect. Make sure our fullscreen-quad draw is controlled by the
    // preview viewport instead of an inherited text/widget scissor.
    ctx2->clear_scissor();
    ctx2->set_depth_test(false);
    ctx2->set_depth_write(false);
    ctx2->set_blend(false);
    ctx2->set_cull(tgfx::CullMode::None);

    ctx2->bind_shader(ctx2->fsq_vertex_shader(), fs);
    ctx2->use_shader_resource_layout(tc_shader_get(shader_handle_));

    ctx2->bind_texture("u_tex", draw.capture_tex);
    PresenterPushData push{};
    push.channel_mode = draw.options.channel_mode;
    push.highlight_hdr = draw.options.highlight_hdr ? 1 : 0;
    ctx2->set_push_constants(&push, sizeof(push));

    ctx2->draw_fullscreen_quad();
}

void FrameGraphPresenter::render(
    tgfx::RenderContext2* ctx2,
    tgfx::TextureHandle target_tex,
    const FrameGraphPresenterDraw& draw
) {
    if (!ctx2 || !draw.capture_tex || !target_tex) {
        return;
    }

    ctx2->begin_pass(target_tex, tgfx::TextureHandle{}, nullptr, 1.0f, false);
    render_in_current_pass(ctx2, draw);
    ctx2->end_pass();
}

HDRStats FrameGraphPresenter::compute_hdr_stats(
    tgfx::IRenderDevice* device,
    tgfx::TextureHandle tex
) {
    HDRStats stats{};

    if (!device || !tex) {
        return stats;
    }
    auto desc = device->texture_desc(tex);
    int w = static_cast<int>(desc.width);
    int h = static_cast<int>(desc.height);
    int total = w * h;
    if (total <= 0) {
        return stats;
    }

    std::vector<float> pixels(total * 4);
    if (!device->read_texture_rgba_float(tex, pixels.data())) {
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

    if (!device || !tex) {
        return {};
    }

    auto desc = device->texture_desc(tex);
    int w = static_cast<int>(desc.width);
    int h = static_cast<int>(desc.height);
    if (out_w) *out_w = w;
    if (out_h) *out_h = h;
    if (w <= 0 || h <= 0) {
        return {};
    }

    std::vector<float> depth(w * h);
    if (!device->read_texture_depth_float(tex, depth.data())) {
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
    info.format_name = std::string(tgfx::pixel_format_name(desc.format));
    return info;
}

} // namespace termin
