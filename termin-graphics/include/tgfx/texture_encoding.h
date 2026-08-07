// texture_encoding.h - Backend-neutral texture transfer encoding contract.
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum tc_texture_encoding {
    // Zero is intentionally Linear: zero-initialized procedural and render
    // target textures keep the historical no-transfer behavior until their
    // asset contracts opt into sRGB explicitly.
    TC_TEXTURE_ENCODING_LINEAR = 0,
    TC_TEXTURE_ENCODING_SRGB = 1,
} tc_texture_encoding;

#ifdef __cplusplus
}

namespace tgfx {

    enum class TextureEncoding : uint8_t {
        Linear = TC_TEXTURE_ENCODING_LINEAR,
        SRGB = TC_TEXTURE_ENCODING_SRGB,
    };

    constexpr tc_texture_encoding to_tc_texture_encoding(TextureEncoding encoding) {
        return static_cast<tc_texture_encoding>(static_cast<uint8_t>(encoding));
    }

    constexpr TextureEncoding from_tc_texture_encoding(tc_texture_encoding encoding) {
        return static_cast<TextureEncoding>(static_cast<uint8_t>(encoding));
    }

} // namespace tgfx
#endif
