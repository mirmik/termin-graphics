#pragma once

// TcTexture - RAII wrapper with handle-based access to tc_texture
// Uses tc_texture_handle with generation checking for safety

extern "C" {
#include <tgfx/resources/tc_texture.h>
#include <tgfx/resources/tc_texture_registry.h>
}

#include <tgfx/tgfx_api.h>
#include <string>
#include <cstring>
#include <vector>
#include <cstdint>
#include <cstddef>
#include <tuple>

namespace termin {

struct TexturePixelDataView {
    const void* data = nullptr;
    uint32_t width = 0;
    uint32_t height = 0;
    uint8_t channels = 4;

    size_t byte_size() const {
        return static_cast<size_t>(width) * height * channels;
    }
};

struct TextureTransformFlags {
    bool flip_x = false;
    bool flip_y = true;
    bool transpose = false;
};

struct TcTextureCreateInfo {
    TexturePixelDataView pixels;
    TextureTransformFlags transform;
    std::string name;
    std::string source_path;
    std::string uuid_hint;
};

// TcTexture - texture wrapper with registry integration
// Stores handle (index + generation) instead of raw pointer
class TGFX_API TcTexture {
public:
    tc_texture_handle handle = tc_texture_handle_invalid();

    TcTexture() = default;

    explicit TcTexture(tc_texture_handle h) : handle(h) {
        if (tc_texture* t = tc_texture_get(handle)) {
            tc_texture_add_ref(t);
        }
    }

    // Construct from raw pointer (finds handle for it)
    explicit TcTexture(tc_texture* t) {
        if (t) {
            handle = tc_texture_find(t->header.uuid);
            tc_texture_add_ref(t);
        }
    }

    TcTexture(const TcTexture& other) : handle(other.handle) {
        if (tc_texture* t = tc_texture_get(handle)) {
            tc_texture_add_ref(t);
        }
    }

    TcTexture(TcTexture&& other) noexcept : handle(other.handle) {
        other.handle = tc_texture_handle_invalid();
    }

    TcTexture& operator=(const TcTexture& other) {
        if (this != &other) {
            if (tc_texture* t = tc_texture_get(handle)) {
                tc_texture_release(t);
            }
            handle = other.handle;
            if (tc_texture* t = tc_texture_get(handle)) {
                tc_texture_add_ref(t);
            }
        }
        return *this;
    }

    TcTexture& operator=(TcTexture&& other) noexcept {
        if (this != &other) {
            if (tc_texture* t = tc_texture_get(handle)) {
                tc_texture_release(t);
            }
            handle = other.handle;
            other.handle = tc_texture_handle_invalid();
        }
        return *this;
    }

    ~TcTexture() {
        if (tc_texture* t = tc_texture_get(handle)) {
            tc_texture_release(t);
        }
        handle = tc_texture_handle_invalid();
    }

    // Get raw pointer (may return nullptr if handle is stale)
    tc_texture* get() const { return tc_texture_get(handle); }

    // Query (safe - returns defaults if handle is stale)
    bool is_valid() const { return tc_texture_is_valid(handle); }

    const char* uuid() const {
        tc_texture* t = get();
        return t ? t->header.uuid : "";
    }

    const char* name() const {
        tc_texture* t = get();
        return (t && t->header.name) ? t->header.name : "";
    }

    uint32_t version() const {
        tc_texture* t = get();
        return t ? t->header.version : 0;
    }

    uint32_t width() const {
        tc_texture* t = get();
        return t ? t->width : 0;
    }

    uint32_t height() const {
        tc_texture* t = get();
        return t ? t->height : 0;
    }

    uint8_t channels() const {
        tc_texture* t = get();
        return t ? t->channels : 0;
    }

    const void* data() const {
        tc_texture* t = get();
        return t ? t->data : nullptr;
    }

    size_t data_size() const {
        tc_texture* t = get();
        return t ? (size_t)t->width * t->height * t->channels : 0;
    }

    // Transform flags
    bool flip_x() const {
        tc_texture* t = get();
        return t && t->flip_x;
    }

    bool flip_y() const {
        tc_texture* t = get();
        return t && t->flip_y;
    }

    bool transpose() const {
        tc_texture* t = get();
        return t && t->transpose;
    }

    const char* source_path() const {
        tc_texture* t = get();
        return (t && t->source_path) ? t->source_path : "";
    }

    void bump_version() {
        if (tc_texture* t = get()) {
            t->header.version++;
        }
    }

    // Set texture data
    bool set_data(
        const void* pixel_data,
        uint32_t w,
        uint32_t h,
        uint8_t ch,
        const std::string& tex_name = "",
        const std::string& src_path = ""
    ) {
        tc_texture* t = get();
        if (!t) return false;
        return tc_texture_set_data(
            t,
            pixel_data,
            w, h, ch,
            tex_name.empty() ? nullptr : tex_name.c_str(),
            src_path.empty() ? nullptr : src_path.c_str()
        );
    }

    // Set transform flags
    void set_transforms(bool fx, bool fy, bool trans) {
        if (tc_texture* t = get()) {
            tc_texture_set_transforms(t, fx, fy, trans);
        }
    }

    // Create TcTexture from raw pixel data
    static TcTexture from_data(const TcTextureCreateInfo& info);

    // Create 1x1 white texture
    static TcTexture white_1x1();

    // Create 1x1 flat tangent-space normal texture
    static TcTexture normal_1x1();

    // Create 1x1 depth texture for sampler2DShadow placeholders (AMD compatibility)
    // Returns 1.0 (fully lit) when sampled
    static TcTexture dummy_shadow_1x1();

    // Get by UUID from registry
    static TcTexture from_uuid(const std::string& uuid) {
        tc_texture_handle h = tc_texture_find(uuid.c_str());
        if (tc_texture_handle_is_invalid(h)) {
            return TcTexture();
        }
        return TcTexture(h);
    }

    // Get or create by UUID
    static TcTexture get_or_create(const std::string& uuid) {
        tc_texture_handle h = tc_texture_get_or_create(uuid.c_str());
        if (tc_texture_handle_is_invalid(h)) {
            return TcTexture();
        }
        return TcTexture(h);
    }

    // Get transformed data for GPU upload
    // Returns new buffer with transforms applied, plus final width and height
    std::tuple<std::vector<uint8_t>, uint32_t, uint32_t> get_upload_data() const;

    // Sync GPU-first texture data to CPU. No-op for CPU-first textures.
    // After a successful call, data() returns the pixel content.
    bool sync_to_cpu() {
        tc_texture* t = get();
        return tc_texture_sync_to_cpu(t);
    }

    // Set mipmap flag (affects next upload)
    void set_mipmap(bool enable) {
        if (tc_texture* t = get()) {
            t->mipmap = enable ? 1 : 0;
        }
    }

    // Set clamp flag (affects next upload)
    void set_clamp(bool enable) {
        if (tc_texture* t = get()) {
            t->clamp = enable ? 1 : 0;
        }
    }

    bool mipmap() const {
        tc_texture* t = get();
        return t && t->mipmap;
    }

    bool clamp() const {
        tc_texture* t = get();
        return t && t->clamp;
    }
};

} // namespace termin
