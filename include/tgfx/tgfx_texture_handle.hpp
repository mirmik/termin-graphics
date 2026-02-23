#pragma once

// TcTexture - RAII wrapper with handle-based access to tc_texture
// Uses tc_texture_handle with generation checking for safety

extern "C" {
#include <tgfx/resources/tc_texture.h>
#include <tgfx/resources/tc_texture_registry.h>
#include <tgfx/tc_gpu_context.h>
#include <tgfx/tgfx_resource_gpu.h>
}

#include <string>
#include <cstring>
#include <vector>
#include <cstdint>
#include <tuple>

namespace termin {

// TcTexture - texture wrapper with registry integration
// Stores handle (index + generation) instead of raw pointer
class TcTexture {
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

    // For backwards compatibility
    tc_texture* texture_ptr() const { return get(); }

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
    static TcTexture from_data(
        const void* pixel_data,
        uint32_t width,
        uint32_t height,
        uint8_t channels = 4,
        bool flip_x = false,
        bool flip_y = true,
        bool transpose = false,
        const std::string& name = "",
        const std::string& source_path = "",
        const std::string& uuid_hint = ""
    );

    // Create 1x1 white texture
    static TcTexture white_1x1();

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

    // ========== GPU operations ==========

    // Bind texture to unit, uploading if needed
    // Returns true if bind succeeded
    bool bind_gpu(int unit = 0) {
        tc_texture* t = get();
        return t ? tc_texture_bind_gpu(t, unit) : false;
    }

    // Force re-upload texture to GPU
    bool upload_gpu() {
        tc_texture* t = get();
        return t ? tc_texture_upload_gpu(t) : false;
    }

    // Delete texture from GPU (keeps CPU data)
    void delete_gpu() {
        if (tc_texture* t = get()) {
            tc_texture_delete_gpu(t);
        }
    }

    // Check if texture needs GPU upload (version mismatch)
    bool needs_upload() const {
        tc_texture* t = get();
        return t ? tc_texture_needs_upload(t) : false;
    }

    // Get GPU texture ID for current context (0 = not uploaded)
    uint32_t gpu_id() const {
        tc_texture* t = get();
        if (!t) return 0;
        tc_gpu_context* ctx = tc_gpu_get_context();
        if (!ctx) return 0;
        tc_gpu_slot* slot = tc_gpu_context_texture_slot(ctx, t->header.pool_index);
        return slot ? slot->gl_id : 0;
    }

    // Get GPU version for current context (-1 = never uploaded)
    int32_t gpu_version() const {
        tc_texture* t = get();
        if (!t) return -1;
        tc_gpu_context* ctx = tc_gpu_get_context();
        if (!ctx) return -1;
        tc_gpu_slot* slot = tc_gpu_context_texture_slot(ctx, t->header.pool_index);
        return slot ? slot->version : -1;
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
