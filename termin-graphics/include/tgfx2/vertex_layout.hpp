#pragma once

#include <cstdint>
#include <vector>

namespace tgfx2 {

enum class VertexFormat {
    // Floating point (glVertexAttribPointer)
    Float,    // 1x float
    Float2,   // 2x float
    Float3,   // 3x float
    Float4,   // 4x float

    // 32-bit integer (glVertexAttribIPointer)
    Int,      // 1x int32
    Int2,     // 2x int32
    Int3,     // 3x int32
    Int4,     // 4x int32
    UInt,     // 1x uint32
    UInt2,    // 2x uint32
    UInt3,    // 3x uint32
    UInt4,    // 4x uint32

    // 16-bit integer (glVertexAttribIPointer)
    Short,    // 1x int16
    Short2,   // 2x int16
    Short3,   // 3x int16
    Short4,   // 4x int16
    UShort,   // 1x uint16
    UShort2,  // 2x uint16
    UShort3,  // 3x uint16
    UShort4,  // 4x uint16

    // 8-bit integer (glVertexAttribIPointer)
    Byte4,    // 4x int8 (raw integer input to shader)
    UByte4,   // 4x uint8 raw (e.g. integer joint indices)
    UByte4N,  // 4x uint8 normalized (e.g. vertex color)
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
