#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include "tgfx2/tgfx2_api.h"

namespace tgfx {

struct LinePoint3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

struct LineVertex {
    LinePoint3 position;
};

enum class LineCapStyle {
    Butt,
    Square,
    Round,
};

enum class LineJoinStyle {
    Bevel,
    Round,
};

struct LineStyle {
    float width = 1.0f;
    LinePoint3 up_hint{0.0f, 1.0f, 0.0f};
    LineCapStyle cap = LineCapStyle::Round;
    LineJoinStyle join = LineJoinStyle::Round;
    int round_segments = 8;
    bool closed = false;
};

struct LineMesh {
    std::vector<LineVertex> vertices;
    std::vector<uint32_t> indices;

    bool empty() const { return vertices.empty() || indices.empty(); }
};

TGFX2_TYPE_API LineMesh build_line_mesh(std::span<const LinePoint3> points,
                                        const LineStyle& style);

} // namespace tgfx
