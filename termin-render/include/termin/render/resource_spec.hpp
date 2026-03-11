#pragma once

#include <array>
#include <optional>
#include <string>

#include "tgfx/types.hpp"

namespace termin {

struct ResourceSpec {
    std::string resource;
    std::string resource_type = "fbo";
    std::optional<std::pair<int, int>> size;
    std::optional<std::array<double, 4>> clear_color;
    std::optional<float> clear_depth;
    std::optional<std::string> format;
    int samples = 1;
    std::string viewport_name;
    float scale = 1.0f;
    TextureFilter filter = TextureFilter::LINEAR;

    ResourceSpec() = default;

    ResourceSpec(
        std::string resource_,
        std::string resource_type_ = "fbo",
        std::optional<std::pair<int, int>> size_ = std::nullopt,
        std::optional<std::array<double, 4>> clear_color_ = std::nullopt,
        std::optional<float> clear_depth_ = std::nullopt,
        std::optional<std::string> format_ = std::nullopt,
        int samples_ = 1,
        std::string viewport_name_ = "",
        float scale_ = 1.0f,
        TextureFilter filter_ = TextureFilter::LINEAR
    ) : resource(std::move(resource_)),
        resource_type(std::move(resource_type_)),
        size(std::move(size_)),
        clear_color(std::move(clear_color_)),
        clear_depth(std::move(clear_depth_)),
        format(std::move(format_)),
        samples(samples_),
        viewport_name(std::move(viewport_name_)),
        scale(scale_),
        filter(filter_) {}
};

} // namespace termin
