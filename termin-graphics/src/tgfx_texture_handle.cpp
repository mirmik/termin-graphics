#include <tgfx/tgfx_texture_handle.hpp>
#include <tcbase/tc_log.hpp>
#include <algorithm>
#include <cstdlib>
#include <cstring>

namespace termin {

TcTexture TcTexture::from_data(const TcTextureCreateInfo& info) {
    const TexturePixelDataView& pixels = info.pixels;

    // Compute UUID from content if not provided
    char uuid_buf[40];
    const char* final_uuid = nullptr;

    if (!info.uuid_hint.empty()) {
        final_uuid = info.uuid_hint.c_str();
    } else {
        // Compute content-based UUID
        tc_texture_compute_uuid(
            pixels.data,
            pixels.byte_size(),
            pixels.width,
            pixels.height,
            pixels.channels,
            uuid_buf
        );
        final_uuid = uuid_buf;
    }

    // Check if texture already exists
    tc_texture_handle h = tc_texture_find(final_uuid);
    if (!tc_texture_handle_is_invalid(h)) {
        tc_texture* tex = tc_texture_get(h);
        if (tex) {
            if (!tc_texture_set_data(
                tex,
                pixels.data,
                pixels.width, pixels.height, pixels.channels,
                info.name.empty() ? nullptr : info.name.c_str(),
                info.source_path.empty() ? nullptr : info.source_path.c_str()
            )) {
                tc::Log::error("TcTexture::from_data: failed to set data on declared texture");
                return TcTexture();
            }
            tc_texture_set_transforms(
                tex,
                info.transform.flip_x,
                info.transform.flip_y,
                info.transform.transpose
            );
        }
        return TcTexture(h);
    }

    // Create new texture
    h = tc_texture_create(final_uuid);
    tc_texture* tex = tc_texture_get(h);
    if (!tex) {
        tc::Log::error("TcTexture::from_data: failed to add texture");
        return TcTexture();
    }

    // Set data
    if (!tc_texture_set_data(
        tex,
        pixels.data,
        pixels.width, pixels.height, pixels.channels,
        info.name.empty() ? nullptr : info.name.c_str(),
        info.source_path.empty() ? nullptr : info.source_path.c_str()
    )) {
        tc::Log::error("TcTexture::from_data: failed to set data");
        tc_texture_destroy(h);
        return TcTexture();
    }

    // Set transforms
    tc_texture_set_transforms(
        tex,
        info.transform.flip_x,
        info.transform.flip_y,
        info.transform.transpose
    );

    return TcTexture(h);
}

TcTexture TcTexture::white_1x1() {
    return TcTexture(tc_texture_get_white_1x1());
}

TcTexture TcTexture::normal_1x1() {
    return TcTexture(tc_texture_get_normal_1x1());
}

TcTexture TcTexture::dummy_shadow_1x1() {
    static const char* SHADOW_UUID = "__dummy_shadow_1x1__";

    // Check if already exists
    tc_texture_handle h = tc_texture_find(SHADOW_UUID);
    if (!tc_texture_handle_is_invalid(h)) {
        return TcTexture(h);
    }

    // Create new texture
    h = tc_texture_create(SHADOW_UUID);
    tc_texture* tex = tc_texture_get(h);
    if (!tex) {
        tc::Log::error("TcTexture::dummy_shadow_1x1: failed to create texture");
        return TcTexture();
    }

    // 1x1 depth texture with value 1.0 (max depth = fully lit, no shadow)
    float depth_value = 1.0f;

    // Allocate and copy data
    tex->data = malloc(sizeof(float));
    if (!tex->data) {
        tc::Log::error("TcTexture::dummy_shadow_1x1: failed to allocate data");
        tc_texture_destroy(h);
        return TcTexture();
    }
    std::memcpy(tex->data, &depth_value, sizeof(float));

    tex->width = 1;
    tex->height = 1;
    tex->channels = 1;
    tex->format = TC_TEXTURE_DEPTH24;
    tex->compare_mode = 1;  // Enable depth comparison for sampler2DShadow
    tex->clamp = 1;
    tex->mipmap = 0;
    tex->header.version = 1;

    if (tex->header.name) {
        free((void*)tex->header.name);
    }
    tex->header.name = strdup("__dummy_shadow_1x1__");

    return TcTexture(h);
}

std::tuple<std::vector<uint8_t>, uint32_t, uint32_t> TcTexture::get_upload_data() const {
    tc_texture* tex = get();
    if (!tex || !tex->data) {
        return {{}, 0, 0};
    }

    uint32_t w = tex->width;
    uint32_t h = tex->height;
    uint8_t ch = tex->channels;
    size_t size = (size_t)w * h * ch;

    std::vector<uint8_t> result(size);
    std::memcpy(result.data(), tex->data, size);

    // Transpose: swap width and height, rearrange pixels
    if (tex->transpose) {
        std::vector<uint8_t> transposed(size);
        for (uint32_t y = 0; y < h; ++y) {
            for (uint32_t x = 0; x < w; ++x) {
                for (uint8_t c = 0; c < ch; ++c) {
                    size_t src_idx = (y * w + x) * ch + c;
                    size_t dst_idx = (x * h + y) * ch + c;
                    transposed[dst_idx] = result[src_idx];
                }
            }
        }
        result = std::move(transposed);
        std::swap(w, h);
    }

    // Flip X: mirror horizontally
    if (tex->flip_x) {
        for (uint32_t y = 0; y < h; ++y) {
            for (uint32_t x = 0; x < w / 2; ++x) {
                uint32_t x2 = w - 1 - x;
                for (uint8_t c = 0; c < ch; ++c) {
                    size_t idx1 = (y * w + x) * ch + c;
                    size_t idx2 = (y * w + x2) * ch + c;
                    std::swap(result[idx1], result[idx2]);
                }
            }
        }
    }

    // Flip Y: mirror vertically
    if (tex->flip_y) {
        for (uint32_t y = 0; y < h / 2; ++y) {
            uint32_t y2 = h - 1 - y;
            for (uint32_t x = 0; x < w; ++x) {
                for (uint8_t c = 0; c < ch; ++c) {
                    size_t idx1 = (y * w + x) * ch + c;
                    size_t idx2 = (y2 * w + x) * ch + c;
                    std::swap(result[idx1], result[idx2]);
                }
            }
        }
    }

    return {std::move(result), w, h};
}

} // namespace termin
