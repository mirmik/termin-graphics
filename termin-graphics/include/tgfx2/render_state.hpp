#pragma once

#include "tgfx2/enums.hpp"

namespace tgfx {

struct RasterState {
    CullMode cull = CullMode::Back;
    // Logical mesh-authoring winding before backend clip/viewport adapters.
    // Native rasterizer mappings own any Y-convention inversion; callers and
    // render passes must not compensate for it (docs/coord_system.md §4a).
    FrontFace front_face = FrontFace::CCW;
    PolygonMode polygon_mode = PolygonMode::Fill;
    bool depth_bias_enabled = false;
    float depth_bias_constant = 0.0f;
    float depth_bias_slope = 0.0f;
    float depth_bias_clamp = 0.0f;
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
