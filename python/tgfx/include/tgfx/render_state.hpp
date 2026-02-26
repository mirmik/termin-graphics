#pragma once

#include <string>

namespace termin {

enum class PolygonMode {
    Fill,
    Line
};

inline const char* polygon_mode_to_string(PolygonMode m) {
    switch (m) {
        case PolygonMode::Fill: return "fill";
        case PolygonMode::Line: return "line";
    }
    return "fill";
}

inline PolygonMode polygon_mode_from_string(const std::string& s) {
    if (s == "line") return PolygonMode::Line;
    return PolygonMode::Fill;
}

enum class BlendFactor {
    Zero,
    One,
    SrcAlpha,
    OneMinusSrcAlpha
};

inline const char* blend_factor_to_string(BlendFactor f) {
    switch (f) {
        case BlendFactor::Zero: return "zero";
        case BlendFactor::One: return "one";
        case BlendFactor::SrcAlpha: return "src_alpha";
        case BlendFactor::OneMinusSrcAlpha: return "one_minus_src_alpha";
    }
    return "src_alpha";
}

inline BlendFactor blend_factor_from_string(const std::string& s) {
    if (s == "zero") return BlendFactor::Zero;
    if (s == "one") return BlendFactor::One;
    if (s == "one_minus_src_alpha") return BlendFactor::OneMinusSrcAlpha;
    return BlendFactor::SrcAlpha;
}

enum class DepthFunc {
    Less,
    LessEqual,
    Equal,
    Greater,
    GreaterEqual,
    NotEqual,
    Always,
    Never
};

inline const char* depth_func_to_string(DepthFunc f) {
    switch (f) {
        case DepthFunc::Less: return "less";
        case DepthFunc::LessEqual: return "lequal";
        case DepthFunc::Equal: return "equal";
        case DepthFunc::Greater: return "greater";
        case DepthFunc::GreaterEqual: return "gequal";
        case DepthFunc::NotEqual: return "notequal";
        case DepthFunc::Always: return "always";
        case DepthFunc::Never: return "never";
    }
    return "less";
}

inline DepthFunc depth_func_from_string(const std::string& s) {
    if (s == "lequal") return DepthFunc::LessEqual;
    if (s == "equal") return DepthFunc::Equal;
    if (s == "greater") return DepthFunc::Greater;
    if (s == "gequal") return DepthFunc::GreaterEqual;
    if (s == "notequal") return DepthFunc::NotEqual;
    if (s == "always") return DepthFunc::Always;
    if (s == "never") return DepthFunc::Never;
    return DepthFunc::Less;
}

// Complete render state for a draw call.
struct RenderState {
    PolygonMode polygon_mode = PolygonMode::Fill;
    bool cull = true;
    bool depth_test = true;
    bool depth_write = true;
    bool blend = false;
    BlendFactor blend_src = BlendFactor::SrcAlpha;
    BlendFactor blend_dst = BlendFactor::OneMinusSrcAlpha;

    RenderState() = default;

    RenderState(
        PolygonMode polygon_mode_,
        bool cull_,
        bool depth_test_,
        bool depth_write_,
        bool blend_,
        BlendFactor blend_src_,
        BlendFactor blend_dst_
    ) : polygon_mode(polygon_mode_),
        cull(cull_),
        depth_test(depth_test_),
        depth_write(depth_write_),
        blend(blend_),
        blend_src(blend_src_),
        blend_dst(blend_dst_) {}

    static RenderState opaque() {
        return RenderState();
    }

    static RenderState transparent() {
        RenderState s;
        s.blend = true;
        s.depth_write = false;
        return s;
    }

    static RenderState wireframe() {
        RenderState s;
        s.polygon_mode = PolygonMode::Line;
        s.cull = false;
        return s;
    }

    bool operator==(const RenderState& other) const {
        return polygon_mode == other.polygon_mode &&
               cull == other.cull &&
               depth_test == other.depth_test &&
               depth_write == other.depth_write &&
               blend == other.blend &&
               blend_src == other.blend_src &&
               blend_dst == other.blend_dst;
    }

    bool operator!=(const RenderState& other) const {
        return !(*this == other);
    }
};

} // namespace termin
