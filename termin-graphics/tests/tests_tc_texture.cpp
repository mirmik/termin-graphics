#include "guard_main.h"
#include "tgfx/tgfx_texture_handle.hpp"

#include <cstring>
#include <string>

extern "C" {
#include "tcbase/tc_log.h"
#include "tgfx/resources/tc_material.h"
#include "tgfx/resources/tc_texture.h"
#include "tgfx/resources/tc_texture_registry.h"
}

namespace {

    tc_log_level g_encoding_log_level = TC_LOG_DEBUG;
    std::string g_encoding_log_message;

    void capture_encoding_log(tc_log_level level, const char* message) {
        if (message && std::strstr(message, "expects sRGB")) {
            g_encoding_log_level = level;
            g_encoding_log_message = message;
        }
    }

} // namespace

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

TEST_CASE("tc_texture encoding changes are validated and versioned") {
    tc_texture_init();
    const tc_texture_handle handle = tc_texture_create("texture-encoding-version");
    tc_texture* texture = tc_texture_get(handle);
    REQUIRE(texture != nullptr);
    CHECK_EQ(texture->encoding, TC_TEXTURE_ENCODING_LINEAR);

    const uint32_t initial_version = texture->header.version;
    CHECK(tc_texture_set_encoding(texture, TC_TEXTURE_ENCODING_SRGB));
    CHECK_EQ(texture->encoding, TC_TEXTURE_ENCODING_SRGB);
    CHECK_EQ(texture->header.version, initial_version + 1u);

    CHECK(tc_texture_set_encoding(texture, TC_TEXTURE_ENCODING_SRGB));
    CHECK_EQ(texture->header.version, initial_version + 1u);

    CHECK_FALSE(tc_texture_set_encoding(texture, static_cast<tc_texture_encoding>(255)));
    CHECK_EQ(texture->encoding, TC_TEXTURE_ENCODING_SRGB);
    CHECK_EQ(texture->header.version, initial_version + 1u);
    tc_texture_shutdown();
}

TEST_CASE("content texture identity includes transfer encoding") {
    tc_texture_init();
    const uint8_t pixel[4] = {128, 128, 128, 128};

    termin::TcTextureCreateInfo linear_info;
    linear_info.pixels = {pixel, 1, 1, 4};
    linear_info.name = "linear";
    linear_info.encoding = tgfx::TextureEncoding::Linear;
    termin::TcTexture linear = termin::TcTexture::from_data(linear_info);

    termin::TcTextureCreateInfo srgb_info;
    srgb_info.pixels = {pixel, 1, 1, 4};
    srgb_info.name = "srgb";
    srgb_info.encoding = tgfx::TextureEncoding::SRGB;
    termin::TcTexture srgb = termin::TcTexture::from_data(srgb_info);

    REQUIRE(linear.is_valid());
    REQUIRE(srgb.is_valid());
    CHECK_FALSE(tc_texture_handle_eq(linear.handle, srgb.handle));
    CHECK(std::string(linear.uuid()) != std::string(srgb.uuid()));
    CHECK(linear.encoding() == tgfx::TextureEncoding::Linear);
    CHECK(srgb.encoding() == tgfx::TextureEncoding::SRGB);
    tc_texture_shutdown();
}

