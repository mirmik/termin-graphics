#include "tgfx2/path2d.hpp"

#include <cassert>
#include <cmath>
#include <limits>
#include <vector>

int main() {
    tgfx::Path2f path;
    assert(path.move_to({0, 0}));
    assert(path.line_to({10, 0}));
    assert(path.quadratic_to({15, 5}, {10, 10}));
    assert(path.cubic_to({8, 12}, {2, 12}, {0, 10}));
    assert(path.close());

    const auto flat = path.flatten(0.1f);
    assert(!flat.empty);
    assert(flat.contours.size() == 1);
    assert(flat.contours[0].closed);
    assert(flat.contains({5, 5}, tgfx::FillRule::NonZero));
    assert(!flat.contains({50, 50}, tgfx::FillRule::EvenOdd));

    const auto transformed =
        path.transformed_bounds(termin::Affine2f::translation({3, 4}) * termin::Affine2f::shear(0.5f, 0.0f));
    assert(transformed.x0 >= 3.0f);
    assert(transformed.y0 >= 4.0f);

    tgfx::StrokePaint stroke;
    stroke.width = 2.0f;
    assert(stroke.validate());
    assert(flat.stroke_contains({5, 0.5f}, stroke));
    stroke.dash_pattern = {2.0f};
    assert(!stroke.validate());

    tgfx::FillPaint fill;
    assert(fill.validate());
    fill.rule = static_cast<tgfx::FillRule>(255);
    assert(!fill.validate());

    // Raw imports are transactional.
    const auto old_verbs = path.verbs();
    const auto old_points = path.points();
    const std::vector<tgfx::Path2Verb> malformed = {tgfx::Path2Verb::MoveTo, tgfx::Path2Verb::CubicTo};
    const std::vector<termin::Vec2f> too_few = {{1, 2}, {3, 4}};
    assert(!path.try_assign(malformed, too_few));
    assert(path.verbs() == old_verbs);
    assert(path.points() == old_points);

    assert(!path.line_to({std::numeric_limits<float>::quiet_NaN(), 0}));
    assert(path.verbs() == old_verbs);
}
