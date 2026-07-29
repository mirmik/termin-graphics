#include "tgfx2/tc_texture_upload.hpp"

#include "tgfx2/pixel_format_utils.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <limits>

extern "C" {
#include "tcbase/tc_log.h"
}

namespace tgfx {
namespace {

const char* texture_name(const tc_texture* texture) {
    if (!texture) return "<null>";
    return texture->header.name ? texture->header.name : texture->header.uuid;
}

float srgb_to_linear(float value) {
    return value <= 0.04045f
        ? value / 12.92f
        : std::pow((value + 0.055f) / 1.055f, 2.4f);
}

float linear_to_srgb(float value) {
    value = std::clamp(value, 0.0f, 1.0f);
    return value <= 0.0031308f
        ? value * 12.92f
        : 1.055f * std::pow(value, 1.0f / 2.4f) - 0.055f;
}

float half_to_float(uint16_t half) {
    const uint32_t sign = static_cast<uint32_t>(half & 0x8000u) << 16u;
    uint32_t exponent = (half >> 10u) & 0x1fu;
    uint32_t mantissa = half & 0x03ffu;
    uint32_t bits = 0;

    if (exponent == 0) {
        if (mantissa == 0) {
            bits = sign;
        } else {
            int shift = 0;
            while ((mantissa & 0x0400u) == 0) {
                mantissa <<= 1u;
                ++shift;
            }
            mantissa &= 0x03ffu;
            const uint32_t float_exponent =
                static_cast<uint32_t>(127 - 14 - shift);
            bits = sign | (float_exponent << 23u) | (mantissa << 13u);
        }
    } else if (exponent == 0x1fu) {
        bits = sign | 0x7f800000u | (mantissa << 13u);
    } else {
        exponent += 127u - 15u;
        bits = sign | (exponent << 23u) | (mantissa << 13u);
    }
    return std::bit_cast<float>(bits);
}

uint16_t float_to_half(float value) {
    const uint32_t bits = std::bit_cast<uint32_t>(value);
    const uint32_t sign = (bits >> 16u) & 0x8000u;
    const uint32_t exponent = (bits >> 23u) & 0xffu;
    uint32_t mantissa = bits & 0x007fffffu;

    if (exponent == 0xffu) {
        return static_cast<uint16_t>(
            sign | (mantissa ? 0x7e00u : 0x7c00u));
    }

    const int half_exponent = static_cast<int>(exponent) - 127 + 15;
    if (half_exponent >= 0x1f) {
        return static_cast<uint16_t>(sign | 0x7c00u);
    }
    if (half_exponent <= 0) {
        if (half_exponent < -10) {
            return static_cast<uint16_t>(sign);
        }
        mantissa |= 0x00800000u;
        const uint32_t shift = static_cast<uint32_t>(14 - half_exponent);
        uint32_t rounded = mantissa >> shift;
        const uint32_t remainder = mantissa & ((1u << shift) - 1u);
        const uint32_t halfway = 1u << (shift - 1u);
        if (remainder > halfway || (remainder == halfway && (rounded & 1u))) {
            ++rounded;
        }
        return static_cast<uint16_t>(sign | rounded);
    }

    uint32_t rounded_mantissa = mantissa >> 13u;
    const uint32_t remainder = mantissa & 0x1fffu;
    if (remainder > 0x1000u ||
        (remainder == 0x1000u && (rounded_mantissa & 1u))) {
        ++rounded_mantissa;
        if (rounded_mantissa == 0x0400u) {
            rounded_mantissa = 0;
            if (half_exponent + 1 >= 0x1f) {
                return static_cast<uint16_t>(sign | 0x7c00u);
            }
            return static_cast<uint16_t>(
                sign | (static_cast<uint32_t>(half_exponent + 1) << 10u));
        }
    }
    return static_cast<uint16_t>(
        sign |
        (static_cast<uint32_t>(half_exponent) << 10u) |
        rounded_mantissa);
}

uint32_t channel_count(PixelFormat format) {
    switch (format) {
    case PixelFormat::R8_UNorm:
    case PixelFormat::R16F:
    case PixelFormat::R32F:
        return 1;
    case PixelFormat::RG8_UNorm:
    case PixelFormat::RG16F:
    case PixelFormat::RG32F:
        return 2;
    case PixelFormat::RGB8_UNorm:
        return 3;
    case PixelFormat::RGBA8_UNorm:
    case PixelFormat::BGRA8_UNorm:
    case PixelFormat::RGBA8_sRGB:
    case PixelFormat::BGRA8_sRGB:
    case PixelFormat::RGBA16F:
    case PixelFormat::RGBA32F:
        return 4;
    default:
        return 0;
    }
}

bool is_u8_format(PixelFormat format) {
    switch (format) {
    case PixelFormat::R8_UNorm:
    case PixelFormat::RG8_UNorm:
    case PixelFormat::RGB8_UNorm:
    case PixelFormat::RGBA8_UNorm:
    case PixelFormat::BGRA8_UNorm:
    case PixelFormat::RGBA8_sRGB:
    case PixelFormat::BGRA8_sRGB:
        return true;
    default:
        return false;
    }
}

bool is_f16_format(PixelFormat format) {
    return format == PixelFormat::R16F ||
           format == PixelFormat::RG16F ||
           format == PixelFormat::RGBA16F;
}

bool is_f32_format(PixelFormat format) {
    return format == PixelFormat::R32F ||
           format == PixelFormat::RG32F ||
           format == PixelFormat::RGBA32F;
}

float read_channel(
    const uint8_t* pixel,
    PixelFormat format,
    uint32_t channel)
{
    if (is_u8_format(format)) {
        const float encoded = static_cast<float>(pixel[channel]) / 255.0f;
        if (is_srgb_format(format) && channel < 3u) {
            return srgb_to_linear(encoded);
        }
        return encoded;
    }
    if (is_f16_format(format)) {
        uint16_t value = 0;
        std::memcpy(&value, pixel + channel * sizeof(value), sizeof(value));
        return half_to_float(value);
    }
    float value = 0.0f;
    std::memcpy(&value, pixel + channel * sizeof(value), sizeof(value));
    return value;
}

void write_channel(
    uint8_t* pixel,
    PixelFormat format,
    uint32_t channel,
    float value)
{
    if (is_u8_format(format)) {
        if (is_srgb_format(format) && channel < 3u) {
            value = linear_to_srgb(value);
        }
        pixel[channel] = static_cast<uint8_t>(
            std::lround(std::clamp(value, 0.0f, 1.0f) * 255.0f));
        return;
    }
    if (is_f16_format(format)) {
        const uint16_t half = float_to_half(value);
        std::memcpy(pixel + channel * sizeof(half), &half, sizeof(half));
        return;
    }
    std::memcpy(pixel + channel * sizeof(value), &value, sizeof(value));
}

bool downsample_level(
    PixelFormat format,
    uint32_t source_width,
    uint32_t source_height,
    std::span<const uint8_t> source,
    std::vector<uint8_t>& destination)
{
    const uint32_t channels = channel_count(format);
    const uint32_t bytes_per_pixel = pixel_format_byte_size(format);
    if (channels == 0 || bytes_per_pixel == 0 ||
        (!is_u8_format(format) &&
         !is_f16_format(format) &&
         !is_f32_format(format))) {
        tc_log_error(
            "build_texture_mip_chain: format '%.*s' does not support CPU mip filtering",
            static_cast<int>(pixel_format_name(format).size()),
            pixel_format_name(format).data());
        return false;
    }

    const uint32_t destination_width = std::max(1u, source_width / 2u);
    const uint32_t destination_height = std::max(1u, source_height / 2u);
    destination.resize(
        static_cast<size_t>(destination_width) *
        destination_height *
        bytes_per_pixel);

    for (uint32_t y = 0; y < destination_height; ++y) {
        for (uint32_t x = 0; x < destination_width; ++x) {
            float sums[4] = {};
            uint32_t sample_count = 0;
            const uint32_t source_y_begin =
                y * source_height / destination_height;
            const uint32_t source_y_end =
                (y + 1u) * source_height / destination_height;
            const uint32_t source_x_begin =
                x * source_width / destination_width;
            const uint32_t source_x_end =
                (x + 1u) * source_width / destination_width;
            for (uint32_t sy = source_y_begin; sy < source_y_end; ++sy) {
                for (uint32_t sx = source_x_begin; sx < source_x_end; ++sx) {
                    const uint8_t* source_pixel =
                        source.data() +
                        (static_cast<size_t>(sy) * source_width + sx) *
                            bytes_per_pixel;
                    for (uint32_t channel = 0; channel < channels; ++channel) {
                        sums[channel] +=
                            read_channel(source_pixel, format, channel);
                    }
                    ++sample_count;
                }
            }
            uint8_t* destination_pixel =
                destination.data() +
                (static_cast<size_t>(y) * destination_width + x) *
                    bytes_per_pixel;
            for (uint32_t channel = 0; channel < channels; ++channel) {
                write_channel(
                    destination_pixel,
                    format,
                    channel,
                    sums[channel] / static_cast<float>(sample_count));
            }
        }
    }
    return true;
}

bool normalize_base_level(
    const tc_texture* texture,
    PixelFormat& out_format,
    std::vector<uint8_t>& out_pixels)
{
    if (!texture || !texture->data) return false;

    const auto source_format =
        static_cast<tc_texture_format>(texture->format);
    const auto encoding =
        static_cast<tc_texture_encoding>(texture->encoding);
    const size_t pixel_count =
        static_cast<size_t>(texture->width) * texture->height;
    const auto* source = static_cast<const uint8_t*>(texture->data);

    if (source_format == TC_TEXTURE_RGB8) {
        out_format =
            pixel_format_for_tc_texture(TC_TEXTURE_RGBA8, encoding);
        if (out_format == PixelFormat::Undefined) return false;
        out_pixels.resize(pixel_count * 4u);
        for (size_t i = 0; i < pixel_count; ++i) {
            std::memcpy(out_pixels.data() + i * 4u, source + i * 3u, 3u);
            out_pixels[i * 4u + 3u] = 0xffu;
        }
        return true;
    }

    if (source_format == TC_TEXTURE_RGB16F) {
        out_format =
            pixel_format_for_tc_texture(source_format, encoding);
        if (out_format == PixelFormat::Undefined) return false;
        out_pixels.resize(pixel_count * 8u);
        for (size_t i = 0; i < pixel_count; ++i) {
            std::memcpy(out_pixels.data() + i * 8u, source + i * 6u, 6u);
            out_pixels[i * 8u + 6u] = 0x00u;
            out_pixels[i * 8u + 7u] = 0x3cu;
        }
        return true;
    }

    out_format = pixel_format_for_tc_texture(source_format, encoding);
    const size_t bytes_per_pixel = tc_texture_format_bpp(source_format);
    if (out_format == PixelFormat::Undefined || bytes_per_pixel == 0) {
        return false;
    }
    out_pixels.assign(source, source + pixel_count * bytes_per_pixel);
    return true;
}

} // namespace

uint32_t texture_mip_level_count(uint32_t width, uint32_t height) {
    if (width == 0 || height == 0) return 0;
    uint32_t levels = 1;
    while (width > 1 || height > 1) {
        width = std::max(1u, width / 2u);
        height = std::max(1u, height / 2u);
        ++levels;
    }
    return levels;
}

bool build_texture_mip_chain(
    PixelFormat format,
    uint32_t width,
    uint32_t height,
    std::span<const uint8_t> base_level,
    std::vector<std::vector<uint8_t>>& out_levels)
{
    out_levels.clear();
    const uint32_t bytes_per_pixel = pixel_format_byte_size(format);
    if (width == 0 || height == 0 || bytes_per_pixel == 0) {
        tc_log_error("build_texture_mip_chain: invalid texture extent or format");
        return false;
    }
    if (static_cast<size_t>(width) >
        std::numeric_limits<size_t>::max() /
            static_cast<size_t>(height) / bytes_per_pixel) {
        tc_log_error("build_texture_mip_chain: texture byte size overflows size_t");
        return false;
    }
    const size_t expected_size =
        static_cast<size_t>(width) * height * bytes_per_pixel;
    if (base_level.size() != expected_size) {
        tc_log_error(
            "build_texture_mip_chain: base level has %zu bytes, expected %zu",
            base_level.size(),
            expected_size);
        return false;
    }

    out_levels.emplace_back(base_level.begin(), base_level.end());
    while (width > 1 || height > 1) {
        std::vector<uint8_t> next;
        if (!downsample_level(
                format,
                width,
                height,
                std::span<const uint8_t>(
                    out_levels.back().data(),
                    out_levels.back().size()),
                next)) {
            out_levels.clear();
            return false;
        }
        out_levels.push_back(std::move(next));
        width = std::max(1u, width / 2u);
        height = std::max(1u, height / 2u);
    }
    return true;
}

bool prepare_tc_texture_upload(
    const tc_texture* texture,
    TcTextureUpload& out_upload)
{
    out_upload = {};
    if (!texture || !texture->data ||
        texture->width == 0 || texture->height == 0) {
        tc_log_error(
            "prepare_tc_texture_upload: texture '%s' has no valid base level",
            texture_name(texture));
        return false;
    }

    std::vector<uint8_t> base_level;
    if (!normalize_base_level(texture, out_upload.format, base_level)) {
        tc_log_error(
            "prepare_tc_texture_upload: texture '%s' has unsupported format/encoding %u/%u",
            texture_name(texture),
            static_cast<unsigned>(texture->format),
            static_cast<unsigned>(texture->encoding));
        out_upload = {};
        return false;
    }

    if (!texture->mipmap) {
        out_upload.levels.push_back(std::move(base_level));
        return true;
    }
    if (!build_texture_mip_chain(
            out_upload.format,
            texture->width,
            texture->height,
            std::span<const uint8_t>(base_level.data(), base_level.size()),
            out_upload.levels)) {
        tc_log_error(
            "prepare_tc_texture_upload: failed to build mip chain for texture '%s'",
            texture_name(texture));
        out_upload = {};
        return false;
    }
    return true;
}

} // namespace tgfx
