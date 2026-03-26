#pragma once

#include "tgfx2/enums.hpp"

namespace tgfx2 {

struct RasterState {
    CullMode cull = CullMode::Back;
    FrontFace front_face = FrontFace::CCW;
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

} // namespace tgfx2
