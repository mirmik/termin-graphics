// Canonical owned 2D path and paint values shared by rendering and hit testing.
#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include <termin/geom/affine2.hpp>
#include <termin/geom/bounds2.hpp>
#include <termin/geom/vec2.hpp>

#include "tgfx2/tgfx2_api.h"

namespace tgfx {

struct Color4f {
    float r = 1.0f;
    float g = 1.0f;
    float b = 1.0f;
    float a = 1.0f;

    static constexpr Color4f white() noexcept { return {}; }
    static constexpr Color4f transparent() noexcept { return {0, 0, 0, 0}; }
    bool is_finite() const noexcept;
};

enum class FillRule : std::uint8_t { NonZero, EvenOdd };
enum class StrokeJoin : std::uint8_t { Miter, Round, Bevel };
enum class StrokeCap : std::uint8_t { Butt, Round, Square };

struct FillPaint {
    Color4f color{};
    FillRule rule = FillRule::NonZero;

    bool validate() const;
};

struct StrokePaint {
    Color4f color{};
    float width = 1.0f;
    StrokeJoin join = StrokeJoin::Miter;
    StrokeCap cap = StrokeCap::Butt;
    float miter_limit = 4.0f;
    std::vector<float> dash_pattern;
    float dash_offset = 0.0f;

    bool validate() const;
};

enum class Path2Verb : std::uint8_t {
    MoveTo,
    LineTo,
    QuadraticTo,
    CubicTo,
    Close,
};

struct FlattenedContour2f {
    std::vector<termin::Vec2f> points;
    bool closed = false;
};

struct FlattenedPath2f {
    std::vector<FlattenedContour2f> contours;
    termin::Bounds2f bounds{};
    bool empty = true;

    bool contains(termin::Vec2f point, FillRule rule) const noexcept;
    bool stroke_contains(termin::Vec2f point, const StrokePaint& stroke) const noexcept;
};

// Path2f owns all verb and point memory. Copies are deep and detached; no
// backend context or GPU resource is retained. Mutation is transactional when
// importing raw streams: a rejected stream leaves the previous path unchanged.
class TGFX2_TYPE_API Path2f {
public:
    bool move_to(termin::Vec2f point);
    bool line_to(termin::Vec2f point);
    bool quadratic_to(termin::Vec2f control, termin::Vec2f end);
    bool cubic_to(termin::Vec2f control1, termin::Vec2f control2, termin::Vec2f end);
    bool close();
    void clear() noexcept;

    bool try_assign(std::span<const Path2Verb> verbs,
                    std::span<const termin::Vec2f> points);

    const std::vector<Path2Verb>& verbs() const noexcept { return verbs_; }
    const std::vector<termin::Vec2f>& points() const noexcept { return points_; }
    bool empty() const noexcept { return verbs_.empty(); }

    termin::Bounds2f bounds() const;
    termin::Bounds2f transformed_bounds(const termin::Affine2f& transform) const;
    FlattenedPath2f flatten(float tolerance = 0.25f,
                            const termin::Affine2f& transform = termin::Affine2f::identity()) const;
    termin::Bounds2f stroke_bounds(const StrokePaint& stroke,
                                   const termin::Affine2f& transform =
                                       termin::Affine2f::identity()) const;

private:
    std::vector<Path2Verb> verbs_;
    std::vector<termin::Vec2f> points_;
};

}  // namespace tgfx
