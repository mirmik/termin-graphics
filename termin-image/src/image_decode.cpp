#include "termin/image/image_decode.hpp"

#include <csetjmp>
#include <cstring>
#include <stdexcept>

#include <jpeglib.h>
#include <png.h>
#include <webp/decode.h>

namespace termin::image {
namespace {

bool has_png_signature(std::span<const std::uint8_t> bytes) {
    return bytes.size() >= 8 && png_sig_cmp(bytes.data(), 0, 8) == 0;
}

bool has_jpeg_signature(std::span<const std::uint8_t> bytes) {
    return bytes.size() >= 2 && bytes[0] == 0xFF && bytes[1] == 0xD8;
}

bool has_webp_signature(std::span<const std::uint8_t> bytes) {
    return bytes.size() >= 12
        && std::memcmp(bytes.data(), "RIFF", 4) == 0
        && std::memcmp(bytes.data() + 8, "WEBP", 4) == 0;
}

DecodedImage decode_png_rgba8(std::span<const std::uint8_t> bytes) {
    png_image image;
    std::memset(&image, 0, sizeof(image));
    image.version = PNG_IMAGE_VERSION;

    if (png_image_begin_read_from_memory(&image, bytes.data(), bytes.size()) == 0) {
        throw std::runtime_error(std::string("libpng failed to read image header: ") + image.message);
    }

    image.format = PNG_FORMAT_RGBA;
    DecodedImage result;
    result.width = static_cast<int>(image.width);
    result.height = static_cast<int>(image.height);
    result.channels = 4;
    result.format = "png";
    result.pixels.resize(PNG_IMAGE_SIZE(image));

    if (png_image_finish_read(&image, nullptr, result.pixels.data(), 0, nullptr) == 0) {
        std::string message = image.message;
        png_image_free(&image);
        throw std::runtime_error("libpng failed to decode image: " + message);
    }

    png_image_free(&image);
    return result;
}

struct JpegErrorManager {
    jpeg_error_mgr pub;
    jmp_buf jump;
    char message[JMSG_LENGTH_MAX];
};

void jpeg_error_exit(j_common_ptr cinfo) {
    auto* manager = reinterpret_cast<JpegErrorManager*>(cinfo->err);
    (*cinfo->err->format_message)(cinfo, manager->message);
    longjmp(manager->jump, 1);
}

DecodedImage decode_jpeg_rgba8(std::span<const std::uint8_t> bytes) {
    jpeg_decompress_struct cinfo;
    JpegErrorManager jerr;
    cinfo.err = jpeg_std_error(&jerr.pub);
    jerr.pub.error_exit = jpeg_error_exit;
    jerr.message[0] = '\0';

    if (setjmp(jerr.jump) != 0) {
        jpeg_destroy_decompress(&cinfo);
        throw std::runtime_error(std::string("libjpeg failed to decode image: ") + jerr.message);
    }

    jpeg_create_decompress(&cinfo);
    jpeg_mem_src(&cinfo, bytes.data(), static_cast<unsigned long>(bytes.size()));
    jpeg_read_header(&cinfo, TRUE);
    cinfo.out_color_space = JCS_RGB;
    jpeg_start_decompress(&cinfo);

    const int width = static_cast<int>(cinfo.output_width);
    const int height = static_cast<int>(cinfo.output_height);
    const int components = static_cast<int>(cinfo.output_components);
    if (width <= 0 || height <= 0 || components != 3) {
        jpeg_finish_decompress(&cinfo);
        jpeg_destroy_decompress(&cinfo);
        throw std::runtime_error("libjpeg produced unsupported output layout");
    }

    DecodedImage result;
    result.width = width;
    result.height = height;
    result.channels = 4;
    result.format = "jpeg";
    result.pixels.resize(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4);

    std::vector<std::uint8_t> row(static_cast<std::size_t>(width) * 3);
    JSAMPROW rows[1] = {row.data()};
    while (cinfo.output_scanline < cinfo.output_height) {
        const std::size_t y = cinfo.output_scanline;
        jpeg_read_scanlines(&cinfo, rows, 1);
        std::uint8_t* dst = result.pixels.data() + y * static_cast<std::size_t>(width) * 4;
        for (int x = 0; x < width; ++x) {
            dst[x * 4 + 0] = row[x * 3 + 0];
            dst[x * 4 + 1] = row[x * 3 + 1];
            dst[x * 4 + 2] = row[x * 3 + 2];
            dst[x * 4 + 3] = 255;
        }
    }

    jpeg_finish_decompress(&cinfo);
    jpeg_destroy_decompress(&cinfo);
    return result;
}

DecodedImage decode_webp_rgba8(std::span<const std::uint8_t> bytes) {
    int width = 0;
    int height = 0;
    if (WebPGetInfo(bytes.data(), bytes.size(), &width, &height) == 0 || width <= 0 || height <= 0) {
        throw std::runtime_error("libwebp failed to read image header");
    }

    DecodedImage result;
    result.width = width;
    result.height = height;
    result.channels = 4;
    result.format = "webp";
    result.pixels.resize(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4);

    std::uint8_t* decoded = WebPDecodeRGBAInto(
        bytes.data(),
        bytes.size(),
        result.pixels.data(),
        result.pixels.size(),
        width * 4
    );
    if (decoded == nullptr) {
        throw std::runtime_error("libwebp failed to decode image");
    }
    return result;
}

struct PngWriteState {
    std::vector<std::uint8_t> bytes;
};

void png_write_callback(png_structp png_ptr, png_bytep data, png_size_t length) {
    auto* state = static_cast<PngWriteState*>(png_get_io_ptr(png_ptr));
    const std::uint8_t* begin = reinterpret_cast<const std::uint8_t*>(data);
    state->bytes.insert(state->bytes.end(), begin, begin + length);
}

void png_flush_callback(png_structp) {}

} // namespace

DecodedImage decode_rgba8(std::span<const std::uint8_t> bytes, const std::string& source_hint) {
    if (bytes.empty()) {
        throw std::runtime_error("cannot decode empty image data");
    }

    try {
        if (has_png_signature(bytes)) {
            return decode_png_rgba8(bytes);
        }
        if (has_jpeg_signature(bytes)) {
            return decode_jpeg_rgba8(bytes);
        }
        if (has_webp_signature(bytes)) {
            return decode_webp_rgba8(bytes);
        }
    } catch (const std::exception& e) {
        if (!source_hint.empty()) {
            throw std::runtime_error(source_hint + ": " + e.what());
        }
        throw;
    }

    if (!source_hint.empty()) {
        throw std::runtime_error(source_hint + ": unsupported image format");
    }
    throw std::runtime_error("unsupported image format");
}

std::vector<std::uint8_t> encode_png_rgba8(std::span<const std::uint8_t> rgba, int width, int height) {
    if (width <= 0 || height <= 0) {
        throw std::runtime_error("PNG encode requires positive width and height");
    }
    const std::size_t expected = static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4;
    if (rgba.size() != expected) {
        throw std::runtime_error("PNG encode input size does not match RGBA8 dimensions");
    }

    png_structp png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
    if (png_ptr == nullptr) {
        throw std::runtime_error("libpng failed to create write struct");
    }
    png_infop info_ptr = png_create_info_struct(png_ptr);
    if (info_ptr == nullptr) {
        png_destroy_write_struct(&png_ptr, nullptr);
        throw std::runtime_error("libpng failed to create info struct");
    }

    if (setjmp(png_jmpbuf(png_ptr)) != 0) {
        png_destroy_write_struct(&png_ptr, &info_ptr);
        throw std::runtime_error("libpng failed to encode PNG");
    }

    PngWriteState state;
    png_set_write_fn(png_ptr, &state, png_write_callback, png_flush_callback);
    png_set_IHDR(
        png_ptr,
        info_ptr,
        static_cast<png_uint_32>(width),
        static_cast<png_uint_32>(height),
        8,
        PNG_COLOR_TYPE_RGBA,
        PNG_INTERLACE_NONE,
        PNG_COMPRESSION_TYPE_DEFAULT,
        PNG_FILTER_TYPE_DEFAULT
    );
    png_write_info(png_ptr, info_ptr);

    std::vector<png_bytep> rows(static_cast<std::size_t>(height));
    for (int y = 0; y < height; ++y) {
        rows[static_cast<std::size_t>(y)] = const_cast<png_bytep>(
            rgba.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(width) * 4
        );
    }
    png_write_image(png_ptr, rows.data());
    png_write_end(png_ptr, info_ptr);
    png_destroy_write_struct(&png_ptr, &info_ptr);
    return state.bytes;
}

} // namespace termin::image
