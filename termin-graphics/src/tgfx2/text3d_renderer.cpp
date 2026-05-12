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
#include <string>
#include <vector>

#include "tgfx2/descriptors.hpp"
#include "tgfx2/enums.hpp"
#include "tgfx2/font_atlas.hpp"
#include "tgfx2/i_render_device.hpp"
#include "tgfx2/render_context.hpp"

#include "internal/utf8_decode.hpp"

extern "C" {
#include "tgfx/tgfx2_interop.h"
}

namespace tgfx {

namespace {

struct Text3DPushData {
    float mvp[16];
    float color[4];
    float cam_right[4];
    float cam_up[4];
};
static_assert(sizeof(Text3DPushData) == 112,
              "Text3DPushData layout drift — shader and C++ disagree");

constexpr const char* kText3DCommon = R"(
struct Text3DPush {
    mat4 u_mvp;
    vec4 u_color;
    vec4 u_cam_right;
    vec4 u_cam_up;
};
#ifdef VULKAN
layout(push_constant) uniform Text3DPushBlock { Text3DPush pc; };
#else
layout(std140, binding = 14) uniform Text3DPushBlock { Text3DPush pc; };
#endif
)";

static std::string make_text3d_vert() {
    return std::string("#version 450 core\n") + kText3DCommon + R"(
layout(location=0) in vec3 a_world_pos;
layout(location=1) in vec4 a_offset_uv;  // (offset.x, offset.y, u, v)

layout(location=0) out vec2 v_uv;

void main() {
    vec3 pos = a_world_pos
             + pc.u_cam_right.xyz * a_offset_uv.x
             + pc.u_cam_up.xyz    * a_offset_uv.y;
    gl_Position = pc.u_mvp * vec4(pos, 1.0);
    v_uv = a_offset_uv.zw;
}
)";
}

static std::string make_text3d_frag() {
    return std::string("#version 450 core\n") + kText3DCommon + R"(
layout(binding=4) uniform sampler2D u_font_atlas;

layout(location=0) in vec2 v_uv;
layout(location=0) out vec4 frag_color;

void main() {
    float a = texture(u_font_atlas, v_uv).r * pc.u_color.a;
    // Threshold at one 8-bit alpha level — same rationale as in
    // Text2DRenderer. 0.01 was high enough to nibble AA tail pixels.
    if (a < (1.0/255.0)) discard;
    frag_color = vec4(pc.u_color.rgb, a);
}
)";
}

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
    vs_desc.source = make_text3d_vert();
    vs_ = device.create_shader(vs_desc);

    ShaderDesc fs_desc;
    fs_desc.stage = ShaderStage::Fragment;
    fs_desc.source = make_text3d_frag();
    fs_ = device.create_shader(fs_desc);

    compiled_on_ = &device;
}

void Text3DRenderer::release_gpu() {
    // See Text2DRenderer::release_gpu for the lifetime story. Leak at
    // interpreter shutdown when the device is already gone.
    if (compiled_on_ != nullptr) {
        void* live = tgfx2_interop_get_device();
        if (live == compiled_on_) {
            if (vs_.id != 0) compiled_on_->destroy(vs_);
            if (fs_.id != 0) compiled_on_->destroy(fs_);
        }
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

    // Rasterise any new glyphs for this size and push to the atlas if
    // needed. The atlas keys bakes by (codepoint, px_size) so every
    // unique display size gets its own native-resolution rasterisation.
    font_->ensure_glyphs(text_utf8, size, ctx_);

    // Ascent is already in display pixels at this size.
    const float ascent = static_cast<float>(font_->ascent_px(size));

    auto total = font_->measure_text(text_utf8, size);
    const float total_w = total.width;

    float start_x = 0.0f;
    switch (anchor) {
        case Anchor::Center: start_x = -total_w * 0.5f; break;
        case Anchor::Right:  start_x = -total_w;        break;
        case Anchor::Left:
        default: break;
    }

    // Rebind shader + atlas + per-draw state on every draw so we
    // survive state changes made by interleaved callers.
    RenderContext2& ctx = *ctx_;
    ctx.bind_shader(vs_, fs_);

    Text3DPushData push{};
    // mvp_ arrived column-major from the caller (PlotEngine3D /
    // orbit_camera). The shader consumes the same column-major layout.
    std::memcpy(push.mvp, mvp_, sizeof(push.mvp));
    push.color[0] = r;
    push.color[1] = g;
    push.color[2] = b;
    push.color[3] = a;
    push.cam_right[0] = cam_right_[0];
    push.cam_right[1] = cam_right_[1];
    push.cam_right[2] = cam_right_[2];
    push.cam_up[0] = cam_up_[0];
    push.cam_up[1] = cam_up_[1];
    push.cam_up[2] = cam_up_[2];
    ctx.set_push_constants(&push, static_cast<uint32_t>(sizeof(push)));

    TextureHandle atlas = font_->ensure_texture(&ctx);
    ctx.bind_sampled_texture(4, atlas);

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
        const FontAtlas::GlyphInfo* gi = font_->get_glyph(cp, size);
        if (!gi) continue;

        // Metrics already in display px at this size — no * scale.
        const float char_w = gi->width_px;
        const float char_h = gi->height_px;

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

        // See Text2DRenderer: advance by the glyph's true advance, not
        // ink width — so space glyphs actually move the cursor.
        // Advance is already in display pixels at this size.
        cursor_x += gi->advance_px;
    }

    if (verts.empty()) return;

    const uint32_t vertex_count = static_cast<uint32_t>(verts.size() / 7);
    ctx.draw_immediate_triangles(verts.data(), vertex_count);
}

void Text3DRenderer::end() {
    ctx_ = nullptr;
}

}  // namespace tgfx
