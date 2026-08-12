#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include <termin/geom/color.hpp>
#include <tgfx/tgfx_mesh3.hpp>
#include <tgfx2/point_cloud_renderer.hpp>

#include "termin_visual_scene/export.h"

namespace termin::visual {

    inline constexpr char PrimitiveDrawProtocol3D[] = "termin.visual.primitive-triangles.v1";
    inline constexpr char StaticMeshDrawProtocol3D[] = "termin.visual.static-mesh.v1";
    inline constexpr char PointCloudDrawProtocol3D[] = "termin.visual.point-cloud.v1";

    struct PrimitiveVertex3D {
        termin::Vec3f position{};
        termin::LinearColor color{1.0f, 1.0f, 1.0f, 1.0f};
    };

    struct PrimitiveGeometry3D {
        std::vector<PrimitiveVertex3D> vertices;
        std::vector<std::uint32_t> triangles;
        // Empty means triangle_index + 1. Otherwise one token per triangle.
        std::vector<std::uint64_t> triangle_parts;
    };

    struct PrimitiveDrawPacket3D {
        std::shared_ptr<const PrimitiveGeometry3D> geometry;
        bool depth_test = true;
    };

    // Immutable, renderer-neutral sRGB texel snapshot. GPU textures belong to
    // the presentation host because one scene may be painted by multiple
    // devices or backends.
    struct BaseColorTextureData3D {
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        std::vector<std::uint8_t> rgba8;
    };

    struct StaticMeshDrawPacket3D {
        std::shared_ptr<const termin::Mesh3> mesh;
        std::shared_ptr<const BaseColorTextureData3D> base_color_texture;
        termin::LinearColor tint{1.0f, 1.0f, 1.0f, 1.0f};
        bool depth_test = true;
    };

    struct PointCloudData3D {
        std::vector<tgfx::PointCloudPoint> points;
    };

    struct PointCloudDrawPacket3D {
        std::shared_ptr<const PointCloudData3D> cloud;
        tgfx::PointCloudStyle style{};
    };

} // namespace termin::visual
