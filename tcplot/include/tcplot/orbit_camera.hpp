// orbit_camera.hpp - Orbit camera for tcplot's 3D view.
//
// Port of tcplot/tcplot/camera3d.py. Convention: Y-forward (depth), Z-up.
// The camera orbits around a `target` point at `distance`; `azimuth` and
// `elevation` parameterise the direction from target to eye.
//
// Matrix output:
//   view_matrix()    — right-handed look-at, +Z up.
//   projection_matrix(aspect) — OpenGL-standard perspective (symmetric
//                               clip, depth in [-1, 1]).
//
// We deliberately do NOT reuse termin::Mat44::perspective / look_at —
// the engine convention there (Y = forward, projection swap) differs
// from what the Python tcplot implementation produced pixel-for-pixel.
// Keeping this local preserves visual parity with the existing demos.
#pragma once

#include <array>

#include "tcplot/tcplot_api.h"

namespace tcplot {

class TCPLOT_API OrbitCamera {
public:
    // Target point in world space.
    float target[3] = {0.0f, 0.0f, 0.0f};
    // Distance from target to eye.
    float distance = 5.0f;
    // Horizontal angle (radians). 0 = looking from +X toward origin.
    float azimuth;    // = 45 deg, set in ctor
    // Vertical angle (radians). 0 = horizontal, +pi/2 = straight up.
    float elevation;  // = 30 deg, set in ctor
    // Vertical FOV in radians.
    float fov_y;      // = 45 deg
    float near = 0.01f;
    float far = 1000.0f;

    // Clamping limits.
    float min_distance = 0.01f;
    float max_distance = 10000.0f;
    float min_elevation;  // = -89 deg
    float max_elevation;  // = +89 deg

    OrbitCamera();

    // Camera eye position in world space.
    void compute_eye(float out[3]) const;

    // View matrix (4x4 column-major, 16 floats). Written to `out16`.
    void view_matrix(float out16[16]) const;

    // Perspective projection matrix. Right-handed, depth in [-1, 1].
    void projection_matrix(float aspect, float out16[16]) const;

    // Model-View-Projection with identity model.
    void mvp(float aspect, float out16[16]) const;

    // Input handlers.
    void orbit(float d_azimuth, float d_elevation);
    void zoom(float factor);
    void pan(float dx, float dy);

    // Re-centre on a bounding box; adjusts target and distance so the
    // full box fits in view at the current FOV.
    void fit_bounds(const float bounds_min[3], const float bounds_max[3]);
};

}  // namespace tcplot
