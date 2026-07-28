#include "tgfx2/path2d.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

#include <tcbase/tc_log.h>

namespace tgfx {
namespace {

bool finite(termin::Vec2f p) { return std::isfinite(p.x) && std::isfinite(p.y); }

unsigned point_count(Path2Verb verb) {
    switch (verb) {
        case Path2Verb::MoveTo:
        case Path2Verb::LineTo: return 1;
        case Path2Verb::QuadraticTo: return 2;
        case Path2Verb::CubicTo: return 3;
        case Path2Verb::Close: return 0;
    }
    return std::numeric_limits<unsigned>::max();
}

float distance_to_line(termin::Vec2f p, termin::Vec2f a, termin::Vec2f b) {
    const float dx = b.x - a.x;
    const float dy = b.y - a.y;
    const float length = std::hypot(dx, dy);
    if (length == 0.0f) return std::hypot(p.x - a.x, p.y - a.y);
    return std::abs(dy * p.x - dx * p.y + b.x * a.y - b.y * a.x) / length;
}

termin::Vec2f midpoint(termin::Vec2f a, termin::Vec2f b) {
    return {(a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f};
}

void flatten_quad(termin::Vec2f a, termin::Vec2f b, termin::Vec2f c,
                  float tolerance, unsigned depth, std::vector<termin::Vec2f>& out) {
    if (depth == 16 || distance_to_line(b, a, c) <= tolerance) {
        out.push_back(c);
        return;
    }
    const auto ab = midpoint(a, b);
    const auto bc = midpoint(b, c);
    const auto abc = midpoint(ab, bc);
    flatten_quad(a, ab, abc, tolerance, depth + 1, out);
    flatten_quad(abc, bc, c, tolerance, depth + 1, out);
}

void flatten_cubic(termin::Vec2f a, termin::Vec2f b, termin::Vec2f c,
                   termin::Vec2f d, float tolerance, unsigned depth,
                   std::vector<termin::Vec2f>& out) {
    if (depth == 16 &&
        std::max(distance_to_line(b, a, d), distance_to_line(c, a, d)) > tolerance) {
        tc_log_warn("Path2f::flatten: curve subdivision reached depth limit");
    }
    if (depth == 16 ||
        std::max(distance_to_line(b, a, d), distance_to_line(c, a, d)) <= tolerance) {
        out.push_back(d);
        return;
    }
    const auto ab = midpoint(a, b);
    const auto bc = midpoint(b, c);
    const auto cd = midpoint(c, d);
    const auto abc = midpoint(ab, bc);
    const auto bcd = midpoint(bc, cd);
    const auto abcd = midpoint(abc, bcd);
    flatten_cubic(a, ab, abc, abcd, tolerance, depth + 1, out);
    flatten_cubic(abcd, bcd, cd, d, tolerance, depth + 1, out);
}

float segment_distance(termin::Vec2f p, termin::Vec2f a, termin::Vec2f b) {
    const float dx = b.x - a.x;
    const float dy = b.y - a.y;
    const float length2 = dx * dx + dy * dy;
    if (length2 == 0.0f) return std::hypot(p.x - a.x, p.y - a.y);
    const float t = std::clamp(((p.x - a.x) * dx + (p.y - a.y) * dy) / length2,
                               0.0f, 1.0f);
    return std::hypot(p.x - (a.x + t * dx), p.y - (a.y + t * dy));
}

}  // namespace

bool Color4f::is_finite() const noexcept {
    return std::isfinite(r) && std::isfinite(g) && std::isfinite(b) && std::isfinite(a);
}

bool FillPaint::validate() const {
    if (!color.is_finite() ||
        (rule != FillRule::NonZero && rule != FillRule::EvenOdd)) {
        tc_log_error("FillPaint: finite color and known fill rule required");
        return false;
    }
    return true;
}

bool StrokePaint::validate() const {
    if (!color.is_finite() || !std::isfinite(width) || width < 0.0f ||
        !std::isfinite(miter_limit) || miter_limit < 1.0f ||
        !std::isfinite(dash_offset) ||
        (join != StrokeJoin::Miter && join != StrokeJoin::Round &&
         join != StrokeJoin::Bevel) ||
        (cap != StrokeCap::Butt && cap != StrokeCap::Round &&
         cap != StrokeCap::Square)) {
        tc_log_error("StrokePaint: non-finite or out-of-range scalar");
        return false;
    }
    float total = 0.0f;
    for (float length : dash_pattern) {
        if (!std::isfinite(length) || length <= 0.0f) {
            tc_log_error("StrokePaint: dash lengths must be finite and positive");
            return false;
        }
        total += length;
    }
    if (!dash_pattern.empty() && (dash_pattern.size() % 2) != 0) {
        tc_log_error("StrokePaint: dash pattern must contain on/off pairs");
        return false;
    }
    return std::isfinite(total);
}

bool Path2f::move_to(termin::Vec2f point) {
    if (!finite(point)) {
        tc_log_error("Path2f::move_to: point must be finite");
        return false;
    }
    verbs_.push_back(Path2Verb::MoveTo);
    points_.push_back(point);
    return true;
}

bool Path2f::line_to(termin::Vec2f point) {
    if (!finite(point) || verbs_.empty() || verbs_.back() == Path2Verb::Close) {
        tc_log_error("Path2f::line_to: finite point and open contour required");
        return false;
    }
    verbs_.push_back(Path2Verb::LineTo);
    points_.push_back(point);
    return true;
}

bool Path2f::quadratic_to(termin::Vec2f control, termin::Vec2f end) {
    if (!finite(control) || !finite(end) || verbs_.empty() ||
        verbs_.back() == Path2Verb::Close) {
        tc_log_error("Path2f::quadratic_to: finite points and open contour required");
        return false;
    }
    verbs_.push_back(Path2Verb::QuadraticTo);
    points_.insert(points_.end(), {control, end});
    return true;
}

bool Path2f::cubic_to(termin::Vec2f c1, termin::Vec2f c2, termin::Vec2f end) {
    if (!finite(c1) || !finite(c2) || !finite(end) || verbs_.empty() ||
        verbs_.back() == Path2Verb::Close) {
        tc_log_error("Path2f::cubic_to: finite points and open contour required");
        return false;
    }
    verbs_.push_back(Path2Verb::CubicTo);
    points_.insert(points_.end(), {c1, c2, end});
    return true;
}

bool Path2f::close() {
    if (verbs_.empty() || verbs_.back() == Path2Verb::Close) {
        tc_log_error("Path2f::close: open contour required");
        return false;
    }
    verbs_.push_back(Path2Verb::Close);
    return true;
}

void Path2f::clear() noexcept {
    verbs_.clear();
    points_.clear();
}

bool Path2f::try_assign(std::span<const Path2Verb> verbs,
                        std::span<const termin::Vec2f> points) {
    Path2f candidate;
    std::size_t point_index = 0;
    for (Path2Verb verb : verbs) {
        const unsigned count = point_count(verb);
        if (count == std::numeric_limits<unsigned>::max() ||
            point_index + count > points.size()) {
            tc_log_error("Path2f::try_assign: malformed verb/point stream");
            return false;
        }
        bool ok = false;
        switch (verb) {
            case Path2Verb::MoveTo: ok = candidate.move_to(points[point_index]); break;
            case Path2Verb::LineTo: ok = candidate.line_to(points[point_index]); break;
            case Path2Verb::QuadraticTo:
                ok = candidate.quadratic_to(points[point_index], points[point_index + 1]);
                break;
            case Path2Verb::CubicTo:
                ok = candidate.cubic_to(points[point_index], points[point_index + 1],
                                        points[point_index + 2]);
                break;
            case Path2Verb::Close: ok = candidate.close(); break;
        }
        if (!ok) {
            tc_log_error("Path2f::try_assign: invalid path topology");
            return false;
        }
        point_index += count;
    }
    if (point_index != points.size()) {
        tc_log_error("Path2f::try_assign: unconsumed points");
        return false;
    }
    *this = std::move(candidate);
    return true;
}

FlattenedPath2f Path2f::flatten(float tolerance, const termin::Affine2f& transform) const {
    FlattenedPath2f result;
    if (!std::isfinite(tolerance) || tolerance <= 0.0f || !transform.is_finite()) {
        tc_log_error("Path2f::flatten: finite positive tolerance and transform required");
        return result;
    }
    std::size_t pi = 0;
    termin::Vec2f current{};
    for (Path2Verb verb : verbs_) {
        if (verb == Path2Verb::MoveTo) {
            current = transform.transform_point(points_[pi++]);
            result.contours.push_back({{current}, false});
        } else if (verb == Path2Verb::LineTo) {
            current = transform.transform_point(points_[pi++]);
            result.contours.back().points.push_back(current);
        } else if (verb == Path2Verb::QuadraticTo) {
            std::vector<termin::Vec2f> transformed;
            const auto control = transform.transform_point(points_[pi]);
            const auto end = transform.transform_point(points_[pi + 1]);
            flatten_quad(current, control, end, tolerance, 0, transformed);
            current = end;
            pi += 2;
            result.contours.back().points.insert(result.contours.back().points.end(),
                                                 transformed.begin(), transformed.end());
        } else if (verb == Path2Verb::CubicTo) {
            std::vector<termin::Vec2f> transformed;
            const auto control1 = transform.transform_point(points_[pi]);
            const auto control2 = transform.transform_point(points_[pi + 1]);
            const auto end = transform.transform_point(points_[pi + 2]);
            flatten_cubic(current, control1, control2, end, tolerance, 0, transformed);
            current = end;
            pi += 3;
            result.contours.back().points.insert(result.contours.back().points.end(),
                                                 transformed.begin(), transformed.end());
        } else {
            result.contours.back().closed = true;
        }
    }
    for (const auto& contour : result.contours) {
        for (auto p : contour.points) {
            if (result.empty) {
                result.bounds = {p.x, p.y, p.x, p.y};
                result.empty = false;
            } else {
                result.bounds.x0 = std::min(result.bounds.x0, p.x);
                result.bounds.y0 = std::min(result.bounds.y0, p.y);
                result.bounds.x1 = std::max(result.bounds.x1, p.x);
                result.bounds.y1 = std::max(result.bounds.y1, p.y);
            }
        }
    }
    return result;
}

termin::Bounds2f Path2f::bounds() const {
    return flatten(0.25f).bounds;
}

termin::Bounds2f Path2f::transformed_bounds(const termin::Affine2f& transform) const {
    return flatten(0.25f, transform).bounds;
}

termin::Bounds2f Path2f::stroke_bounds(const StrokePaint& stroke,
                                       const termin::Affine2f& transform) const {
    auto flat = flatten(0.25f, transform);
    if (flat.empty || !stroke.validate()) return flat.bounds;
    const float extent = stroke.width * 0.5f * std::max(1.0f, stroke.miter_limit);
    flat.bounds.x0 -= extent;
    flat.bounds.y0 -= extent;
    flat.bounds.x1 += extent;
    flat.bounds.y1 += extent;
    return flat.bounds;
}

bool FlattenedPath2f::contains(termin::Vec2f p, FillRule rule) const noexcept {
    int winding = 0;
    for (const auto& contour : contours) {
        if (!contour.closed || contour.points.size() < 3) continue;
        for (std::size_t i = 0; i < contour.points.size(); ++i) {
            const auto a = contour.points[i];
            const auto b = contour.points[(i + 1) % contour.points.size()];
            if ((a.y <= p.y && b.y > p.y) || (a.y > p.y && b.y <= p.y)) {
                const float x = a.x + (p.y - a.y) * (b.x - a.x) / (b.y - a.y);
                if (x > p.x) winding += (b.y > a.y) ? 1 : -1;
            }
        }
    }
    return rule == FillRule::EvenOdd ? (std::abs(winding) % 2) == 1 : winding != 0;
}

bool FlattenedPath2f::stroke_contains(termin::Vec2f p,
                                      const StrokePaint& stroke) const noexcept {
    if (stroke.width <= 0.0f || !std::isfinite(stroke.width)) return false;
    const float radius = stroke.width * 0.5f;
    for (const auto& contour : contours) {
        for (std::size_t i = 1; i < contour.points.size(); ++i) {
            if (segment_distance(p, contour.points[i - 1], contour.points[i]) <= radius)
                return true;
        }
        if (contour.closed && contour.points.size() > 1 &&
            segment_distance(p, contour.points.back(), contour.points.front()) <= radius)
            return true;
    }
    return false;
}

}  // namespace tgfx
