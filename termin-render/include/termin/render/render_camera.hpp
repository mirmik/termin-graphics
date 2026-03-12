#pragma once

#include <termin/geom/mat44.hpp>
#include <termin/geom/vec3.hpp>

namespace termin {

struct RenderCamera {
public:
    Mat44 view = Mat44::identity();
    Mat44 projection = Mat44::identity();
    Vec3 position = Vec3::zero();
    double near_clip = 0.1;
    double far_clip = 100.0;

public:
    Mat44 get_view_matrix() const { return view; }
    Mat44 get_projection_matrix() const { return projection; }
    Mat44 view_matrix() const { return view; }
    Mat44 projection_matrix() const { return projection; }
    Vec3 get_position() const { return position; }
};

} // namespace termin
