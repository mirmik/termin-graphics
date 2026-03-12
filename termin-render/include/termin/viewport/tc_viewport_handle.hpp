#pragma once

extern "C" {
#include "render/tc_viewport.h"
}

#include <string>

namespace termin {

class TcViewport {
public:
    tc_viewport_handle handle_ = TC_VIEWPORT_HANDLE_INVALID;

public:
    TcViewport() = default;
    explicit TcViewport(tc_viewport_handle h) : handle_(h) {}

    TcViewport(const TcViewport& other) = default;
    TcViewport(TcViewport&& other) noexcept = default;
    TcViewport& operator=(const TcViewport& other) = default;
    TcViewport& operator=(TcViewport&& other) noexcept = default;

    ~TcViewport() = default;

    tc_viewport_handle handle() const { return handle_; }
    bool is_valid() const { return tc_viewport_alive(handle_); }

    const char* name() const {
        return is_valid() ? tc_viewport_get_name(handle_) : "";
    }

    bool enabled() const {
        return is_valid() ? tc_viewport_get_enabled(handle_) : false;
    }

    int depth() const {
        return is_valid() ? tc_viewport_get_depth(handle_) : 0;
    }

    uint64_t layer_mask() const {
        return is_valid() ? tc_viewport_get_layer_mask(handle_) : 0xFFFFFFFFFFFFFFFFULL;
    }

    tc_scene_handle scene() const {
        return is_valid() ? tc_viewport_get_scene(handle_) : TC_SCENE_HANDLE_INVALID;
    }

    tc_component* camera() const {
        return is_valid() ? tc_viewport_get_camera(handle_) : nullptr;
    }

    tc_pipeline_handle pipeline() const {
        return is_valid() ? tc_viewport_get_pipeline(handle_) : TC_PIPELINE_HANDLE_INVALID;
    }

    void set_enabled(bool enabled) {
        if (is_valid()) {
            tc_viewport_set_enabled(handle_, enabled);
        }
    }

    void set_depth(int depth) {
        if (is_valid()) {
            tc_viewport_set_depth(handle_, depth);
        }
    }

    void set_layer_mask(uint64_t mask) {
        if (is_valid()) {
            tc_viewport_set_layer_mask(handle_, mask);
        }
    }

    void set_scene(tc_scene_handle scene) {
        if (is_valid()) {
            tc_viewport_set_scene(handle_, scene);
        }
    }

    void set_camera(tc_component* camera) {
        if (is_valid()) {
            tc_viewport_set_camera(handle_, camera);
        }
    }

    void set_pipeline(tc_pipeline_handle pipeline) {
        if (is_valid()) {
            tc_viewport_set_pipeline(handle_, pipeline);
        }
    }

    void set_input_manager(tc_input_manager* manager) {
        if (is_valid()) {
            tc_viewport_set_input_manager(handle_, manager);
        }
    }

    tc_input_manager* input_manager() const {
        return is_valid() ? tc_viewport_get_input_manager(handle_) : nullptr;
    }

    void get_rect(float& x, float& y, float& w, float& h) const {
        if (is_valid()) {
            tc_viewport_get_rect(handle_, &x, &y, &w, &h);
        } else {
            x = y = 0.0f;
            w = h = 1.0f;
        }
    }

    void set_rect(float x, float y, float w, float h) {
        if (is_valid()) {
            tc_viewport_set_rect(handle_, x, y, w, h);
        }
    }

    void get_pixel_rect(int& px, int& py, int& pw, int& ph) const {
        if (is_valid()) {
            tc_viewport_get_pixel_rect(handle_, &px, &py, &pw, &ph);
        } else {
            px = py = 0;
            pw = ph = 1;
        }
    }

    void set_pixel_rect(int px, int py, int pw, int ph) {
        if (is_valid()) {
            tc_viewport_set_pixel_rect(handle_, px, py, pw, ph);
        }
    }

    void update_pixel_rect(int display_width, int display_height) {
        if (is_valid()) {
            tc_viewport_update_pixel_rect(handle_, display_width, display_height);
        }
    }

    static TcViewport create(const std::string& name,
                             tc_scene_handle scene = TC_SCENE_HANDLE_INVALID,
                             tc_component* camera = nullptr) {
        tc_viewport_handle h = tc_viewport_new(name.c_str(), scene, camera);
        return TcViewport(h);
    }

    void destroy() {
        if (is_valid()) {
            tc_viewport_free(handle_);
            handle_ = TC_VIEWPORT_HANDLE_INVALID;
        }
    }

    static TcViewport from_handle(tc_viewport_handle h) {
        return TcViewport(h);
    }
};

} // namespace termin
