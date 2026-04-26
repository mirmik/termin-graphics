// text2d_renderer.hpp - Screen-space 2D text renderer for tgfx2.
//
// Works in pixel coordinates with an orthographic projection (y+ down).
// The ortho matrix flips Y so vertices emitted CCW in pixel space land
// CCW in NDC (y+ up) — survives the default CullMode::Back.
//
// Usage:
//   Text2DRenderer t2d(font);            // optional font
//   t2d.begin(ctx, viewport_w, viewport_h); // shader is compiled lazily
//   t2d.draw("hello", 20, 40, 1,1,1,1, 14, Anchor::Left);
//   t2d.end();
//
// Design mirrors the Python reference implementation:
//   - shader + atlas + projection are rebound on every draw() to
//     survive state changes from interleaved callers (e.g. UIRenderer
//     drawing a rect between two draw_text calls),
//   - missing glyphs are rasterised on demand and re-uploaded,
//   - measurement / scaling use the FontAtlas's ink-width semantics.
#pragma once

#include <cstdint>
#include <string_view>

#include "tgfx2/handles.hpp"
#include "tgfx2/tgfx2_api.h"
extern "C" {
#include <tgfx/resources/tc_shader_registry.h>
}

namespace tgfx {

class RenderContext2;
class IRenderDevice;
class FontAtlas;

class TGFX2_API Text2DRenderer {
public:
    enum class Anchor : uint8_t { Left, Center, Right };

private:
    // Shader lives on the tc_shader registry — shared across
    // Text2DRenderer instances so Play/Stop doesn't re-run shaderc.
    // vs_/fs_ are per-frame cached views into the slot's current ids.
    IRenderDevice* compiled_on_ = nullptr;
    tc_shader_handle shader_handle_ = tc_shader_handle_invalid();
    ShaderHandle vs_{};
    ShaderHandle fs_{};
    // SDF shader pair (different fragment shader, extended push constants).
    tc_shader_handle sdf_shader_handle_ = tc_shader_handle_invalid();
    ShaderHandle vs_sdf_{};
    ShaderHandle fs_sdf_{};

    // Active frame state (valid between begin() and end()).
    RenderContext2* ctx_ = nullptr;
    FontAtlas* font_ = nullptr;
    float proj_[16]{};

public:
    explicit Text2DRenderer(FontAtlas* font = nullptr);
    ~Text2DRenderer();

    Text2DRenderer(const Text2DRenderer&) = delete;
    Text2DRenderer& operator=(const Text2DRenderer&) = delete;

    // Set up a frame. Compiles the shader on first call or when the
    // underlying IRenderDevice changes (caller re-created its context).
    // If `font` is non-null it replaces any earlier font; the caller
    // retains ownership.
    void begin(RenderContext2* ctx, int viewport_w, int viewport_h,
               FontAtlas* font = nullptr);

    // Draw a UTF-8 string anchored at pixel (x, y). Color components
    // in [0, 1]. `size` is the display pixel height (glyphs are
    // scaled from the atlas rasterise size). Anchor selects whether
    // (x, y) is the left, centre or right of the text box (top-aligned
    // on Y for Left/Right, center for Center).
    void draw(std::string_view text_utf8,
              float x, float y,
              float r, float g, float b, float a,
              float size = 14.0f,
              Anchor anchor = Anchor::Left);

    // End batch. Currently a no-op — shader/state stays bound on ctx
    // until the caller rebinds or ends its pass.
    void end();

    // Drop the compiled shader. Call when the GL context is torn down
    // so the destructor does not reach into a dead device.
    void release_gpu();

    // Current font atlas (may be null).
    FontAtlas* font() const { return font_; }

private:
    void ensure_shader_(IRenderDevice& device);
};

}  // namespace tgfx
