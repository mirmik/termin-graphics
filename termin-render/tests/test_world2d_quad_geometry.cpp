#include <cmath>

#include <termin/render/world2d_quad_geometry.hpp>

#include "guard_main.h"

namespace {

bool near(double lhs, double rhs) {
    return std::abs(lhs - rhs) < 1.0e-6;
}

} // namespace

TEST_CASE("world2d quad bounds stay planar and use all transformed corners") {
    const termin::World2DQuadRect rect{-1.0, -2.0, 1.0, 2.0};
    const termin::Mat44 model =
        termin::Mat44::translation({3.0, 4.0, 5.0}) *
        termin::Mat44::scale({-2.0, 7.0, 0.5});

    const termin::AABB bounds = termin::world2d_quad_bounds(rect, model);
    CHECK(near(bounds.min_point.x, 1.0));
    CHECK(near(bounds.max_point.x, 5.0));
    CHECK(near(bounds.min_point.y, 4.0));
    CHECK(near(bounds.max_point.y, 4.0));
    CHECK(near(bounds.min_point.z, 4.0));
    CHECK(near(bounds.max_point.z, 6.0));
}

TEST_CASE("world2d quad ray picking hits exact transformed surface") {
    const termin::World2DQuadRect rect{-1.0, -1.0, 1.0, 1.0};
    const termin::Mat44 model = termin::Mat44::translation({2.0, 3.0, 4.0});

    double distance = 0.0;
    REQUIRE(termin::ray_intersects_world2d_quad(
        {2.0, 0.0, 4.0}, {0.0, 1.0, 0.0}, rect, model, &distance));
    CHECK(near(distance, 3.0));

    CHECK_FALSE(termin::ray_intersects_world2d_quad(
        {4.0, 0.0, 4.0}, {0.0, 1.0, 0.0}, rect, model));
}

TEST_CASE("world2d quad ray picking rejects parallel and zero rays") {
    const termin::World2DQuadRect rect{-1.0, -1.0, 1.0, 1.0};
    const termin::Mat44 model = termin::Mat44::identity();

    CHECK_FALSE(termin::ray_intersects_world2d_quad(
        {0.0, 1.0, 0.0}, {1.0, 0.0, 0.0}, rect, model));
    CHECK_FALSE(termin::ray_intersects_world2d_quad(
        {0.0, 1.0, 0.0}, {0.0, 0.0, 0.0}, rect, model));
}

GUARD_TEST_MAIN();
