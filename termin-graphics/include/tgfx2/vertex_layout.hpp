#pragma once

#include <cstdint>
#include <vector>

namespace tgfx2 {

enum class VertexFormat {
    Float,    // 1x float
    Float2,   // 2x float
    Float3,   // 3x float
    Float4,   // 4x float
    UByte4,   // 4x uint8 (e.g. color)
    UByte4N,  // 4x uint8 normalized
};

struct VertexAttribute {
    uint32_t location = 0;
    VertexFormat format = VertexFormat::Float3;
    uint32_t offset = 0;
};

struct VertexBufferLayout {
    uint32_t stride = 0;
    std::vector<VertexAttribute> attributes;
    bool per_instance = false;
};

} // namespace tgfx2
