// viewport_config.hpp - C++ ViewportConfig class
#pragma once

#include <string>
#include <tuple>

extern "C" {
#include <tc_viewport_config.h>
}

namespace termin {

class ViewportConfig {
public:
    std::string name;
    std::string display_name = "Main";
    std::string render_target_name;
    float region_x = 0.0f;
    float region_y = 0.0f;
    float region_w = 1.0f;
    float region_h = 1.0f;
    int depth = 0;
    std::string input_mode = "simple";
    bool block_input_in_editor = false;
    bool enabled = true;

    ViewportConfig() = default;

    std::tuple<float, float, float, float> region() const {
        return {region_x, region_y, region_w, region_h};
    }

    void set_region(float x, float y, float w, float h) {
        region_x = x;
        region_y = y;
        region_w = w;
        region_h = h;
    }

    tc_viewport_config to_c() const;
    static ViewportConfig from_c(const tc_viewport_config* c);
};

} // namespace termin
