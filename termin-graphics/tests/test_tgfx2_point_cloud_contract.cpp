#include "guard_main.h"

#include <cstddef>
#include <type_traits>

#include <tgfx2/point_cloud_renderer.hpp>

TEST_CASE("point cloud GPU instance contract stays packed and standard-layout") {
    static_assert(std::is_standard_layout_v<tgfx::PointCloudPoint>);
    static_assert(std::is_trivially_copyable_v<tgfx::PointCloudPoint>);

    CHECK_EQ(sizeof(tgfx::PointCloudPoint), 8u * sizeof(float));
    CHECK_EQ(offsetof(tgfx::PointCloudPoint, position), 0u);
    CHECK_EQ(offsetof(tgfx::PointCloudPoint, size_scale), 3u * sizeof(float));
    CHECK_EQ(offsetof(tgfx::PointCloudPoint, color), 4u * sizeof(float));
}

TEST_CASE("point cloud defaults are directly drawable") {
    tgfx::PointCloudStyle style;
    CHECK_EQ(style.size_px, 3.0f);
    CHECK(style.shape == tgfx::PointCloudShape::Circle);
    CHECK(style.depth_test);
    CHECK(style.depth_write);

    tgfx::PointCloudDrawParams params;
    for (size_t i = 0; i < params.view_projection.size(); ++i) {
        const bool diagonal = i == 0 || i == 5 || i == 10 || i == 15;
        CHECK_EQ(params.view_projection[i], diagonal ? 1.0f : 0.0f);
    }

    tgfx::PointCloud cloud;
    CHECK(cloud.empty());
    CHECK_EQ(cloud.point_count(), 0u);
    CHECK_FALSE(cloud.has_bounds());
}
