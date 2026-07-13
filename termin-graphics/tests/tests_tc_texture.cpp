#include "guard_main.h"

extern "C" {
#include "tgfx/resources/tc_texture.h"
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
