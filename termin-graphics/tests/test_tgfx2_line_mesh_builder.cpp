#include "guard_main.h"

#include <tgfx2/line_mesh_builder.hpp>

#include <cmath>

namespace {

bool close_to(float a, float b, float eps = 1.0e-5f) {
    return std::fabs(a - b) <= eps;
}

} // namespace

TEST_CASE("line mesh builder returns empty mesh for invalid input") {
    tgfx::LineStyle style;
    style.width = 0.1f;

    tgfx::LinePoint3 one[] = {{0.0f, 0.0f, 0.0f}};
    tgfx::LineMesh mesh = tgfx::build_line_mesh(one, style);

    CHECK(mesh.vertices.empty());
    CHECK(mesh.indices.empty());
}

TEST_CASE("line mesh builder creates a quad for a straight butt line") {
    tgfx::LineStyle style;
    style.width = 0.2f;
    style.up_hint = {0.0f, 0.0f, 1.0f};
    style.cap = tgfx::LineCapStyle::Butt;
    style.join = tgfx::LineJoinStyle::Bevel;

    tgfx::LinePoint3 points[] = {
        {0.0f, 0.0f, 0.0f},
        {1.0f, 0.0f, 0.0f},
    };
    tgfx::LineMesh mesh = tgfx::build_line_mesh(points, style);

    CHECK_EQ(mesh.vertices.size(), 6u);
    CHECK_EQ(mesh.indices.size(), 6u);
    CHECK(close_to(mesh.vertices[0].position.y, 0.1f));
    CHECK(close_to(mesh.vertices[1].position.y, -0.1f));
    CHECK(close_to(mesh.vertices[2].position.x, 1.0f));
}

TEST_CASE("line mesh builder skips duplicate points") {
    tgfx::LineStyle style;
    style.width = 0.2f;
    style.up_hint = {0.0f, 0.0f, 1.0f};
    style.cap = tgfx::LineCapStyle::Butt;
    style.join = tgfx::LineJoinStyle::Bevel;

    tgfx::LinePoint3 points[] = {
        {0.0f, 0.0f, 0.0f},
        {0.0f, 0.0f, 0.0f},
        {1.0f, 0.0f, 0.0f},
    };
    tgfx::LineMesh mesh = tgfx::build_line_mesh(points, style);

    CHECK_EQ(mesh.vertices.size(), 6u);
    CHECK_EQ(mesh.indices.size(), 6u);
}

TEST_CASE("line mesh builder round caps add semicircle geometry") {
    tgfx::LineStyle style;
    style.width = 0.2f;
    style.up_hint = {0.0f, 0.0f, 1.0f};
    style.cap = tgfx::LineCapStyle::Round;
    style.join = tgfx::LineJoinStyle::Bevel;
    style.round_segments = 6;

    tgfx::LinePoint3 points[] = {
        {0.0f, 0.0f, 0.0f},
        {1.0f, 0.0f, 0.0f},
    };
    tgfx::LineMesh mesh = tgfx::build_line_mesh(points, style);

    CHECK_EQ(mesh.vertices.size(), 42u);
    CHECK_EQ(mesh.indices.size(), 42u);
}
