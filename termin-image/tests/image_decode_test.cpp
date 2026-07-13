#include <array>
#include <cstdint>
#include <new>
#include <stdexcept>
#include <vector>

#include <termin/image/image_decode.hpp>

#include "guard_main.h"

namespace {

bool decode_throws(std::span<const std::uint8_t> bytes) {
    try {
        (void)termin::image::decode_rgba8(bytes, "malformed-image");
    } catch (const std::runtime_error&) {
        return true;
    }
    return false;
}

void fail_png_write(void*, std::span<const std::uint8_t>) {
    throw std::bad_alloc();
}

} // namespace

TEST_CASE("image codecs reject truncated JPEG without escaping through C++ frames") {
    constexpr std::array<std::uint8_t, 4> truncated_jpeg = {0xFF, 0xD8, 0xFF, 0xDB};

    CHECK(decode_throws(truncated_jpeg));
}

TEST_CASE("image codecs reject truncated PNG") {
    constexpr std::array<std::uint8_t, 12> truncated_png = {
        0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A,
        0x00, 0x00, 0x00, 0x00,
    };

    CHECK(decode_throws(truncated_png));
}

TEST_CASE("PNG encoder output is decoded as RGBA8") {
    const std::vector<std::uint8_t> pixels = {
        0x10, 0x20, 0x30, 0x40,
        0x50, 0x60, 0x70, 0x80,
    };

    const std::vector<std::uint8_t> encoded = termin::image::encode_png_rgba8(pixels, 2, 1);
    const termin::image::DecodedImage decoded = termin::image::decode_rgba8(encoded, "round-trip");

    CHECK(decoded.width == 2);
    CHECK(decoded.height == 1);
    CHECK(decoded.channels == 4);
    CHECK(decoded.format == "png");
    CHECK(decoded.pixels == pixels);
}

TEST_CASE("PNG write callback reports allocation failure without crossing C frames") {
    const std::vector<std::uint8_t> pixels = {
        0x10, 0x20, 0x30, 0x40,
        0x50, 0x60, 0x70, 0x80,
    };

    bool caught_bad_alloc = false;
    try {
        termin::image::encode_png_rgba8_to(pixels, 2, 1, fail_png_write, nullptr);
    } catch (const std::bad_alloc&) {
        caught_bad_alloc = true;
    }

    CHECK(caught_bad_alloc);
}

GUARD_TEST_MAIN();
