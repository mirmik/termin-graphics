// text3d_renderer.hpp - 3D-anchored text renderer for tgfx2.
//
// Draws text attached to a 3D world position. WorldPlane expands glyph quads
// in world space using caller-provided basis vectors, which supports fixed
// world labels. ScreenAligned projects only the world anchor and expands the
// glyph quad in clip space, which is the preferred mode for annotations such
// as plot axis labels.
//
// Usage:
//   Text3DRenderer t3d(font);
//   t3d.begin(ctx, mvp, cam_right, cam_up);
//   t3d.draw("hello", pos, 1,1,1,1, 0.1f);
//   t3d.end();
//
// The caller supplies the text plane basis directly:
//   - mvp:       4x4 column-major (projection * view * model). 16 floats.
//   - cam_right: 3 floats - world-space text-right basis.
//   - cam_up:    3 floats - world-space text-up basis.
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

class TGFX2_TYPE_API Text3DRenderer {
public:
    enum class Anchor : uint8_t { Left, Center, Right };
    enum class ExpansionMode : uint8_t { WorldPlane, ScreenAligned };

private:
    IRenderDevice* compiled_on_ = nullptr;
    tc_shader_handle shader_handle_ = tc_shader_handle_invalid();
    ShaderHandle vs_{};
    ShaderHandle fs_{};

    RenderContext2* ctx_ = nullptr;
    FontAtlas* font_ = nullptr;
    float mvp_[16]{};
    float cam_right_[3]{};
    float cam_up_[3]{};
    ExpansionMode expansion_mode_ = ExpansionMode::WorldPlane;

public:
    explicit Text3DRenderer(FontAtlas* font = nullptr);
    ~Text3DRenderer();

    Text3DRenderer(const Text3DRenderer&) = delete;
    Text3DRenderer& operator=(const Text3DRenderer&) = delete;

    // Set up a frame. Compiles the shader on first call or when the
    // device changes. mvp/cam_right/cam_up are copied by value; the
    // caller may free their source buffers immediately.
    void begin(RenderContext2* ctx,
               const float mvp[16],
               const float cam_right[3],
               const float cam_up[3],
               FontAtlas* font = nullptr);

    // Draw a UTF-8 string at world-space `position`. `size` is the text
    // height in expansion units: world units for WorldPlane, clip/NDC
    // units for ScreenAligned. Color in [0, 1].
    void draw(std::string_view text_utf8,
              const float position[3],
              float r, float g, float b, float a,
              float size = 0.05f,
              Anchor anchor = Anchor::Center);

    void end();
    void release_gpu();

    void set_expansion_mode(ExpansionMode mode) { expansion_mode_ = mode; }
    ExpansionMode expansion_mode() const { return expansion_mode_; }

    FontAtlas* font() const { return font_; }

private:
    void ensure_shader_(IRenderDevice& device);
};

}  // namespace tgfx
