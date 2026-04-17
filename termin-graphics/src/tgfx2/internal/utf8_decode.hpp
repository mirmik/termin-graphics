// utf8_decode.hpp - Minimal UTF-8 → codepoint decoder for tgfx2 internals.
//
// Private to termin_graphics2; not installed, not part of the public
// API. Used by FontAtlas, Text2DRenderer, and Text3DRenderer to iterate
// UTF-8 strings character-by-character without pulling in ICU or <cwchar>.
//
// Decoder is liberal: on malformed input it emits U+FFFD (REPLACEMENT
// CHARACTER) and advances one byte so callers always make progress.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace tgfx {
namespace internal {

// Decode one codepoint starting at `i`. Advances `i` past the sequence.
// Returns 0 when `i` is already at/past end.
inline uint32_t utf8_decode(std::string_view s, size_t& i) {
    if (i >= s.size()) return 0;
    unsigned char b0 = static_cast<unsigned char>(s[i]);

    auto take_cont = [&](size_t off) -> int {
        if (i + off >= s.size()) return -1;
        unsigned char b = static_cast<unsigned char>(s[i + off]);
        if ((b & 0xC0) != 0x80) return -1;
        return b & 0x3F;
    };

    if (b0 < 0x80) {
        i += 1;
        return b0;
    }
    if ((b0 & 0xE0) == 0xC0) {
        int b1 = take_cont(1);
        if (b1 < 0) { i += 1; return 0xFFFD; }
        i += 2;
        return (static_cast<uint32_t>(b0 & 0x1F) << 6) |
               static_cast<uint32_t>(b1);
    }
    if ((b0 & 0xF0) == 0xE0) {
        int b1 = take_cont(1);
        int b2 = take_cont(2);
        if (b1 < 0 || b2 < 0) { i += 1; return 0xFFFD; }
        i += 3;
        return (static_cast<uint32_t>(b0 & 0x0F) << 12) |
               (static_cast<uint32_t>(b1) << 6) |
               static_cast<uint32_t>(b2);
    }
    if ((b0 & 0xF8) == 0xF0) {
        int b1 = take_cont(1);
        int b2 = take_cont(2);
        int b3 = take_cont(3);
        if (b1 < 0 || b2 < 0 || b3 < 0) { i += 1; return 0xFFFD; }
        i += 4;
        return (static_cast<uint32_t>(b0 & 0x07) << 18) |
               (static_cast<uint32_t>(b1) << 12) |
               (static_cast<uint32_t>(b2) << 6) |
               static_cast<uint32_t>(b3);
    }
    i += 1;
    return 0xFFFD;
}

}  // namespace internal
}  // namespace tgfx
