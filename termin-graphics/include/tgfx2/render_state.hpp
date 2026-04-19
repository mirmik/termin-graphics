#pragma once

#include "tgfx2/enums.hpp"

namespace tgfx {

struct RasterState {
    CullMode cull = CullMode::Back;
    // Y-flip in projection matrices (Vulkan-native clip, Y+ down — see
    // docs/coord_system.md §2) reverses the visual winding of every
    // triangle in clip space. A mesh whose triangles are authored CCW
    // in view space now rasterises CW; using CCW as the front face
    // would hide every front-facing polygon and render the scene
    // inside-out. Default here is CW so meshes look right out of the box.
    FrontFace front_face = FrontFace::CW;
    PolygonMode polygon_mode = PolygonMode::Fill;
};

struct DepthStencilState {
    bool depth_test = true;
    bool depth_write = true;
    CompareOp depth_compare = CompareOp::Less;
};

struct BlendState {
    bool enabled = false;
    BlendFactor src_color = BlendFactor::SrcAlpha;
    BlendFactor dst_color = BlendFactor::OneMinusSrcAlpha;
    BlendOp color_op = BlendOp::Add;
    BlendFactor src_alpha = BlendFactor::One;
    BlendFactor dst_alpha = BlendFactor::Zero;
    BlendOp alpha_op = BlendOp::Add;
};

struct ColorMask {
    bool r = true;
    bool g = true;
    bool b = true;
    bool a = true;
};

} // namespace tgfx
