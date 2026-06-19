// text3d_renderer.cpp - World-space plane text on tgfx2.
//
// Port of termin-graphics/python/tgfx/text3d.py.
//
// Vertex layout matches draw_immediate_triangles (7 floats per vertex:
// vec3 + vec4). The shader re-interprets the second slot as
// (offset_x, offset_y, u, v) — same byte shape, different semantics.
//
// Every vertex of a glyph quad carries the SAME world position. The shader
// expands the quad via (offset * cam_right + offset * cam_up), where those
// basis vectors may be camera-facing billboard axes or fixed world-plane axes.

#include "tgfx2/text3d_renderer.hpp"

#include "tgfx2/builtin_shader_sources.hpp"
#include <algorithm>
#include <cstring>
#include <vector>

#include "tgfx2/enums.hpp"
#include "tgfx2/font_atlas.hpp"
#include "tgfx2/i_render_device.hpp"
#include "tgfx2/render_context.hpp"
#include "tgfx2/tc_shader_bridge.hpp"

#include "internal/utf8_decode.hpp"

extern "C" {
#include <tgfx/resources/tc_shader.h>
}

extern "C" {
#include "tgfx/tgfx2_interop.h"
}

#include <tcbase/tc_log.hpp>

namespace tgfx {

namespace {

constexpr float kText3DRasterPx = 16.0f;
constexpr const char* TEXT3D_SHADER_UUID = "termin-engine-text3d";

struct Text3DPushData {
    float mvp[16];
    float color[4];
    float cam_right[4];
    float cam_up[4];
};
static_assert(sizeof(Text3DPushData) == 112,
              "Text3DPushData layout drift — shader and C++ disagree");

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
    vs_ = ShaderHandle{};
    fs_ = ShaderHandle{};

    if (tc_shader_handle_is_invalid(shader_handle_)) {
        shader_handle_ = register_builtin_shader_from_catalog(TEXT3D_SHADER_UUID);
    }

    if (!tc_shader_handle_is_invalid(shader_handle_)) {
        tc_shader* raw = tc_shader_get(shader_handle_);
        if (raw && !termin::tc_shader_ensure_tgfx2(raw, &device, &vs_, &fs_)) {
            tc::Log::error("[Text3DRenderer] failed to create shader");
        }
    }

    if (vs_.id == 0 || fs_.id == 0) {
        tc::Log::error("[Text3DRenderer] shader is unavailable");
    }

    compiled_on_ = &device;
}

void Text3DRenderer::release_gpu() {
    // Shaders live in the tc_shader registry and are shared across
    // Text3DRenderer instances; cached handles are local views only.
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

    // `size` is a world-space text height. FontAtlas sizes are
    // display-pixel raster sizes, so keep a stable bitmap atlas size
    // and scale metrics into world units below. Passing world sizes
    // such as 0.5 directly to FontAtlas quantises to its minimum pixel
    // size and then uses 4-16 as world units, producing huge blurred
    // quads in 3D plots.
    font_->ensure_glyphs(text_utf8, kText3DRasterPx, ctx_);

    const float raster_line_h =
        std::max(1.0f, static_cast<float>(font_->line_height_px(kText3DRasterPx)));
    const float world_scale = size / raster_line_h;
    const float ascent = static_cast<float>(font_->ascent_px(kText3DRasterPx))
                       * world_scale;

    auto total = font_->measure_text(text_utf8, kText3DRasterPx);
    const float total_w = total.width * world_scale;

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
    tc_shader* raw = tc_shader_get(shader_handle_);
    ctx.use_shader_resource_layout(raw);

    Text3DPushData push{};
    // mvp_ arrived column-major from the caller. The shader consumes the same
    // column-major layout.
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
    ctx.bind_uniform_data("text3d_draw", &push, static_cast<uint32_t>(sizeof(push)));

    TextureHandle atlas = font_->ensure_texture(&ctx);
    ctx.bind_texture("u_font_atlas", atlas);

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
        const FontAtlas::GlyphInfo* gi = font_->get_glyph(cp, kText3DRasterPx);
        if (!gi) continue;

        const float char_w = gi->width_px * world_scale;
        const float char_h = gi->height_px * world_scale;

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
        cursor_x += gi->advance_px * world_scale;
    }

    if (verts.empty()) return;

    const uint32_t vertex_count = static_cast<uint32_t>(verts.size() / 7);
    ctx.draw_immediate_triangles(verts.data(), vertex_count);
}

void Text3DRenderer::end() {
    ctx_ = nullptr;
}

}  // namespace tgfx