TEST_CASE("tc_texture registry owns canonical default textures") {
    tc_texture_init();

    const tc_texture_handle white = tc_texture_get_white_1x1();
    const tc_texture_handle white_again = tc_texture_get_white_1x1();
    const tc_texture_handle white_srgb = tc_texture_get_white_1x1_srgb();
    const tc_texture_handle white_srgb_again = tc_texture_get_white_1x1_srgb();
    const tc_texture_handle normal = tc_texture_get_normal_1x1();
    const tc_texture_handle normal_again = tc_texture_get_normal_1x1();

    REQUIRE(tc_texture_is_valid(white));
    REQUIRE(tc_texture_is_valid(white_srgb));
    REQUIRE(tc_texture_is_valid(normal));
    CHECK(tc_texture_handle_eq(white, white_again));
    CHECK(tc_texture_handle_eq(white_srgb, white_srgb_again));
    CHECK(tc_texture_handle_eq(normal, normal_again));
    CHECK_FALSE(tc_texture_handle_eq(white, normal));
    CHECK_FALSE(tc_texture_handle_eq(white, white_srgb));
    CHECK_EQ(tc_texture_get(white)->encoding, TC_TEXTURE_ENCODING_LINEAR);
    CHECK_EQ(tc_texture_get(white_srgb)->encoding, TC_TEXTURE_ENCODING_SRGB);

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
    CHECK_FALSE(tc_texture_is_valid(white_srgb));
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

TEST_CASE("material texture slots warn and bind encoding mismatches") {
    tc_texture_init();
    const tc_texture_handle linear = tc_texture_get_white_1x1();
    const tc_texture_handle srgb = tc_texture_get_white_1x1_srgb();

    tc_material_phase phase{};
    REQUIRE(tc_material_phase_declare_texture(&phase, "albedo", TC_TEXTURE_ENCODING_SRGB));
    CHECK_EQ(phase.texture_count, 1u);
    CHECK(phase.textures[0].is_declared != 0);
    CHECK(phase.textures[0].has_expected_encoding != 0);
    CHECK_EQ(phase.textures[0].expected_encoding, TC_TEXTURE_ENCODING_SRGB);

    REQUIRE(tc_material_phase_set_texture(&phase, "albedo", srgb));
    const tc_texture_handle previous = phase.textures[0].texture;
    g_encoding_log_level = TC_LOG_DEBUG;
    g_encoding_log_message.clear();
    tc_log_set_callback(capture_encoding_log);
    CHECK(tc_material_phase_set_texture(&phase, "albedo", linear));
    tc_log_set_callback(nullptr);
    CHECK_FALSE(tc_texture_handle_eq(phase.textures[0].texture, previous));
    CHECK(tc_texture_handle_eq(phase.textures[0].texture, linear));
    CHECK_EQ(g_encoding_log_level, TC_LOG_WARN);
    CHECK(g_encoding_log_message.find("binding it unchanged") != std::string::npos);

    tc_material_phase unconstrained{};
    REQUIRE(tc_material_phase_declare_texture_slot(&unconstrained, "input"));
    CHECK(unconstrained.textures[0].is_declared != 0);
    CHECK(unconstrained.textures[0].has_expected_encoding == 0);
    CHECK(tc_material_phase_set_texture(&unconstrained, "input", linear));
    CHECK(tc_material_phase_set_texture(&unconstrained, "input", srgb));

    tc_material_phase unchecked{};
    CHECK(tc_material_phase_set_texture(&unchecked, "manual", linear));
    CHECK(tc_material_phase_set_texture(&unchecked, "manual", srgb));
    tc_texture_shutdown();
}

TEST_CASE("material texture sources remain symbolic until an ordinary texture replaces them") {
    tc_texture_init();
    tc_material material{};
    material.phase_count = 1;
    REQUIRE(tc_material_phase_declare_texture_slot(&material.phases[0], "u_input"));

    REQUIRE(tc_material_set_texture_source(
        &material, "u_input", "render_target", "Panel Texture", "color"));
    REQUIRE_EQ(material.texture_source_count, 1u);
    const tc_material_texture_source* source = tc_material_find_texture_source(&material, "u_input");
    REQUIRE(source != nullptr);
    CHECK_EQ(std::string(source->kind), "render_target");
    CHECK_EQ(std::string(source->source_name), "Panel Texture");
    CHECK_EQ(std::string(source->channel), "color");

    REQUIRE_EQ(tc_material_set_texture(&material, "u_input", tc_texture_get_white_1x1()), 1u);
    CHECK_EQ(material.texture_source_count, 0u);
    CHECK(tc_material_find_texture_source(&material, "u_input") == nullptr);
    tc_texture_shutdown();
}
