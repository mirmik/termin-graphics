#include "termin/render/debug_geometry.hpp"

#include <algorithm>
#include <utility>

namespace termin {

DebugGeometryTypeRegistration::DebugGeometryTypeRegistration(
    const char* stable_id,
    const char* display_name,
    const char* category,
    bool default_enabled
) : type_id_(tc_debug_geometry_type_register(
        stable_id, display_name, category, default_enabled)) {}

DebugGeometryTypeRegistration::~DebugGeometryTypeRegistration() {
    if (tc_debug_geometry_type_registered(type_id_)) {
        tc_debug_geometry_type_unregister(type_id_);
    }
}

DebugGeometryTypeRegistration::DebugGeometryTypeRegistration(
    DebugGeometryTypeRegistration&& other
) noexcept : type_id_(std::exchange(
        other.type_id_, TC_DEBUG_GEOMETRY_TYPE_INVALID)) {}

DebugGeometryTypeRegistration& DebugGeometryTypeRegistration::operator=(
    DebugGeometryTypeRegistration&& other
) noexcept {
    if (this == &other) return *this;
    if (tc_debug_geometry_type_registered(type_id_)) {
        tc_debug_geometry_type_unregister(type_id_);
    }
    type_id_ = std::exchange(other.type_id_, TC_DEBUG_GEOMETRY_TYPE_INVALID);
    return *this;
}

bool DebugGeometryDrawer::line(
    const Vec3& start,
    const Vec3& end,
    const Color4& color,
    bool depth_test
) const {
    const float start_data[3] = {
        static_cast<float>(start.x),
        static_cast<float>(start.y),
        static_cast<float>(start.z),
    };
    const float end_data[3] = {
        static_cast<float>(end.x),
        static_cast<float>(end.y),
        static_cast<float>(end.z),
    };
    const float color_data[4] = {color.r, color.g, color.b, color.a};
    return tc_debug_geometry_drawer_line(
        &drawer_, start_data, end_data, color_data, depth_test);
}

bool DebugGeometryDrawer::wire_sphere(
    const Vec3& center,
    double radius,
    const Color4& color,
    int segments,
    bool depth_test
) const {
    const float center_data[3] = {
        static_cast<float>(center.x),
        static_cast<float>(center.y),
        static_cast<float>(center.z),
    };
    const float color_data[4] = {color.r, color.g, color.b, color.a};
    const uint16_t segment_count = static_cast<uint16_t>(
        std::clamp(segments, 3, 65535));
    return tc_debug_geometry_drawer_wire_sphere(
        &drawer_, center_data, static_cast<float>(radius), color_data,
        segment_count, depth_test);
}

} // namespace termin
