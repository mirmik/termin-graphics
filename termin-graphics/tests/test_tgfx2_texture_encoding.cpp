#include "guard_main.h"

#include "tgfx2/pixel_format_utils.hpp"
#include "tgfx2/tc_texture_upload.hpp"

#include <cmath>
#include <cstring>

#ifdef TGFX2_HAS_OPENGL
#include "tgfx2/opengl/opengl_type_conversions.hpp"
#endif

#ifdef TGFX2_HAS_VULKAN
#include "tgfx2/vulkan/vulkan_type_conversions.hpp"
#endif

#ifdef TGFX2_HAS_D3D11
#include "tgfx2/d3d11/d3d11_type_conversions.hpp"
#endif

TEST_CASE("pixel format encoding selection is explicit and portable") {
    using tgfx::PixelFormat;
    using tgfx::TextureEncoding;

    CHECK(tgfx::pixel_format_for_encoding(PixelFormat::RGBA8_UNorm, TextureEncoding::Linear) ==
          PixelFormat::RGBA8_UNorm);
    CHECK(tgfx::pixel_format_for_encoding(PixelFormat::RGBA8_UNorm, TextureEncoding::SRGB) == PixelFormat::RGBA8_sRGB);
    CHECK(tgfx::pixel_format_for_encoding(PixelFormat::BGRA8_UNorm, TextureEncoding::SRGB) == PixelFormat::BGRA8_sRGB);
    CHECK(tgfx::pixel_format_for_encoding(PixelFormat::RGBA16F, TextureEncoding::SRGB) == PixelFormat::Undefined);

    CHECK(tgfx::is_srgb_format(PixelFormat::RGBA8_sRGB));
    CHECK(tgfx::is_srgb_format(PixelFormat::BGRA8_sRGB));
    CHECK_FALSE(tgfx::is_srgb_format(PixelFormat::RGBA8_UNorm));
    CHECK(tgfx::is_rgba8_family(PixelFormat::RGBA8_sRGB));
    CHECK_EQ(tgfx::pixel_format_byte_size(PixelFormat::RGBA8_sRGB), 4u);
    CHECK_EQ(tgfx::pixel_format_byte_size(PixelFormat::BGRA8_sRGB), 4u);
}

TEST_CASE("sRGB pixel format names round trip") {
    using tgfx::PixelFormat;

    CHECK_EQ(tgfx::pixel_format_name(PixelFormat::RGBA8_sRGB), "rgba8_srgb");
    CHECK_EQ(tgfx::pixel_format_name(PixelFormat::BGRA8_sRGB), "bgra8_srgb");
    CHECK(tgfx::pixel_format_from_name("rgba8_srgb") == PixelFormat::RGBA8_sRGB);
    CHECK(tgfx::pixel_format_from_name("bgra8_srgb") == PixelFormat::BGRA8_sRGB);
}

TEST_CASE("tc texture storage and encoding select one pixel format") {
    using tgfx::PixelFormat;

    CHECK(tgfx::pixel_format_for_tc_texture(TC_TEXTURE_RGBA8, TC_TEXTURE_ENCODING_LINEAR) == PixelFormat::RGBA8_UNorm);
    CHECK(tgfx::pixel_format_for_tc_texture(TC_TEXTURE_RGBA8, TC_TEXTURE_ENCODING_SRGB) == PixelFormat::RGBA8_sRGB);
    CHECK(tgfx::pixel_format_for_tc_texture(TC_TEXTURE_RGBA16F, TC_TEXTURE_ENCODING_SRGB) == PixelFormat::Undefined);
    CHECK(tgfx::pixel_format_for_tc_texture(TC_TEXTURE_RGBA8, static_cast<tc_texture_encoding>(255)) ==
          PixelFormat::Undefined);
}

TEST_CASE("native format mappings preserve sRGB storage") {
    using tgfx::PixelFormat;

#ifdef TGFX2_HAS_OPENGL
    CHECK_EQ(tgfx::gl::to_gl_format(PixelFormat::RGBA8_sRGB).internal_format, static_cast<GLenum>(GL_SRGB8_ALPHA8));
    CHECK_EQ(tgfx::gl::to_gl_format(PixelFormat::BGRA8_sRGB).format, static_cast<GLenum>(GL_BGRA));
#endif

#ifdef TGFX2_HAS_VULKAN
    CHECK_EQ(tgfx::vk::to_vk_format(PixelFormat::RGBA8_sRGB), VK_FORMAT_R8G8B8A8_SRGB);
    CHECK_EQ(tgfx::vk::to_vk_format(PixelFormat::BGRA8_sRGB), VK_FORMAT_B8G8R8A8_SRGB);
#endif

#ifdef TGFX2_HAS_D3D11
    CHECK_EQ(tgfx::d3d11::to_dxgi_format(PixelFormat::RGBA8_sRGB), DXGI_FORMAT_R8G8B8A8_UNORM_SRGB);
    CHECK_EQ(tgfx::d3d11::to_dxgi_format(PixelFormat::BGRA8_sRGB), DXGI_FORMAT_B8G8R8A8_UNORM_SRGB);
#endif
}

