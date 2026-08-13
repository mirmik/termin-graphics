#pragma once

#include <optional>
#include <string>

#include "tgfx/types.hpp"
#include <termin/geom/color.hpp>

namespace termin {

    struct ResourceSpec {
        std::string resource;
        std::string resource_type = "fbo";
        std::optional<std::pair<int, int>> size;
        std::optional<termin::LinearColor> clear_color;
        std::optional<float> clear_depth;
        std::optional<std::string> format;
        int samples = 1;
        int array_layers = 1;
        std::optional<bool> has_color;
        std::optional<bool> has_depth;
        std::string viewport_name;
        float scale = 1.0f;
        TextureFilter filter = TextureFilter::LINEAR;

        ResourceSpec() = default;

        ResourceSpec(std::string resource_,
                     std::string resource_type_ = "fbo",
                     std::optional<std::pair<int, int>> size_ = std::nullopt,
                     std::optional<termin::LinearColor> clear_color_ = std::nullopt,
                     std::optional<float> clear_depth_ = std::nullopt,
                     std::optional<std::string> format_ = std::nullopt,
                     int samples_ = 1,
                     std::string viewport_name_ = "",
                     float scale_ = 1.0f,
                     TextureFilter filter_ = TextureFilter::LINEAR,
                     std::optional<bool> has_color_ = std::nullopt,
                     std::optional<bool> has_depth_ = std::nullopt)
            : resource(std::move(resource_)),
              resource_type(std::move(resource_type_)),
              size(std::move(size_)),
              clear_color(std::move(clear_color_)),
              clear_depth(std::move(clear_depth_)),
              format(std::move(format_)),
              samples(samples_),
              has_color(has_color_),
              has_depth(has_depth_),
              viewport_name(std::move(viewport_name_)),
              scale(scale_),
              filter(filter_) {}
    };

} // namespace termin
