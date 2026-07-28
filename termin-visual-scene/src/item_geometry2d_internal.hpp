#pragma once

#include <algorithm>
#include <cmath>

#include <termin/geom/rect2.hpp>
#include <tgfx2/path2d.hpp>

namespace termin::visual::detail {

inline bool valid_point(termin::Vec2f point) {
    return std::isfinite(point.x) &&
        std::isfinite(point.y);
}

inline bool valid_rect(termin::Rect2f rect) {
    return std::isfinite(rect.x) &&
        std::isfinite(rect.y) &&
        std::isfinite(rect.width) &&
        std::isfinite(rect.height) &&
        rect.width >= 0.0f &&
        rect.height >= 0.0f;
}

inline bool valid_bounds(termin::Bounds2f bounds) {
    return std::isfinite(bounds.x0) &&
        std::isfinite(bounds.y0) &&
        std::isfinite(bounds.x1) &&
        std::isfinite(bounds.y1) &&
        bounds.x1 >= bounds.x0 &&
        bounds.y1 >= bounds.y0;
}

inline termin::Bounds2f rect_bounds(termin::Rect2f rect) {
    return {
        rect.x,
        rect.y,
        rect.x + rect.width,
        rect.y + rect.height,
    };
}

inline termin::Bounds2f expanded(
    termin::Bounds2f bounds,
    float amount)
{
    return {
        bounds.x0 - amount,
        bounds.y0 - amount,
        bounds.x1 + amount,
        bounds.y1 + amount,
    };
}

inline termin::Bounds2f merged(
    termin::Bounds2f a,
    termin::Bounds2f b)
{
    return {
        std::min(a.x0, b.x0),
        std::min(a.y0, b.y0),
        std::max(a.x1, b.x1),
        std::max(a.y1, b.y1),
    };
}

inline bool bounds_contains(
    termin::Bounds2f bounds,
    termin::Vec2f point)
{
    return point.x >= bounds.x0 &&
        point.x <= bounds.x1 &&
        point.y >= bounds.y0 &&
        point.y <= bounds.y1;
}

inline bool rect_contains(
    termin::Rect2f rect,
    termin::Vec2f point)
{
    return point.x >= rect.x &&
        point.x <= rect.x + rect.width &&
        point.y >= rect.y &&
        point.y <= rect.y + rect.height;
}

inline bool rounded_rect_contains(
    termin::Rect2f rect,
    float radius,
    termin::Vec2f point)
{
    if (!rect_contains(rect, point)) return false;
    const float r = std::min(
        radius,
        std::min(rect.width, rect.height) * 0.5f);
    if (r <= 0.0f) return true;
    const float cx = std::clamp(
        point.x,
        rect.x + r,
        rect.x + rect.width - r);
    const float cy = std::clamp(
        point.y,
        rect.y + r,
        rect.y + rect.height - r);
    const float dx = point.x - cx;
    const float dy = point.y - cy;
    return dx * dx + dy * dy <= r * r;
}

inline bool ellipse_contains(
    termin::Rect2f bounds,
    termin::Vec2f point)
{
    if (bounds.width <= 0.0f ||
        bounds.height <= 0.0f) {
        return false;
    }
    const float nx =
        (point.x - (bounds.x + bounds.width * 0.5f)) /
        (bounds.width * 0.5f);
    const float ny =
        (point.y - (bounds.y + bounds.height * 0.5f)) /
        (bounds.height * 0.5f);
    return nx * nx + ny * ny <= 1.0f;
}

}  // namespace termin::visual::detail
