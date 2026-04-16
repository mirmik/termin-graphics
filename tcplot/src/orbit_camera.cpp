// orbit_camera.cpp - Port of tcplot/camera3d.py.

#include "tcplot/orbit_camera.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace tcplot {

namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kDegToRad = kPi / 180.0f;

// Column-major 4x4 matrix helpers. Index: (col, row) → col * 4 + row.
inline float& M(float* m, int col, int row) { return m[col * 4 + row]; }

inline void set_identity(float m[16]) {
    for (int i = 0; i < 16; ++i) m[i] = 0.0f;
    m[0] = m[5] = m[10] = m[15] = 1.0f;
}

inline float dot3(const float a[3], const float b[3]) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

inline void sub3(const float a[3], const float b[3], float out[3]) {
    out[0] = a[0] - b[0];
    out[1] = a[1] - b[1];
    out[2] = a[2] - b[2];
}

inline void cross3(const float a[3], const float b[3], float out[3]) {
    const float x = a[1] * b[2] - a[2] * b[1];
    const float y = a[2] * b[0] - a[0] * b[2];
    const float z = a[0] * b[1] - a[1] * b[0];
    out[0] = x; out[1] = y; out[2] = z;
}

inline void normalize3(float v[3]) {
    const float n = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]) + 1e-12f;
    v[0] /= n;
    v[1] /= n;
    v[2] /= n;
}

// Build a standard right-handed look-at view matrix (camera at `eye`
// looking at `target`, `up` is world up). Matches Python camera3d.py
// bit-for-bit.
void look_at(const float eye[3], const float target[3], const float up[3],
             float m[16]) {
    float f[3];
    sub3(target, eye, f);
    normalize3(f);

    float s[3];
    cross3(f, up, s);
    normalize3(s);

    float u[3];
    cross3(s, f, u);

    set_identity(m);
    // Row 0 = s; row 1 = u; row 2 = -f. With column-major storage,
    // row r, col c maps to m[c*4 + r]. Right-handed, -Z forward.
    M(m, 0, 0) = s[0];    M(m, 1, 0) = s[1];    M(m, 2, 0) = s[2];
    M(m, 0, 1) = u[0];    M(m, 1, 1) = u[1];    M(m, 2, 1) = u[2];
    M(m, 0, 2) = -f[0];   M(m, 1, 2) = -f[1];   M(m, 2, 2) = -f[2];
    M(m, 3, 0) = -dot3(s, eye);
    M(m, 3, 1) = -dot3(u, eye);
    M(m, 3, 2) = dot3(f, eye);
}

// OpenGL-standard perspective projection (depth in [-1, 1]).
void perspective(float fov_y, float aspect, float near, float far,
                 float m[16]) {
    for (int i = 0; i < 16; ++i) m[i] = 0.0f;
    const float f = 1.0f / std::tan(fov_y * 0.5f);
    M(m, 0, 0) = f / aspect;
    M(m, 1, 1) = f;
    M(m, 2, 2) = (far + near) / (near - far);
    M(m, 3, 2) = (2.0f * far * near) / (near - far);
    M(m, 2, 3) = -1.0f;
}

// Column-major 4x4 multiply: out = a * b.
void mul4(const float a[16], const float b[16], float out[16]) {
    float t[16];
    for (int col = 0; col < 4; ++col) {
        for (int row = 0; row < 4; ++row) {
            float sum = 0.0f;
            for (int k = 0; k < 4; ++k) {
                sum += a[k * 4 + row] * b[col * 4 + k];
            }
            t[col * 4 + row] = sum;
        }
    }
    std::memcpy(out, t, sizeof(t));
}

}  // namespace

OrbitCamera::OrbitCamera()
    : azimuth(45.0f * kDegToRad),
      elevation(30.0f * kDegToRad),
      fov_y(45.0f * kDegToRad),
      min_elevation(-89.0f * kDegToRad),
      max_elevation(89.0f * kDegToRad) {}

void OrbitCamera::compute_eye(float out[3]) const {
    const float ce = std::cos(elevation);
    const float se = std::sin(elevation);
    const float ca = std::cos(azimuth);
    const float sa = std::sin(azimuth);
    // Direction matches camera3d.py: +X·sin(azimuth), -Y·cos(azimuth), +Z·sin(elev).
    out[0] = target[0] + distance * (ce * sa);
    out[1] = target[1] + distance * (-ce * ca);
    out[2] = target[2] + distance * se;
}

void OrbitCamera::view_matrix(float out16[16]) const {
    float eye[3];
    compute_eye(eye);
    const float up[3] = {0.0f, 0.0f, 1.0f};
    look_at(eye, target, up, out16);
}

void OrbitCamera::projection_matrix(float aspect, float out16[16]) const {
    perspective(fov_y, aspect, near, far, out16);
}

void OrbitCamera::mvp(float aspect, float out16[16]) const {
    float proj[16];
    float view[16];
    projection_matrix(aspect, proj);
    view_matrix(view);
    mul4(proj, view, out16);
}

void OrbitCamera::orbit(float d_azimuth, float d_elevation) {
    azimuth += d_azimuth;
    elevation = std::clamp(elevation + d_elevation, min_elevation, max_elevation);
}

void OrbitCamera::zoom(float factor) {
    distance = std::clamp(distance * factor, min_distance, max_distance);
}

void OrbitCamera::pan(float dx, float dy) {
    const float ce = std::cos(elevation);
    const float se = std::sin(elevation);
    const float ca = std::cos(azimuth);
    const float sa = std::sin(azimuth);

    // Screen X basis (camera right).
    float right[3] = {ca, sa, 0.0f};
    // Forward (eye→target, flipped sign to match Python which uses
    // -ce*sa, +ce*ca, -se).
    float forward[3] = {-ce * sa, ce * ca, -se};
    // Screen Y basis = right × forward, then normalize.
    float up[3];
    cross3(right, forward, up);
    normalize3(up);

    const float scale = distance * 0.002f;
    target[0] += right[0] * dx * scale + up[0] * dy * scale;
    target[1] += right[1] * dx * scale + up[1] * dy * scale;
    target[2] += right[2] * dx * scale + up[2] * dy * scale;
}

void OrbitCamera::fit_bounds(const float bounds_min[3], const float bounds_max[3]) {
    target[0] = (bounds_min[0] + bounds_max[0]) * 0.5f;
    target[1] = (bounds_min[1] + bounds_max[1]) * 0.5f;
    target[2] = (bounds_min[2] + bounds_max[2]) * 0.5f;

    const float dx = bounds_max[0] - bounds_min[0];
    const float dy = bounds_max[1] - bounds_min[1];
    const float dz = bounds_max[2] - bounds_min[2];
    const float size = std::sqrt(dx * dx + dy * dy + dz * dz);

    distance = std::max(size * 1.2f, min_distance);
}

}  // namespace tcplot
