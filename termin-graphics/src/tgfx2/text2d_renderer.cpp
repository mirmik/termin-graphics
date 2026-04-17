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

#include "internal/utf8_decode.hpp"

namespace tgfx {

namespace {

constexpr const char* kText2DVert = R"(#version 330 core
layout(location=0) in vec3 a_pos;    // (x_pixel, y_pixel, 0)
layout(location=1) in vec4 a_uv_pad; // (u, v, _, _)

uniform mat4 u_projection;

out vec2 v_uv;

void main() {
    gl_Position = u_projection * vec4(a_pos.xy, 0.0, 1.0);
    v_uv = a_uv_pad.xy;
}
)";

constexpr const char* kText2DFrag = R"(#version 330 core
uniform sampler2D u_font_atlas;
uniform vec4 u_color;

in vec2 v_uv;
out vec4 frag_color;

void main() {
    float a = texture(u_font_atlas, v_uv).r * u_color.a;
    if (a < 0.01) discard;
    frag_color = vec4(u_color.rgb, a);
}
)";

// Build an ortho matrix (column-major) that maps pixel coords y+down
// → NDC y+up. (0,0) pixel → (-1,+1) NDC (top-left corner).
//
// The matrix is written here in math (row-major) notation but stored
// column-major so we pass `transpose=true` to set_uniform_mat4. That
// mirrors what the Python version does with np.array + flatten.
void build_ortho_pixel_to_ndc(float w, float h, float out[16]) {
    // Identity for degenerate viewport — avoids NaN downstream.
    if (w <= 0.0f || h <= 0.0f) {
        std::memset(out, 0, 16 * sizeof(float));
        out[0] = out[5] = out[10] = out[15] = 1.0f;
        return;
    }
    // Row-major layout (we upload with transpose=true):
    //   [ 2/w,  0,     0,   -1 ]
    //   [ 0,   -2/h,   0,    1 ]
    //   [ 0,    0,    -1,    0 ]
    //   [ 0,    0,     0,    1 ]
    float m[16] = {
        2.0f / w,  0.0f,      0.0f, -1.0f,
        0.0f,     -2.0f / h,  0.0f,  1.0f,
        0.0f,      0.0f,     -1.0f,  0.0f,
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
    if (compiled_on_ == &device && vs_.id != 0 && fs_.id != 0) {
        return;
    }
    // Device changed or first time — drop any prior handles and
    // recompile. Handles only live across a single device's lifetime.
    if (compiled_on_ != nullptr) {
        if (vs_.id != 0) compiled_on_->destroy(vs_);
        if (fs_.id != 0) compiled_on_->destroy(fs_);
    }
    vs_ = ShaderHandle{};
    fs_ = ShaderHandle{};

    ShaderDesc vs_desc;
    vs_desc.stage = ShaderStage::Vertex;
    vs_desc.source = kText2DVert;
    vs_ = device.create_shader(vs_desc);

    ShaderDesc fs_desc;
    fs_desc.stage = ShaderStage::Fragment;
    fs_desc.source = kText2DFrag;
    fs_ = device.create_shader(fs_desc);

    compiled_on_ = &device;
}

void Text2DRenderer::release_gpu() {
    if (compiled_on_ != nullptr) {
        if (vs_.id != 0) compiled_on_->destroy(vs_);
        if (fs_.id != 0) compiled_on_->destroy(fs_);
    }
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

    // Rasterise any missing glyphs and re-upload the atlas if needed.
    font_->ensure_glyphs(text_utf8, ctx_);

    const float scale = size / static_cast<float>(font_->rasterize_size());
    auto total = font_->measure_text(text_utf8, size);
    const float total_w = total.width;

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

    // Rebind shader + projection + atlas on every draw — a caller
    // (e.g. UIRenderer) may have bound a different shader between
    // our own begin() and this draw. Cheap in practice: one
    // bind_shader + three uniform sets + one texture bind.
    RenderContext2& ctx = *ctx_;
    ctx.bind_shader(vs_, fs_);
    ctx.set_uniform_mat4("u_projection", proj_, /*transpose=*/true);
    ctx.set_uniform_int("u_font_atlas", 0);
    TextureHandle atlas = font_->ensure_texture(&ctx);
    ctx.bind_sampled_texture(0, atlas);
    ctx.set_uniform_vec4("u_color", r, g, b, a);

    // Build one flat vertex array for the whole string.
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

    if (verts.empty()) return;

    const uint32_t vertex_count = static_cast<uint32_t>(verts.size() / 7);
    ctx.draw_immediate_triangles(verts.data(), vertex_count);
}

void Text2DRenderer::end() {
    ctx_ = nullptr;
}

}  // namespace tgfx
