#pragma once

#include <cmath>
#include <array>

namespace termin {


// ============================================================================
// Vec3 (double)
// ============================================================================

struct Vec3 {
    double x, y, z;

    Vec3() : x(0), y(0), z(0) {}
    Vec3(double x, double y, double z) : x(x), y(y), z(z) {}

    double& operator[](int i) { return (&x)[i]; }
    double operator[](int i) const { return (&x)[i]; }

    Vec3 operator+(const Vec3& v) const { return {x + v.x, y + v.y, z + v.z}; }
    Vec3 operator-(const Vec3& v) const { return {x - v.x, y - v.y, z - v.z}; }
    Vec3 operator*(double s) const { return {x * s, y * s, z * s}; }
    Vec3 operator/(double s) const { return {x / s, y / s, z / s}; }
    Vec3 operator-() const { return {-x, -y, -z}; }

    Vec3& operator+=(const Vec3& v) { x += v.x; y += v.y; z += v.z; return *this; }
    Vec3& operator-=(const Vec3& v) { x -= v.x; y -= v.y; z -= v.z; return *this; }
    Vec3& operator*=(double s) { x *= s; y *= s; z *= s; return *this; }
    Vec3& operator/=(double s) { x /= s; y /= s; z /= s; return *this; }

    bool operator==(const Vec3& v) const { return x == v.x && y == v.y && z == v.z; }
    bool operator!=(const Vec3& v) const { return !(*this == v); }

    double dot(const Vec3& v) const { return x * v.x + y * v.y + z * v.z; }

    Vec3 cross(const Vec3& v) const {
        return {
            y * v.z - z * v.y,
            z * v.x - x * v.z,
            x * v.y - y * v.x
        };
    }

    double norm() const { return std::sqrt(x * x + y * y + z * z); }
    double norm_squared() const { return x * x + y * y + z * z; }

    Vec3 normalized() const {
        double n = norm();
        return n > 1e-10 ? *this / n : Vec3{0, 0, 1};
    }

    // Angle between two vectors in radians
    static double angle(const Vec3& a, const Vec3& b) {
        double d = a.normalized().dot(b.normalized());
        d = d < -1.0 ? -1.0 : (d > 1.0 ? 1.0 : d);  // clamp
        return std::acos(d);
    }

    // Angle between two vectors in degrees
    static double angle_degrees(const Vec3& a, const Vec3& b) {
        return angle(a, b) * 180.0 / 3.14159265358979323846;
    }

    static Vec3 zero() { return {0, 0, 0}; }
    static Vec3 unit_x() { return {1, 0, 0}; }
    static Vec3 unit_y() { return {0, 1, 0}; }
    static Vec3 unit_z() { return {0, 0, 1}; }
};

inline Vec3 operator*(double s, const Vec3& v) { return v * s; }


// ============================================================================
// Vec3f (float)
// ============================================================================

struct Vec3f {
    float x, y, z;

    Vec3f() : x(0), y(0), z(0) {}
    Vec3f(float x, float y, float z) : x(x), y(y), z(z) {}
    explicit Vec3f(const Vec3& v) : x(static_cast<float>(v.x)), y(static_cast<float>(v.y)), z(static_cast<float>(v.z)) {}

    float& operator[](int i) { return (&x)[i]; }
    float operator[](int i) const { return (&x)[i]; }

    Vec3f operator+(const Vec3f& v) const { return {x + v.x, y + v.y, z + v.z}; }
    Vec3f operator-(const Vec3f& v) const { return {x - v.x, y - v.y, z - v.z}; }
    Vec3f operator*(float s) const { return {x * s, y * s, z * s}; }
    Vec3f operator/(float s) const { return {x / s, y / s, z / s}; }
    Vec3f operator-() const { return {-x, -y, -z}; }

    Vec3f& operator+=(const Vec3f& v) { x += v.x; y += v.y; z += v.z; return *this; }
    Vec3f& operator-=(const Vec3f& v) { x -= v.x; y -= v.y; z -= v.z; return *this; }
    Vec3f& operator*=(float s) { x *= s; y *= s; z *= s; return *this; }
    Vec3f& operator/=(float s) { x /= s; y /= s; z /= s; return *this; }

    bool operator==(const Vec3f& v) const { return x == v.x && y == v.y && z == v.z; }
    bool operator!=(const Vec3f& v) const { return !(*this == v); }

    float dot(const Vec3f& v) const { return x * v.x + y * v.y + z * v.z; }

    Vec3f cross(const Vec3f& v) const {
        return {
            y * v.z - z * v.y,
            z * v.x - x * v.z,
            x * v.y - y * v.x
        };
    }

    float norm() const { return std::sqrt(x * x + y * y + z * z); }
    float norm_squared() const { return x * x + y * y + z * z; }

    Vec3f normalized() const {
        float n = norm();
        return n > 1e-6f ? *this / n : Vec3f{0, 0, 1};
    }

    Vec3 to_double() const { return {x, y, z}; }

    static Vec3f zero() { return {0, 0, 0}; }
    static Vec3f unit_x() { return {1, 0, 0}; }
    static Vec3f unit_y() { return {0, 1, 0}; }
    static Vec3f unit_z() { return {0, 0, 1}; }
};

inline Vec3f operator*(float s, const Vec3f& v) { return v * s; }


// ============================================================================
// Vec2f (float)
// ============================================================================

struct Vec2f {
    float x = 0, y = 0;
    Vec2f() = default;
    Vec2f(float x_, float y_) : x(x_), y(y_) {}
};


// ============================================================================
// Vec4f (float)
// ============================================================================

// w component stores handedness: +1 or -1
struct Vec4f {
    float x = 0, y = 0, z = 0, w = 1;
    Vec4f() = default;
    Vec4f(float x_, float y_, float z_, float w_ = 1.0f) : x(x_), y(y_), z(z_), w(w_) {}
};


} // namespace termin
