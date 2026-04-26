// text2d_renderer.cpp - Pixel-space text rendering on tgfx2.
//
// Port of termin-graphics/python/tgfx/text2d.py.
//
// Vertex layout matches RenderContext2::draw_immediate_triangles
// (7 floats per vertex: vec3 pos + vec4 misc). The fragment shader
// re-interprets the misc block as (u, v, _, _).

#include "tgfx2/text2d_renderer.hpp"

#include <cmath>
#include <cstring>
#include <vector>

#include "tgfx2/descriptors.hpp"
#include "tgfx2/enums.hpp"
#include "tgfx2/font_atlas.hpp"
#include "tgfx2/i_render_device.hpp"
#include "tgfx2/render_context.hpp"
#include "tgfx2/tc_shader_bridge.hpp"

extern "C" {
#include "tc_profiler.h"
}

extern "C" {
#include <tgfx/resources/tc_shader.h>
}

#include "internal/utf8_decode.hpp"

extern "C" {
#include "tgfx/tgfx2_interop.h"
}

namespace tgfx {

namespace {

// Single source that compiles on both backends. The `#ifdef VULKAN`
// is tripped by the `-DVULKAN=1` macro definition the Vulkan shader
// compiler adds (see vulkan_shader_compiler.cpp). Under GL we take
// the std140 UBO branch bound at the same slot tgfx2's push-constant
// ring buffer uses (TGFX2_PUSH_CONSTANTS_BINDING = 14). Data layout
// matches `Text2DPush` in this TU.
constexpr const char* kText2DCommon = R"(
struct Text2DPushData {
    mat4 u_projection;
    vec4 u_color;
};
#ifdef VULKAN
layout(push_constant) uniform Text2DPushBlock { Text2DPushData pc; };
#else
layout(std140, binding = 14) uniform Text2DPushBlock { Text2DPushData pc; };
#endif
)";

static std::string make_text2d_vert() {
    return std::string("#version 450 core\n") + kText2DCommon + R"(
layout(location=0) in vec3 a_pos;
layout(location=1) in vec4 a_uv_pad;

layout(location=0) out vec2 v_uv;

void main() {
    gl_Position = pc.u_projection * vec4(a_pos.xy, 0.0, 1.0);
    v_uv = a_uv_pad.xy;
}
)";
}

static std::string make_text2d_sdf_vert() {
    return std::string("#version 450 core\n") + R"(
struct Text2DSdfPushData {
    mat4 u_projection;
    vec4 u_color;
    float u_smoothing;
};
#ifdef VULKAN
layout(push_constant) uniform Text2DSdfPushBlock { Text2DSdfPushData pc; };
#else
layout(std140, binding = 14) uniform Text2DSdfPushBlock { Text2DSdfPushData pc; };
#endif

layout(location=0) in vec3 a_pos;
layout(location=1) in vec4 a_uv_pad;

layout(location=0) out vec2 v_uv;

void main() {
    gl_Position = pc.u_projection * vec4(a_pos.xy, 0.0, 1.0);
    v_uv = a_uv_pad.xy;
}
)";
}

static std::string make_text2d_frag() {
    return std::string("#version 450 core\n") + kText2DCommon + R"(
layout(binding = 4) uniform sampler2D u_font_atlas;

layout(location=0) in vec2 v_uv;
layout(location=0) out vec4 frag_color;

void main() {
    float a = texture(u_font_atlas, v_uv).r * pc.u_color.a;
    // Threshold at one 8-bit alpha level. Anything we'd discard above
    // this can't contribute to the blended output anyway; anything
    // below this (but > 0) is AA tail we want to keep — the old 0.01
    // cutoff was high enough to nibble visible edge gradients.
    if (a < (1.0/255.0)) discard;
    frag_color = vec4(pc.u_color.rgb, a);
}
)";
}

// SDF shader — same vertex shader, different fragment shader.
// Push constant layout is extended with a smoothing parameter.
static std::string make_text2d_sdf_frag() {
    return std::string("#version 450 core\n") + R"(
struct Text2DSdfPushData {
    mat4 u_projection;
    vec4 u_color;
    float u_smoothing;
};
#ifdef VULKAN
layout(push_constant) uniform Text2DSdfPushBlock { Text2DSdfPushData pc; };
#else
layout(std140, binding = 14) uniform Text2DSdfPushBlock { Text2DSdfPushData pc; };
#endif

layout(binding = 4) uniform sampler2D u_font_atlas;

layout(location=0) in vec2 v_uv;
layout(location=0) out vec4 frag_color;

void main() {
    float d = texture(u_font_atlas, v_uv).r;
    float a = smoothstep(0.5 - pc.u_smoothing, 0.5 + pc.u_smoothing, d)
            * pc.u_color.a;
    if (a < (1.0/255.0)) discard;
    frag_color = vec4(pc.u_color.rgb, a);
}
)";
}

