// canvas2d_renderer.hpp - Reusable immediate 2D drawing facade for tgfx2.
//
// Canvas2DRenderer does not own frame/pass lifecycle. Callers open a
// RenderContext2 frame/pass, then begin() the canvas inside that pass.
#pragma once

#include <span>
#include <string_view>
#include <vector>

#include "tgfx2/font_atlas.hpp"
#include "tgfx2/handles.hpp"
#include "tgfx2/text2d_renderer.hpp"
#include "tgfx2/tgfx2_api.h"

namespace tgfx {

class RenderContext2;
class IRenderDevice;

struct CanvasColor {
    float r = 1.0f;
    float g = 1.0f;
    float b = 1.0f;
    float a = 1.0f;

    static CanvasColor white() { return {1.0f, 1.0f, 1.0f, 1.0f}; }
    static CanvasColor transparent() { return {0.0f, 0.0f, 0.0f, 0.0f}; }
};

struct CanvasVec2 {
    float x = 0.0f;
    float y = 0.0f;
};

class TGFX2_API Canvas2DRenderer {
public:
    explicit Canvas2DRenderer(FontAtlas* default_font = nullptr);
    ~Canvas2DRenderer();

    Canvas2DRenderer(const Canvas2DRenderer&) = delete;
    Canvas2DRenderer& operator=(const Canvas2DRenderer&) = delete;

    void begin(RenderContext2& ctx, int width, int height);
    void begin(RenderContext2& ctx, int x, int y, int width, int height);
    void end();

    void begin_clip(float x, float y, float w, float h);
    void end_clip();

    void draw_rect(float x, float y, float w, float h,
                   CanvasColor color, float radius = 0.0f);
    void draw_circle(float cx, float cy, float radius,
                     CanvasColor color, int segments = 24);
    void draw_rect_outline(float x, float y, float w, float h,
                           CanvasColor color, float thickness = 1.0f);
    void draw_line(float x0, float y0, float x1, float y1,
                   CanvasColor color, float thickness = 1.0f);
    void draw_polyline(std::span<const CanvasVec2> points,
                       CanvasColor color, float thickness = 1.0f);
    void draw_texture(TextureHandle texture,
                      float x, float y, float w, float h,
                      CanvasColor tint = CanvasColor::white(),
                      bool flip_v = false);

    void draw_text(std::string_view text, float x, float y,
                   float size_px, CanvasColor color,
                   FontAtlas* font = nullptr,
                   Text2DRenderer::Anchor anchor = Text2DRenderer::Anchor::Left);
    FontAtlas::Size2f measure_text(std::string_view text,
                                   float size_px,
                                   FontAtlas* font = nullptr) const;

    void set_default_font(FontAtlas* font) { default_font_ = font; }
    FontAtlas* default_font() const { return default_font_; }

    void release_gpu();

private:
    enum class BatchMode {
        None,
        Solid,
        Texture,
    };

    struct ClipRect {
        int x = 0;
        int y = 0;
        int w = 0;
        int h = 0;
    };

    RenderContext2* ctx_ = nullptr;
    FontAtlas* default_font_ = nullptr;
    Text2DRenderer text2d_;

    int viewport_x_ = 0;
    int viewport_y_ = 0;
    int viewport_w_ = 0;
    int viewport_h_ = 0;
    float projection_[16]{};

    IRenderDevice* compiled_on_ = nullptr;
    ShaderHandle solid_vs_{};
    ShaderHandle solid_fs_{};
    ShaderHandle texture_vs_{};
    ShaderHandle texture_fs_{};

    std::vector<ClipRect> clip_stack_;

    BatchMode batch_mode_ = BatchMode::None;
    CanvasColor batch_color_{};
    TextureHandle batch_texture_{};
    std::vector<float> batch_vertices_;

    void ensure_shaders_(IRenderDevice& device);
    void build_projection_();
    void flush_();
    void bind_solid_(CanvasColor color);
    void bind_texture_(CanvasColor tint, TextureHandle texture);
    void push_quad_(float x0, float y0, float x1, float y1,
                    float u0, float v0, float u1, float v1);
    void append_solid_quad_(float x0, float y0, float x1, float y1,
                            CanvasColor color);
    void append_textured_quad_(float x0, float y0, float x1, float y1,
                               float u0, float v0, float u1, float v1,
                               CanvasColor tint, TextureHandle texture);
};

}  // namespace tgfx