TEST_CASE("sRGB reference transfer preserves alpha as linear data") {
    const float encoded = 128.0f / 255.0f;
    const float linear_rgb = encoded <= 0.04045f ? encoded / 12.92f : std::pow((encoded + 0.055f) / 1.055f, 2.4f);

    CHECK(std::abs(linear_rgb - 0.21586f) < 0.0001f);
    CHECK(std::abs(encoded - 0.50196f) < 0.0001f);
}

TEST_CASE("sRGB mip filtering averages color in linear light and alpha numerically") {
    using tgfx::PixelFormat;

    const std::vector<uint8_t> base = {
        0,
        0,
        0,
        0,
        255,
        255,
        255,
        255,
        0,
        0,
        0,
        0,
        255,
        255,
        255,
        255,
    };
    std::vector<std::vector<uint8_t>> levels;

    REQUIRE(tgfx::build_texture_mip_chain(PixelFormat::RGBA8_sRGB, 2, 2, base, levels));
    REQUIRE_EQ(levels.size(), 2u);
    REQUIRE_EQ(levels[1].size(), 4u);
    CHECK_EQ(levels[1][0], 188u);
    CHECK_EQ(levels[1][1], 188u);
    CHECK_EQ(levels[1][2], 188u);
    CHECK_EQ(levels[1][3], 128u);
}

TEST_CASE("linear mip filtering averages every channel numerically") {
    using tgfx::PixelFormat;

    const std::vector<uint8_t> base = {
        0,
        0,
        0,
        0,
        255,
        255,
        255,
        255,
        0,
        0,
        0,
        0,
        255,
        255,
        255,
        255,
    };
    std::vector<std::vector<uint8_t>> levels;

    REQUIRE(tgfx::build_texture_mip_chain(PixelFormat::RGBA8_UNorm, 2, 2, base, levels));
    REQUIRE_EQ(levels.size(), 2u);
    REQUIRE_EQ(levels[1].size(), 4u);
    CHECK_EQ(levels[1][0], 128u);
    CHECK_EQ(levels[1][1], 128u);
    CHECK_EQ(levels[1][2], 128u);
    CHECK_EQ(levels[1][3], 128u);
}

TEST_CASE("floating-point mip filtering preserves numeric range") {
    using tgfx::PixelFormat;

    const float source_values[] = {1.0f, 2.0f, 3.0f, 4.0f};
    std::vector<uint8_t> base(sizeof(source_values));
    std::memcpy(base.data(), source_values, sizeof(source_values));
    std::vector<std::vector<uint8_t>> levels;

    REQUIRE(tgfx::build_texture_mip_chain(PixelFormat::R32F, 2, 2, base, levels));
    REQUIRE_EQ(levels.size(), 2u);
    REQUIRE_EQ(levels[1].size(), sizeof(float));
    float result = 0.0f;
    std::memcpy(&result, levels[1].data(), sizeof(result));
    CHECK(std::abs(result - 2.5f) < 0.0001f);
}

TEST_CASE("odd mip extents include every source texel") {
    using tgfx::PixelFormat;

    const std::vector<uint8_t> base = {0, 0, 255};
    std::vector<std::vector<uint8_t>> levels;

    REQUIRE(tgfx::build_texture_mip_chain(PixelFormat::R8_UNorm, 3, 1, base, levels));
    REQUIRE_EQ(levels.size(), 2u);
    REQUIRE_EQ(levels[1].size(), 1u);
    CHECK_EQ(levels[1][0], 85u);
}

TEST_CASE("tc texture upload policy produces a complete normalized mip chain") {
    std::vector<uint8_t> rgb(4u * 2u * 3u, 64u);
    tc_texture texture{};
    texture.width = 4;
    texture.height = 2;
    texture.format = TC_TEXTURE_RGB8;
    texture.encoding = TC_TEXTURE_ENCODING_SRGB;
    texture.mipmap = 1;
    texture.data = rgb.data();

    tgfx::TcTextureUpload upload;
    REQUIRE(tgfx::prepare_tc_texture_upload(&texture, upload));
    CHECK(upload.format == tgfx::PixelFormat::RGBA8_sRGB);
    REQUIRE_EQ(upload.levels.size(), 3u);
    CHECK_EQ(upload.levels[0].size(), 4u * 2u * 4u);
    CHECK_EQ(upload.levels[1].size(), 2u * 1u * 4u);
    CHECK_EQ(upload.levels[2].size(), 1u * 1u * 4u);
    CHECK_EQ(upload.levels[0][3], 255u);
    CHECK_EQ(upload.levels[1][3], 255u);
    CHECK_EQ(upload.levels[2][3], 255u);
}