// Python-side struct Text2DPushData mirror. mat4 (64B) + vec4 (16B) = 80B.
struct Text2DPushData {
    float projection[16];
    float color[4];
};
static_assert(sizeof(Text2DPushData) == 80,
              "Text2DPushData layout drift — shader and C++ disagree");

// SDF push constants: mat4 + vec4 + float. 80B + 4B = 84B.
struct Text2DSdfPushData {
    float projection[16];
    float color[4];
    float smoothing;
    float _pad[3];
};
static_assert(sizeof(Text2DSdfPushData) == 96,
              "Text2DSdfPushData layout drift — shader and C++ disagree");

// Build an ortho matrix (column-major) that maps pixel coords y+down
// → clip-space y+down (Vulkan-native; OpenGL reaches this via
// glClipControl(GL_UPPER_LEFT), which tgfx2 re-applies at the start
// of every render pass in OpenGLCommandList::begin_render_pass).
// Pixel (0,0) maps to clip (-1,-1), i.e. the top-left corner.
//
// The matrix is written in math (row-major) notation but stored
// column-major so we pass `transpose=true` to set_uniform_mat4. That
// mirrors what the Python version does with np.array + flatten.
void build_ortho_pixel_to_ndc(float w, float h, float out[16]) {
    if (w <= 0.0f || h <= 0.0f) {
        std::memset(out, 0, 16 * sizeof(float));
        out[0] = out[5] = out[10] = out[15] = 1.0f;
        return;
    }
    float m[16] = {
        2.0f / w,  0.0f,      0.0f, -1.0f,
        0.0f,      2.0f / h,  0.0f, -1.0f,
        0.0f,      0.0f,      1.0f,  0.0f,
        0.0f,      0.0f,      0.0f,  1.0f,
    };
    std::memcpy(out, m, sizeof(m));
}

}  // namespace

// ---------------------------------------------------------------------------

Text2DRenderer::Text2DRenderer(FontAtlas* font) : font_(font) {}

Text2DRenderer::~Text2DRenderer() {
    // Best-effort release. If the device is already gone we can't
    // destroy shaders — just drop the handles and leak; the process
    // is going down anyway.
    release_gpu();
}

void Text2DRenderer::ensure_shader_(IRenderDevice& device) {
    if (compiled_on_ == &device && vs_.id != 0 && fs_.id != 0
        && vs_sdf_.id != 0 && fs_sdf_.id != 0) return;
    compiled_on_ = &device;

    // Bitmap shader pair.
    if (tc_shader_handle_is_invalid(shader_handle_)) {
        std::string vs = make_text2d_vert();
        std::string fs = make_text2d_frag();
        shader_handle_ = tc_shader_register_static(
            vs.c_str(), fs.c_str(), nullptr, "Text2DEngineVSFS");
    }

    vs_ = ShaderHandle{};
    fs_ = ShaderHandle{};
    if (!tc_shader_handle_is_invalid(shader_handle_)) {
        tc_shader* raw = tc_shader_get(shader_handle_);
        if (raw) {
            termin::tc_shader_ensure_tgfx2(raw, &device, &vs_, &fs_);
        }
    }

    if (vs_.id == 0 || fs_.id == 0) {
        ShaderDesc vs_desc;
        vs_desc.stage = ShaderStage::Vertex;
        vs_desc.source = make_text2d_vert();
        vs_ = device.create_shader(vs_desc);

        ShaderDesc fs_desc;
        fs_desc.stage = ShaderStage::Fragment;
        fs_desc.source = make_text2d_frag();
        fs_ = device.create_shader(fs_desc);
    }

    // SDF shader pair — same vertex shader, SDF fragment shader.
    if (tc_shader_handle_is_invalid(sdf_shader_handle_)) {
        std::string vs = make_text2d_sdf_vert();
        std::string fs = make_text2d_sdf_frag();
        sdf_shader_handle_ = tc_shader_register_static(
            vs.c_str(), fs.c_str(), nullptr, "Text2DEngineSdfVSFS");
    }

    vs_sdf_ = ShaderHandle{};
    fs_sdf_ = ShaderHandle{};
    if (!tc_shader_handle_is_invalid(sdf_shader_handle_)) {
        tc_shader* raw = tc_shader_get(sdf_shader_handle_);
        if (raw) {
            termin::tc_shader_ensure_tgfx2(raw, &device, &vs_sdf_, &fs_sdf_);
        }
    }

    if (vs_sdf_.id == 0 || fs_sdf_.id == 0) {
        ShaderDesc vs_desc;
        vs_desc.stage = ShaderStage::Vertex;
        vs_desc.source = make_text2d_sdf_vert();
        vs_sdf_ = device.create_shader(vs_desc);

        ShaderDesc fs_desc;
        fs_desc.stage = ShaderStage::Fragment;
        fs_desc.source = make_text2d_sdf_frag();
        fs_sdf_ = device.create_shader(fs_desc);
    }
}

