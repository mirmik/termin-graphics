// text3d_renderer.cpp - World-space plane text on tgfx2.
//
// Port of termin-graphics/python/tgfx/text3d.py.
//
// Vertex layout is explicit:
//   POSITION  = glyph anchor point in world space
//   TEXCOORD0 = (offset_x, offset_y, u, v)
//
// Every vertex of a glyph quad carries the SAME world position. The shader
// expands the quad via (offset * cam_right + offset * cam_up), where those
// basis vectors may be camera-facing billboard axes or fixed world-plane axes.

#include "tgfx2/text3d_renderer.hpp"

#include "tgfx2/builtin_shader_sources.hpp"
#include <algorithm>
#include <cstddef>
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
    float flags[4];
};
static_assert(sizeof(Text3DPushData) == 128,
              "Text3DPushData layout drift - shader and C++ disagree");

struct Text3DVertex {
    float world_pos[3];
    float offset_uv[4];
};
static_assert(sizeof(Text3DVertex) == 7 * sizeof(float),
              "Text3DVertex layout drift - shader and C++ disagree");

VertexBufferLayout text3d_vertex_layout() {
    VertexBufferLayout layout;
    layout.stride = sizeof(Text3DVertex);
    layout.attributes = {
        {0, VertexFormat::Float3,
         static_cast<uint32_t>(offsetof(Text3DVertex, world_pos)),
         "POSITION"},
        {1, VertexFormat::Float4,
         static_cast<uint32_t>(offsetof(Text3DVertex, offset_uv)),
         "TEXCOORD0"},
    };
    return layout;
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
    const bool screen_aligned = expansion_mode_ == ExpansionMode::ScreenAligned;

    // `size` is the text height in the renderer's expansion units:
    // world units for WorldPlane, clip/NDC units for ScreenAligned.
    font_->ensure_glyphs(text_utf8, kText3DRasterPx, ctx_);

    const float raster_line_h =
        std::max(1.0f, static_cast<float>(font_->line_height_px(kText3DRasterPx)));
    const float glyph_scale = size / raster_line_h;
    const float ascent = static_cast<float>(font_->ascent_px(kText3DRasterPx))
                       * glyph_scale;

    const float total_w = font_->measure_text(text_utf8, kText3DRasterPx).width
                        * glyph_scale;

    float start_x = 0.0f;
    switch (anchor) {
        case Anchor::Center: start_x = -total_w * 0.5f; break;
        case Anchor::Right:  start_x = -total_w;        break;
        case Anchor::Left:
        default: break;
    }

    RenderContext2& ctx = *ctx_;
    ctx.bind_shader(vs_, fs_);
    tc_shader* raw = tc_shader_get(shader_handle_);
    ctx.use_shader_resource_layout(raw);
    ctx.set_cull(CullMode::None);

    Text3DPushData push{};
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
    push.flags[0] = screen_aligned ? 1.0f : 0.0f;
    ctx.bind_uniform_data("text3d_draw", &push, static_cast<uint32_t>(sizeof(push)));

    TextureHandle atlas = font_->ensure_texture(&ctx);
    ctx.bind_texture("u_font_atlas", atlas);

    std::vector<Text3DVertex> verts;
    verts.reserve(text_utf8.size() * 6);

    const float px = position[0];
    const float py = position[1];
    const float pz = position[2];

    float cursor_x = start_x;
    size_t i = 0;
    while (i < text_utf8.size()) {
        uint32_t cp = internal::utf8_decode(text_utf8, i);
        auto gi = font_->get_glyph(cp, kText3DRasterPx);
        if (!gi) continue;

        const float char_w = gi->width_px * glyph_scale;
        const float char_h = gi->height_px * glyph_scale;

        const float left   = cursor_x;
        const float right  = cursor_x + char_w;
        const float top    = ascent;
        const float bottom = ascent - char_h;

        const float u0 = gi->u0, v0 = gi->v0;
        const float u1 = gi->u1, v1 = gi->v1;

        const Text3DVertex quad[] = {
            {{px, py, pz}, {left,  bottom, u0, v1}},
            {{px, py, pz}, {right, bottom, u1, v1}},
            {{px, py, pz}, {left,  top,    u0, v0}},
            {{px, py, pz}, {right, bottom, u1, v1}},
            {{px, py, pz}, {right, top,    u1, v0}},
            {{px, py, pz}, {left,  top,    u0, v0}},
        };
        verts.insert(verts.end(), std::begin(quad), std::end(quad));

        cursor_x += gi->advance_px * glyph_scale;
    }

    if (verts.empty()) return;

    const uint32_t vertex_count = static_cast<uint32_t>(verts.size());
    const VertexBufferLayout layout = text3d_vertex_layout();
    ctx.draw_transient_arrays(
        verts.data(),
        static_cast<uint32_t>(verts.size() * sizeof(Text3DVertex)),
        vertex_count,
        layout,
        PrimitiveTopology::TriangleList);
}

void Text3DRenderer::end() {
    ctx_ = nullptr;
}

}  // namespace tgfx
