#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

#if defined(_WIN32) && defined(TERMIN_IMAGE_EXPORTS)
#define TERMIN_IMAGE_API __declspec(dllexport)
#elif defined(_WIN32)
#define TERMIN_IMAGE_API __declspec(dllimport)
#else
#define TERMIN_IMAGE_API __attribute__((visibility("default")))
#endif

namespace termin::image {

struct DecodedImage {
    int width = 0;
    int height = 0;
    int channels = 4;
    std::string format;
    std::vector<std::uint8_t> pixels;
};

TERMIN_IMAGE_API DecodedImage decode_rgba8(
    std::span<const std::uint8_t> bytes,
    const std::string& source_hint = {}
);

using PngWriteCallback = void (*)(void* context, std::span<const std::uint8_t> bytes);

TERMIN_IMAGE_API void encode_png_rgba8_to(
    std::span<const std::uint8_t> rgba,
    int width,
    int height,
    PngWriteCallback write,
    void* context
);

TERMIN_IMAGE_API std::vector<std::uint8_t> encode_png_rgba8(
    std::span<const std::uint8_t> rgba,
    int width,
    int height
);

} // namespace termin::image