void Text2DRenderer::release_gpu() {
    // Shaders live on the tc_shader registry (`shader_handle_`) and are
    // shared across Text2DRenderer instances — nothing to destroy here.
    // Cached handles (vs_/fs_) are just local views into the slot's
    // current tgfx2 ids, stale after the device goes away.
    vs_ = ShaderHandle{};
    fs_ = ShaderHandle{};
    vs_sdf_ = ShaderHandle{};
    fs_sdf_ = ShaderHandle{};
    compiled_on_ = nullptr;
    ctx_ = nullptr;
}

void Text2DRenderer::begin(RenderContext2* ctx,
                            int viewport_w, int viewport_h,
                            FontAtlas* font) {
    if (font != nullptr) font_ = font;
    ctx_ = ctx;

    if (ctx_ != nullptr) {
        ensure_shader_(ctx_->device());
    }

    // Cache the projection matrix so draw() can rebind every call
    // without re-computing. draw() must rebind shader + atlas +
    // projection on every call because callers may interleave Text2D
    // draws with other draws that change the bound shader.
    build_ortho_pixel_to_ndc(
        static_cast<float>(viewport_w),
        static_cast<float>(viewport_h),
        proj_);
}

void Text2DRenderer::draw(std::string_view text_utf8,
                           float x, float y,
                           float r, float g, float b, float a,
                           float size,
                           Anchor anchor) {
    if (text_utf8.empty() || font_ == nullptr || ctx_ == nullptr) return;

    const bool profile = tc_profiler_enabled();

    // Rasterise any missing glyphs for this display size and re-upload
    // the atlas if needed. Bitmap path bakes per-size; SDF path bakes
    // once at reference size. The atlas handles branching internally.
    if (profile) tc_profiler_begin_section("text.ensure_glyphs");
    font_->ensure_glyphs(text_utf8, size, ctx_);
    if (profile) tc_profiler_end_section();

    if (profile) tc_profiler_begin_section("text.measure");
    auto total = font_->measure_text(text_utf8, size);
    const float total_w = total.width;
    if (profile) tc_profiler_end_section();

    float start_x = x;
    float start_y = y;
    switch (anchor) {
        case Anchor::Center:
            start_x = x - total_w * 0.5f;
            start_y = y - size * 0.5f;
            break;
        case Anchor::Right:
            start_x = x - total_w;
            break;
        case Anchor::Left:
        default:
            break;
    }

    // Snap the text origin to the nearest integer pixel so the first
    // glyph's left edge lands on a texel boundary. Without this a
    // fractional start (common after anchor math, DPI scaling, or
    // caller-side sub-pixel layout) spreads every glyph across two
    // columns via bilinear filtering — the dominant cause of the
    // "mыло" / ghosting look on small text. The cursor itself
    // accumulates in float below so kerning / advance don't drift.
    start_x = std::floor(start_x + 0.5f);
    start_y = std::floor(start_y + 0.5f);

    // Rebind shader + push-constants + atlas on every draw — a caller
    // (e.g. UIRenderer) may have bound a different shader between
    // our own begin() and this draw.
    RenderContext2& ctx = *ctx_;

    const bool use_sdf = font_->is_sdf_size(size);

    if (profile) tc_profiler_begin_section("text.bind_shader");
    if (use_sdf) {
        ctx.bind_shader(vs_sdf_, fs_sdf_);
    } else {
        ctx.bind_shader(vs_, fs_);
    }
    if (profile) tc_profiler_end_section();

    if (profile) tc_profiler_begin_section("text.push_constants");
    // Shader expects column-major mat4; `proj_` was stored row-major
    // (see build_ortho_pixel_to_ndc's comment). Transpose here before
    // shipping raw bytes to GPU.
    if (use_sdf) {
        Text2DSdfPushData push;
        for (int row = 0; row < 4; ++row) {
            for (int col = 0; col < 4; ++col) {
                push.projection[col * 4 + row] = proj_[row * 4 + col];
            }
        }
        push.color[0] = r;
        push.color[1] = g;
        push.color[2] = b;
        push.color[3] = a;
        // smoothing: ±1 reference texel edge width → 1/(2*spread) in
        // texture space where edge=0.5 and dist=[0,1] maps to
        // [-spread, +spread] reference texels.
        push.smoothing = 1.0f / (2.0f * static_cast<float>(font_->sdf_spread()));
        ctx.set_push_constants(&push, static_cast<uint32_t>(sizeof(push)));
    } else {
        Text2DPushData push;
        for (int row = 0; row < 4; ++row) {
            for (int col = 0; col < 4; ++col) {
                push.projection[col * 4 + row] = proj_[row * 4 + col];
            }
        }
        push.color[0] = r;
        push.color[1] = g;
        push.color[2] = b;
        push.color[3] = a;
        ctx.set_push_constants(&push, static_cast<uint32_t>(sizeof(push)));
    }
    if (profile) tc_profiler_end_section();

    if (profile) tc_profiler_begin_section("text.ensure_texture");
    TextureHandle atlas = use_sdf ? font_->sdf_atlas_texture(&ctx)
                                  : font_->ensure_texture(&ctx);
    if (profile) tc_profiler_end_section();

    if (profile) tc_profiler_begin_section("text.bind_texture");
    // Binding 4 matches `layout(binding=4) uniform sampler2D
    // u_font_atlas` in both shader variants.
    ctx.bind_sampled_texture(4, atlas);
    if (profile) tc_profiler_end_section();

    // Build one flat vertex array for the whole string.
    if (profile) tc_profiler_begin_section("text.build_quads");
    std::vector<float> verts;
    verts.reserve(text_utf8.size() * 6 * 7);  // rough upper bound

    float cursor_x = start_x;
    size_t i = 0;
    while (i < text_utf8.size()) {
        uint32_t cp = internal::utf8_decode(text_utf8, i);
        const FontAtlas::GlyphInfo* gi = font_->get_glyph(cp, size);
        if (!gi) continue;

        // Metrics are already in display pixels at this size — no
        // scale multiplication here. Matches the atlas contract.
        const float char_w = gi->width_px;
        const float char_h = gi->height_px;

        // Snap the quad's left edge to an integer pixel; keep the
        // width as-is so the glyph shape isn't distorted by rounding
        // both edges (that produces uneven widths across neighbours).
        // cursor_x continues to accumulate in float — round-to-draw
        // doesn't feed back into the advance chain.
        const float px0 = std::floor(cursor_x + 0.5f);
        const float px1 = px0 + char_w;
        const float py0 = start_y;              // top edge in y+down, already snapped
        const float py1 = py0 + char_h;         // bottom edge

        // 6 vertices (2 triangles). CCW in pixel y+down visual →
        // after ortho y-flip → CCW in NDC y+up → front-facing.
        // Triangle 1: TL, BL, BR
        // Triangle 2: TL, BR, TR
        const float u0 = gi->u0, v0 = gi->v0;
        const float u1 = gi->u1, v1 = gi->v1;

        const float quad[] = {
            px0, py0, 0.0f,  u0, v0, 0.0f, 0.0f,  // TL
            px0, py1, 0.0f,  u0, v1, 0.0f, 0.0f,  // BL
            px1, py1, 0.0f,  u1, v1, 0.0f, 0.0f,  // BR
            px0, py0, 0.0f,  u0, v0, 0.0f, 0.0f,  // TL
            px1, py1, 0.0f,  u1, v1, 0.0f, 0.0f,  // BR
            px1, py0, 0.0f,  u1, v0, 0.0f, 0.0f,  // TR
        };
        verts.insert(verts.end(), std::begin(quad), std::end(quad));

        // Advance by the glyph's true horizontal advance, not the ink
        // width: the quad is sized by ink (what we rasterise), the
        // cursor moves by advance (metric that includes sidebearings
        // and gives space characters their width). Advance is already
        // in display pixels at this size.
        cursor_x += gi->advance_px;
    }

    if (profile) tc_profiler_end_section();  // text.build_quads

    if (verts.empty()) return;

    const uint32_t vertex_count = static_cast<uint32_t>(verts.size() / 7);
    ctx.draw_immediate_triangles(verts.data(), vertex_count);
}

void Text2DRenderer::end() {
    ctx_ = nullptr;
}

}  // namespace tgfx
