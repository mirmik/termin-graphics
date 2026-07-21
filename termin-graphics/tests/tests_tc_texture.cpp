#include "guard_main.h"

extern "C" {
#include "tgfx/resources/tc_texture.h"
#include "tgfx/resources/tc_texture_registry.h"
}

TEST_CASE("tc_texture formats report canonical byte sizes") {
    struct FormatCase {
        tc_texture_format format;
        size_t bytes_per_pixel;
        uint8_t channels;
    };
    constexpr FormatCase formats[] = {
        {TC_TEXTURE_RGBA8, 4, 4},
        {TC_TEXTURE_RGB8, 3, 3},
        {TC_TEXTURE_RG8, 2, 2},
        {TC_TEXTURE_R8, 1, 1},
        {TC_TEXTURE_RGBA16F, 8, 4},
        {TC_TEXTURE_RGB16F, 6, 3},
        {TC_TEXTURE_DEPTH24, 4, 1},
        {TC_TEXTURE_DEPTH32F, 4, 1},
        {TC_TEXTURE_R16F, 2, 1},
        {TC_TEXTURE_R32F, 4, 1},
    };

    for (const FormatCase& item : formats) {
        tc_texture texture{};
        texture.width = 3;
        texture.height = 5;
        texture.channels = 1; // The format, not this legacy field, owns byte size.
        texture.format = static_cast<uint8_t>(item.format);

        CHECK_EQ(tc_texture_format_bpp(item.format), item.bytes_per_pixel);
        CHECK_EQ(tc_texture_format_channels(item.format), item.channels);
        CHECK_EQ(tc_texture_data_size(&texture), 15u * item.bytes_per_pixel);
    }
}

TEST_CASE("tc_texture rejects unknown formats from byte-size calculations") {
    tc_texture texture{};
    texture.width = 4;
    texture.height = 4;
    texture.format = 255;

    CHECK_EQ(tc_texture_format_bpp(static_cast<tc_texture_format>(texture.format)), 0u);
    CHECK_EQ(tc_texture_format_channels(static_cast<tc_texture_format>(texture.format)), 0u);
    CHECK_EQ(tc_texture_data_size(&texture), 0u);
    CHECK_EQ(tc_texture_data_size(nullptr), 0u);
}

TEST_CASE("tc_texture registry owns canonical default textures") {
    tc_texture_init();

    const tc_texture_handle white = tc_texture_get_white_1x1();
    const tc_texture_handle white_again = tc_texture_get_white_1x1();
    const tc_texture_handle normal = tc_texture_get_normal_1x1();
    const tc_texture_handle normal_again = tc_texture_get_normal_1x1();

    REQUIRE(tc_texture_is_valid(white));
    REQUIRE(tc_texture_is_valid(normal));
    CHECK(tc_texture_handle_eq(white, white_again));
    CHECK(tc_texture_handle_eq(normal, normal_again));
    CHECK_FALSE(tc_texture_handle_eq(white, normal));

    const tc_texture* white_texture = tc_texture_get(white);
    const tc_texture* normal_texture = tc_texture_get(normal);
    REQUIRE(white_texture != nullptr);
    REQUIRE(normal_texture != nullptr);
    REQUIRE(white_texture->data != nullptr);
    REQUIRE(normal_texture->data != nullptr);

    const auto* white_pixel = static_cast<const uint8_t*>(white_texture->data);
    const auto* normal_pixel = static_cast<const uint8_t*>(normal_texture->data);
    CHECK_EQ(white_pixel[0], 255);
    CHECK_EQ(white_pixel[1], 255);
    CHECK_EQ(white_pixel[2], 255);
    CHECK_EQ(white_pixel[3], 255);
    CHECK_EQ(normal_pixel[0], 128);
    CHECK_EQ(normal_pixel[1], 128);
    CHECK_EQ(normal_pixel[2], 255);
    CHECK_EQ(normal_pixel[3], 255);

    tc_texture_shutdown();
    CHECK_FALSE(tc_texture_is_valid(white));
    CHECK_FALSE(tc_texture_is_valid(normal));

    tc_texture_init();
    const tc_texture_handle next_white = tc_texture_get_white_1x1();
    const tc_texture_handle next_normal = tc_texture_get_normal_1x1();
    REQUIRE(tc_texture_is_valid(next_white));
    REQUIRE(tc_texture_is_valid(next_normal));
    CHECK_FALSE(tc_texture_handle_eq(white, next_white));
    CHECK_FALSE(tc_texture_handle_eq(normal, next_normal));
    CHECK_FALSE(tc_texture_is_valid(white));
    CHECK_FALSE(tc_texture_is_valid(normal));
    tc_texture_shutdown();
}
