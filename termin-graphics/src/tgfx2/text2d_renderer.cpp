// text2d_renderer.cpp - Pixel-space text rendering on tgfx2.
//
// Port of termin-graphics/python/tgfx/text2d.py.
//
// Vertex layout matches RenderContext2::draw_immediate_triangles
// (7 floats per vertex: vec3 pos + vec4 misc). The fragment shader
// re-interprets the misc block as (u, v, _, _).

#include "tgfx2/text2d_renderer.hpp"

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

static std::string make_text2d_frag() {
    return std::string("#version 450 core\n") + kText2DCommon + R"(
layout(binding = 4) uniform sampler2D u_font_atlas;

layout(location=0) in vec2 v_uv;
layout(location=0) out vec4 frag_color;

void main() {
    float a = texture(u_font_atlas, v_uv).r * pc.u_color.a;
    if (a < 0.01) discard;
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
    if (compiled_on_ == &device && vs_.id != 0 && fs_.id != 0) return;
    compiled_on_ = &device;

    // Try the tc_shader registry path first — hash-based dedup keeps
    // compiled VkShaderModules alive across Text2DRenderer instances
    // (relevant because each new RenderContext2 on Play/Stop builds a
    // fresh Text2DRenderer). The registry needs tc_gpu_context, which
    // the full editor sets up but the bare launcher UI does not; fall
    // back to direct create_shader for that case.
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
}

void Text2DRenderer::release_gpu() {
    // Shaders live on the tc_shader registry (`shader_handle_`) and are
    // shared across Text2DRenderer instances — nothing to destroy here.
    // Cached handles (vs_/fs_) are just local views into the slot's
    // current tgfx2 ids, stale after the device goes away.
    vs_ = ShaderHandle{};
    fs_ = ShaderHandle{};
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

    // Rasterise any missing glyphs and re-upload the atlas if needed.
    if (profile) tc_profiler_begin_section("text.ensure_glyphs");
    font_->ensure_glyphs(text_utf8, ctx_);
    if (profile) tc_profiler_end_section();

    if (profile) tc_profiler_begin_section("text.measure");
    const float scale = size / static_cast<float>(font_->rasterize_size());
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

    // Rebind shader + push-constants + atlas on every draw — a caller
    // (e.g. UIRenderer) may have bound a different shader between
    // our own begin() and this draw. Uses push-constants on both
    // backends: Vulkan ships them via vkCmdPushConstants, OpenGL
    // through the ring UBO at TGFX2_PUSH_CONSTANTS_BINDING.
    RenderContext2& ctx = *ctx_;

    if (profile) tc_profiler_begin_section("text.bind_shader");
    ctx.bind_shader(vs_, fs_);
    if (profile) tc_profiler_end_section();

    if (profile) tc_profiler_begin_section("text.push_constants");
    Text2DPushData push;
    // Shader expects column-major mat4; `proj_` was stored row-major
    // (see build_ortho_pixel_to_ndc's comment). Transpose here before
    // shipping raw bytes to GPU.
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
    if (profile) tc_profiler_end_section();

    if (profile) tc_profiler_begin_section("text.ensure_texture");
    TextureHandle atlas = font_->ensure_texture(&ctx);
    if (profile) tc_profiler_end_section();

    if (profile) tc_profiler_begin_section("text.bind_texture");
    // Binding 4 matches `layout(binding=4) uniform sampler2D
    // u_font_atlas` in make_text2d_frag().
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
        const FontAtlas::GlyphInfo* gi = font_->get_glyph(cp);
        if (!gi) continue;

        const float char_w = gi->width_px * scale;
        const float char_h = gi->height_px * scale;

        const float px0 = cursor_x;
        const float px1 = cursor_x + char_w;
        const float py0 = start_y;              // top edge in y+down
        const float py1 = start_y + char_h;     // bottom edge

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
        // and gives space characters their width).
        cursor_x += gi->advance_px * scale;
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
