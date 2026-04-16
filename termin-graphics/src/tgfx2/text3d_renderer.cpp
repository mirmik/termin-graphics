// text3d_renderer.cpp - World-space billboard text on tgfx2.
//
// Port of termin-graphics/python/tgfx/text3d.py.
//
// Vertex layout matches draw_immediate_triangles (7 floats per vertex:
// vec3 + vec4). The shader re-interprets the second slot as
// (offset_x, offset_y, u, v) — same byte shape, different semantics.
//
// Every vertex of a glyph quad carries the SAME world position; the
// shader expands the quad via (offset * cam_right + offset * cam_up),
// so the glyph always faces the camera regardless of view rotation.

#include "tgfx2/text3d_renderer.hpp"

#include <cstring>
#include <vector>

#include "tgfx2/descriptors.hpp"
#include "tgfx2/enums.hpp"
#include "tgfx2/font_atlas.hpp"
#include "tgfx2/i_render_device.hpp"
#include "tgfx2/render_context.hpp"

#include "internal/utf8_decode.hpp"

namespace tgfx2 {

namespace {

constexpr const char* kText3DVert = R"(#version 330 core
layout(location=0) in vec3 a_world_pos;
layout(location=1) in vec4 a_offset_uv;  // (offset.x, offset.y, u, v)

uniform mat4 u_mvp;
uniform vec3 u_cam_right;
uniform vec3 u_cam_up;

out vec2 v_uv;

void main() {
    vec3 pos = a_world_pos
             + u_cam_right * a_offset_uv.x
             + u_cam_up    * a_offset_uv.y;
    gl_Position = u_mvp * vec4(pos, 1.0);
    v_uv = a_offset_uv.zw;
}
)";

constexpr const char* kText3DFrag = R"(#version 330 core
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

}  // namespace

// ---------------------------------------------------------------------------

Text3DRenderer::Text3DRenderer(FontAtlas* font) : font_(font) {}

Text3DRenderer::~Text3DRenderer() {
    release_gpu();
}

void Text3DRenderer::ensure_shader_(IRenderDevice& device) {
    if (compiled_on_ == &device && vs_.id != 0 && fs_.id != 0) {
        return;
    }
    if (compiled_on_ != nullptr) {
        if (vs_.id != 0) compiled_on_->destroy(vs_);
        if (fs_.id != 0) compiled_on_->destroy(fs_);
    }
    vs_ = ShaderHandle{};
    fs_ = ShaderHandle{};

    ShaderDesc vs_desc;
    vs_desc.stage = ShaderStage::Vertex;
    vs_desc.source = kText3DVert;
    vs_ = device.create_shader(vs_desc);

    ShaderDesc fs_desc;
    fs_desc.stage = ShaderStage::Fragment;
    fs_desc.source = kText3DFrag;
    fs_ = device.create_shader(fs_desc);

    compiled_on_ = &device;
}

void Text3DRenderer::release_gpu() {
    if (compiled_on_ != nullptr) {
        if (vs_.id != 0) compiled_on_->destroy(vs_);
        if (fs_.id != 0) compiled_on_->destroy(fs_);
    }
    vs_ = ShaderHandle{};
    fs_ = ShaderHandle{};
    compiled_on_ = nullptr;
    ctx_ = nullptr;
}

void Text3DRenderer::begin(RenderContext2* ctx,
                            const float mvp[16],
                            const float cam_right[3],
                            const float cam_up[3],
                            FontAtlas* font) {
    if (font != nullptr) font_ = font;
    ctx_ = ctx;

    if (ctx_ != nullptr) {
        ensure_shader_(ctx_->device());
    }

    std::memcpy(mvp_, mvp, sizeof(mvp_));
    std::memcpy(cam_right_, cam_right, sizeof(cam_right_));
    std::memcpy(cam_up_, cam_up, sizeof(cam_up_));
}

void Text3DRenderer::draw(std::string_view text_utf8,
                           const float position[3],
                           float r, float g, float b, float a,
                           float size,
                           Anchor anchor) {
    if (text_utf8.empty() || font_ == nullptr || ctx_ == nullptr) return;

    // Rasterise any new glyphs and push to the atlas if needed.
    font_->ensure_glyphs(text_utf8, ctx_);

    const float scale = size / static_cast<float>(font_->rasterize_size());
    const float ascent = static_cast<float>(font_->ascent_px()) * scale;

    auto total = font_->measure_text(text_utf8, size);
    const float total_w = total.width;

    float start_x = 0.0f;
    switch (anchor) {
        case Anchor::Center: start_x = -total_w * 0.5f; break;
        case Anchor::Right:  start_x = -total_w;        break;
        case Anchor::Left:
        default: break;
    }

    // Rebind shader + atlas + per-frame uniforms on every draw so we
    // survive state changes made by interleaved callers.
    RenderContext2& ctx = *ctx_;
    ctx.bind_shader(vs_, fs_);
    // mvp_ arrived column-major from the caller (PlotEngine3D /
    // orbit_camera). GL expects column-major too; no transpose.
    // Text2DRenderer's projection is built row-major in a local
    // helper, so THERE transpose=true is correct — don't mirror that
    // flag here.
    ctx.set_uniform_mat4("u_mvp", mvp_, /*transpose=*/false);
    ctx.set_uniform_vec3("u_cam_right", cam_right_[0], cam_right_[1], cam_right_[2]);
    ctx.set_uniform_vec3("u_cam_up",    cam_up_[0],    cam_up_[1],    cam_up_[2]);
    ctx.set_uniform_int("u_font_atlas", 0);
    TextureHandle atlas = font_->ensure_texture(&ctx);
    ctx.bind_sampled_texture(0, atlas);
    ctx.set_uniform_vec4("u_color", r, g, b, a);

    // Build one flat vertex array for the whole string.
    std::vector<float> verts;
    verts.reserve(text_utf8.size() * 6 * 7);

    const float px = position[0];
    const float py = position[1];
    const float pz = position[2];

    float cursor_x = start_x;
    size_t i = 0;
    while (i < text_utf8.size()) {
        uint32_t cp = internal::utf8_decode(text_utf8, i);
        const FontAtlas::GlyphInfo* gi = font_->get_glyph(cp);
        if (!gi) continue;

        const float char_w = gi->width_px * scale;
        const float char_h = gi->height_px * scale;

        const float left   = cursor_x;
        const float right  = cursor_x + char_w;
        const float top    = ascent;
        const float bottom = ascent - char_h;

        const float u0 = gi->u0, v0 = gi->v0;
        const float u1 = gi->u1, v1 = gi->v1;

        // 6 vertices (2 triangles). CCW in NDC y+up so we survive the
        // default CullMode::Back in tgfx2. Per vertex:
        //   [world_x, world_y, world_z, offset_x, offset_y, u, v]
        const float quad[] = {
            // Triangle 1: BL, BR, TL
            px, py, pz,  left,  bottom, u0, v1,
            px, py, pz,  right, bottom, u1, v1,
            px, py, pz,  left,  top,    u0, v0,
            // Triangle 2: BR, TR, TL
            px, py, pz,  right, bottom, u1, v1,
            px, py, pz,  right, top,    u1, v0,
            px, py, pz,  left,  top,    u0, v0,
        };
        verts.insert(verts.end(), std::begin(quad), std::end(quad));

        cursor_x += char_w;
    }

    if (verts.empty()) return;

    const uint32_t vertex_count = static_cast<uint32_t>(verts.size() / 7);
    ctx.draw_immediate_triangles(verts.data(), vertex_count);
}

void Text3DRenderer::end() {
    ctx_ = nullptr;
}

}  // namespace tgfx2
